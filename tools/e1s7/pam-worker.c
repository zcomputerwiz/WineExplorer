/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Identity verification only. Never setuid, setgid, pam_setcred, change a
 * password, open a PAM session, or launch a target. Not installed by default.
 */
#define _GNU_SOURCE
#include "protocol.h"
#ifndef WEX_E1S7_LAB
int main(void) { return 1; } /* Fail closed, no PAM/worker implementation. */
#else
#define WEX_ACCOUNTS_STANDALONE
#ifdef WEX_E1S7_TEST
#define WEX_ACCOUNTS_TEST
#endif
/* Exactly S4's protected map parser and real NSS provider, without edits. */
#include "../../wine/server/accounts.c"
#include <security/pam_appl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#ifndef WEX_E1S7_PAM_SERVICE
#define WEX_E1S7_PAM_SERVICE "wex-e1s-test"
#endif
#ifndef SO_PASSPIDFD
#define SO_PASSPIDFD 76
#endif
#ifndef SCM_PIDFD
#define SCM_PIDFD 4
#endif

enum reason { RES_SETUP=1, RES_PROTOCOL, RES_CANCEL, RES_DEADLINE, RES_MAP, RES_PAM,
              RES_LIMIT, RES_USER, RES_RNG, RES_AUDIT, RES_OK };
struct context
{
    int control, dialog, peer_pin, audit, peer_pid, peer_uid;
    uint64_t request, deadline;
    uint32_t attempt, round, index, style;
    unsigned char nonce[32], io[S7_PACKET], key[16];
    unsigned char answer[S7_TEXT+1];
    size_t answer_len;
    uint64_t release_at;
    int normalized, answer_used;
    struct wex_account account;
    struct pam_response *volatile owned;
    volatile sig_atomic_t owned_count;
    size_t owned_sizes[S7_MESSAGES];
    enum reason reason;
    int pam_status, map_status, locked, stage;
};
static struct context ctx;
static char deadline_audit[128];
static volatile sig_atomic_t deadline_audit_len;
static unsigned char timeout_result[S7_RESULT_SIZE];
static volatile sig_atomic_t release_timeout;

static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint64_t le64(const unsigned char *p)
{ return le32(p) | (uint64_t)le32(p+4)<<32; }
static void put32(unsigned char *p, uint32_t n)
{ unsigned int i; for (i=0; i<4; i++) p[i]=(unsigned char)(n>>(8*i)); }
static void put64(unsigned char *p, uint64_t n)
{ put32(p, (uint32_t)n); put32(p+4, (uint32_t)(n>>32)); }
static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return 0;
    return (uint64_t)ts.tv_sec*1000000000u + ts.tv_nsec;
}
static int pin_alive(void)
{
    struct pollfd p={ctx.peer_pin, POLLIN, 0};
    return poll(&p, 1, 0)==0;
}
static int live(void)
{
    uint64_t now=now_ns();
    if (!now || now>=ctx.deadline) { ctx.reason=RES_DEADLINE; return 0; }
    return pin_alive();
}
static int ready(int fd, short events)
{
    struct pollfd p[2]={{fd, events, 0},{ctx.peer_pin,POLLIN,0}};
    uint64_t now, remaining;
    int n;
    for (;;)
    {
        if (!live()) return 0;
        now=now_ns();
        if (!now || now>=ctx.deadline) return 0;
        remaining=(ctx.deadline-now+999999)/1000000;
        n=poll(p, 2, (int)remaining);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) { ctx.reason=RES_DEADLINE; return 0; }
        if (p[1].revents || (p[0].revents & (POLLERR|POLLHUP|POLLNVAL))) return 0;
        return !!(p[0].revents & events);
    }
}
static void header(unsigned int type, size_t size)
{
    memset(ctx.io, 0, S7_HEADER);
    memcpy(ctx.io, "E1S7PAM", 7);
    ctx.io[8]=1; ctx.io[10]=(unsigned char)type;
    put32(ctx.io+12, (uint32_t)size);
    put64(ctx.io+16, ctx.request); put32(ctx.io+24, ctx.attempt);
    put32(ctx.io+28, ctx.round); put32(ctx.io+32, ctx.index);
    put32(ctx.io+36, ctx.style); memcpy(ctx.io+40, ctx.nonce, 32);
}
static int send_packet(int fd, size_t size)
{
    if (!ready(fd, POLLOUT)) return 0;
    return send(fd, ctx.io, size, MSG_NOSIGNAL|MSG_DONTWAIT)==(ssize_t)size;
}
/* The peer pin comes from S6, not a client PID lookup. SCM credentials are
 * checked on EVERY answer. A retained live pidfd prevents numeric PID reuse.
 * Close kernel-issued and unexpected descriptors even on a malformed packet. */
static ssize_t receive_packet(int fd, int dialog)
{
    union { struct cmsghdr align; unsigned char bytes[256]; } ancillary;
    struct iovec iov={ctx.io, sizeof(ctx.io)};
    struct msghdr msg={0};
    struct cmsghdr *c;
    struct ucred cred={0};
    int valid=1, creds=0, pins=0;
    ssize_t n;
    explicit_bzero(ctx.io, sizeof(ctx.io));
    if (!ready(fd, POLLIN)) return -1;
    msg.msg_iov=&iov; msg.msg_iovlen=1;
    msg.msg_control=ancillary.bytes; msg.msg_controllen=sizeof(ancillary.bytes);
    n=recvmsg(fd, &msg, MSG_CMSG_CLOEXEC|MSG_DONTWAIT);
    if (n<0) return -1;
    for (c=CMSG_FIRSTHDR(&msg); c; c=CMSG_NXTHDR(&msg,c))
    {
        size_t len=c->cmsg_len >= CMSG_LEN(0) ? c->cmsg_len-CMSG_LEN(0) : 0;
        if (c->cmsg_level==SOL_SOCKET && (c->cmsg_type==SCM_RIGHTS || c->cmsg_type==SCM_PIDFD))
        {
            size_t i; int v;
            if (c->cmsg_type==SCM_PIDFD && len==sizeof(int)) pins++;
            else valid=0;
            for (i=0; i+sizeof(int)<=len; i+=sizeof(int))
            { memcpy(&v, (unsigned char *)CMSG_DATA(c)+i, sizeof(v)); if (v>=0) close(v); }
        }
        else if (c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_CREDENTIALS && len==sizeof(cred))
        { memcpy(&cred, CMSG_DATA(c), sizeof(cred)); creds++; }
        else valid=0;
    }
    if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) valid=0;
    if (dialog && (creds!=1 || pins!=1 || cred.pid!=ctx.peer_pid || cred.uid!=(uid_t)ctx.peer_uid || !pin_alive())) valid=0;
    if (!dialog && (creds || pins)) valid=0;
    return valid && n>0 ? n : -1;
}
static int bound(unsigned int type, ssize_t n)
{
    return n>=(ssize_t)S7_HEADER && !memcmp(ctx.io, "E1S7PAM\0", 8) &&
           ctx.io[8]==1 && !ctx.io[9] && ctx.io[10]==type && !ctx.io[11] &&
           le32(ctx.io+12)==(uint32_t)n && le64(ctx.io+16)==ctx.request &&
           le32(ctx.io+24)==ctx.attempt && !memcmp(ctx.io+40, ctx.nonce, 32);
}
/* Strict UTF-8, no embedded NUL, surrogate, overlong or out-of-range scalar. */
static int text_valid(const unsigned char *s, size_t len)
{
    size_t i=0; unsigned int c, min, more;
    while (i<len)
    {
        c=s[i++]; if (!c) return 0;
        if (c<0x80) continue;
        if (c>=0xc2 && c<=0xdf) { c&=31; more=1; min=0x80; }
        else if (c>=0xe0 && c<=0xef) { c&=15; more=2; min=0x800; }
        else if (c>=0xf0 && c<=0xf4) { c&=7; more=3; min=0x10000; }
        else return 0;
        if (len-i<more) return 0;
        while (more--) { if ((s[i]&0xc0)!=0x80) return 0; c=(c<<6)|(s[i++]&63); }
        if (c<min || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0;
    }
    return 1;
}
static void block_deadline(sigset_t *previous)
{
    sigset_t set;
    sigemptyset(&set); sigaddset(&set,SIGALRM);
    if (sigprocmask(SIG_BLOCK,&set,previous)) _exit(1);
}
static void restore_deadline(const sigset_t *previous)
{
    if (sigprocmask(SIG_SETMASK,previous,NULL)) _exit(1);
}
static void free_responses(struct pam_response *r, int count)
{
    int i;
    sigset_t previous;
    if (!r) return;
    /* The timer handler must never follow a pointer concurrent with free. */
    block_deadline(&previous);
    for (i=0; i<count; i++) if (r[i].resp)
    { explicit_bzero(r[i].resp, strlen(r[i].resp)); free(r[i].resp); }
    explicit_bzero(r, (size_t)count*sizeof(*r)); free(r);
    ctx.owned=NULL; ctx.owned_count=0;
    memset(ctx.owned_sizes,0,sizeof(ctx.owned_sizes));
    restore_deadline(&previous);
}
static int quiet_dialog(void)
{
    struct pollfd p={ctx.dialog,POLLIN,0};
    /* Before a challenge and before success, ANY queued packet (including
     * cancellation, extra/pre-sent answers), EOF or socket error is terminal. */
    return live() && poll(&p,1,0)==0;
}
static int wire_conversation(int count, const struct pam_message **messages,
                             struct pam_response **out, void *data)
{
    struct pam_response *r=NULL;
    sigset_t previous;
    int i;
    (void)data; *out=NULL;
    ctx.reason=RES_LIMIT;
    if (!live() || ++ctx.round>S7_ROUNDS || count<1 || count>(int)S7_MESSAGES) goto fail;
    if (!(r=calloc((size_t)count, sizeof(*r)))) goto fail;
    block_deadline(&previous);
    ctx.owned=r; ctx.owned_count=count;
    memset(ctx.owned_sizes,0,sizeof(ctx.owned_sizes));
    restore_deadline(&previous);
    for (i=0; i<count; i++)
    {
        size_t len; ssize_t n;
        ctx.reason=RES_PROTOCOL; ctx.index=(uint32_t)i;
        if (!quiet_dialog() || !messages[i] || !messages[i]->msg) goto fail;
        ctx.style=(uint32_t)messages[i]->msg_style;
        if (ctx.style!=PAM_PROMPT_ECHO_OFF && ctx.style!=PAM_PROMPT_ECHO_ON &&
            ctx.style!=PAM_ERROR_MSG && ctx.style!=PAM_TEXT_INFO) goto fail;
        len=strnlen(messages[i]->msg, S7_TEXT+1);
        if (len>S7_TEXT || !text_valid((const unsigned char *)messages[i]->msg,len)) goto fail;
        header(S7_PROMPT, S7_HEADER+len); memcpy(ctx.io+S7_HEADER, messages[i]->msg,len);
        if (!send_packet(ctx.dialog,S7_HEADER+len)) goto fail;
        n=receive_packet(ctx.dialog,1);
        if (bound(S7_CANCEL,n) && n==S7_HEADER && !le32(ctx.io+28) && !le32(ctx.io+32) && !le32(ctx.io+36))
        { ctx.reason=RES_CANCEL; goto fail; }
        if (!bound(S7_ANSWER,n) || le32(ctx.io+28)!=ctx.round || le32(ctx.io+32)!=ctx.index ||
            le32(ctx.io+36)!=ctx.style) goto fail;
        len=(size_t)n-S7_HEADER;
        if (!text_valid(ctx.io+S7_HEADER,len)) goto fail;
        if (ctx.style==PAM_ERROR_MSG || ctx.style==PAM_TEXT_INFO)
        { if (len) goto fail; }
        else
        {
            block_deadline(&previous);
            if (!(r[i].resp=malloc(len+1))) { restore_deadline(&previous); goto fail; }
            ctx.owned_sizes[i]=len+1;
            /* Keep these pages locked until this short-lived process exits,
             * even after ownership passes to PAM. Best effort under memlock
             * quotas; do not touch or munlock a PAM-freed pointer later. */
            (void)mlock(r[i].resp,len+1);
            memcpy(r[i].resp,ctx.io+S7_HEADER,len); r[i].resp[len]=0;
            restore_deadline(&previous);
        }
        explicit_bzero(ctx.io,sizeof(ctx.io));
    }
    block_deadline(&previous);
    *out=r; /* PAM now owns these allocations; see documented PAM-copy limit. */
    ctx.owned=NULL; ctx.owned_count=0;
    memset(ctx.owned_sizes,0,sizeof(ctx.owned_sizes));
    restore_deadline(&previous);
    return PAM_SUCCESS;
fail:
    free_responses(r,count>0 && count<=(int)S7_MESSAGES ? count : 0);
    explicit_bzero(ctx.io,sizeof(ctx.io)); return PAM_CONV_ERR;
}
/* The fixed lab policy has ONE password prompt. Collect it before NSS/PAM for
 * every selection, then answer one backend echo-off prompt privately. Backend
 * errors, early refusals and unsupported conversation shapes cannot change the
 * public transcript. General MFA needs a provisioned equivalent decoy policy;
 * never expose an account-dependent backend transcript as a fallback. */
static int conversation(int count, const struct pam_message **messages,
                        struct pam_response **out, void *data)
{
    struct pam_response *r;
    sigset_t previous;
    if (!ctx.normalized) return wire_conversation(count,messages,out,data);
    *out=NULL;
    if (!live() || ctx.answer_used || count!=1 || !messages || !messages[0] ||
        messages[0]->msg_style!=PAM_PROMPT_ECHO_OFF) return PAM_CONV_ERR;
    block_deadline(&previous);
    r=calloc(1,sizeof(*r));
    if (!r) { restore_deadline(&previous); return PAM_BUF_ERR; }
    ctx.owned=r; ctx.owned_count=1;
    r[0].resp=malloc(ctx.answer_len+1);
    if (!r[0].resp)
    {
        free(r); ctx.owned=NULL; ctx.owned_count=0;
        restore_deadline(&previous); return PAM_BUF_ERR;
    }
    ctx.owned_sizes[0]=ctx.answer_len+1;
    (void)mlock(r[0].resp,ctx.answer_len+1);
    memcpy(r[0].resp,ctx.answer,ctx.answer_len+1);
    explicit_bzero(ctx.answer,sizeof(ctx.answer)); ctx.answer_len=0;
    ctx.answer_used=1;
    *out=r; ctx.owned=NULL; ctx.owned_count=0; ctx.owned_sizes[0]=0;
    restore_deadline(&previous);
    return PAM_SUCCESS;
}
static int fixed_prompt(void)
{
    struct pam_message message={PAM_PROMPT_ECHO_OFF,"Password: "};
    const struct pam_message *messages[]={&message};
    struct pam_response *r=NULL;
    sigset_t previous;
    uint64_t now;
    if (wire_conversation(1,messages,&r,NULL)!=PAM_SUCCESS) return 0;
    block_deadline(&previous);
    /* Reclaim the callback-owned response immediately, including on timeout. */
    ctx.owned=r; ctx.owned_count=1; ctx.owned_sizes[0]=strlen(r[0].resp)+1;
    ctx.answer_len=ctx.owned_sizes[0]-1;
    memcpy(ctx.answer,r[0].resp,ctx.answer_len+1);
    free_responses(r,1);
    restore_deadline(&previous);
    now=now_ns();
    if (!now || now>=ctx.deadline || ctx.deadline-now<=S7_FAILURE_NS) return 0;
    ctx.release_at=now+S7_FAILURE_NS;
    return 1;
}
static int wait_release(void)
{
    struct pollfd p[3]={{ctx.dialog,POLLIN,0},{ctx.control,POLLIN,0},{ctx.peer_pin,POLLIN,0}};
    uint64_t now, left;
    int n;
    while ((now=now_ns()) && now<ctx.release_at)
    {
        if (!live()) return 0;
        left=(ctx.release_at-now+999999)/1000000;
        n=poll(p,3,(int)(left>50 ? 50 : left));
        if (n<0 && errno==EINTR) continue;
        if (n) { ctx.reason=RES_CANCEL; return 0; }
    }
    return now && quiet_dialog();
}
static int same_account(const char *name, const unsigned char *key, uint64_t uid, uint64_t generation)
{
    struct wex_account value;
    ctx.map_status=lookup_account(name,0,WEX_ACCOUNT_MAP_V1,&value);
    if (ctx.map_status || value.uid!=uid || value.generation!=generation || memcmp(value.incarnation,key,16))
    { ctx.reason=RES_MAP; return 0; }
    ctx.account=value; return 1;
}
static int pam_configuration(const char *directory, const char *service)
{
    enum wex_account_status status;
    char *text=NULL;
    size_t length;
    int dir, ok;
    if (!*service || strlen(service)>127 ||
        strspn(service,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=strlen(service)) return 0;
    if ((dir=protected_directory(directory,&status))<0) return 0;
    /* libpam may otherwise silently use its "other" service when this file
     * is missing. Require the exact provisioned protected stack to exist. */
    ok=read_protected(dir,service,(gid_t)-1,&text,&length)==WEX_ACCOUNT_OK;
    free(text); close(dir); return ok;
}
static int audit_record(int success)
{
    char line[256]; int len; struct stat st;
    /* No names, text, lengths, nonce or seed, even in privileged logs. */
    len=snprintf(line,sizeof(line),"E1S7 request=%016llx attempt=%u stage=%d reason=%u pam=%d map=%d rounds=%u locked=%d success=%d\n",
                 (unsigned long long)ctx.request,ctx.attempt,ctx.stage,(unsigned int)ctx.reason,
                 ctx.pam_status,ctx.map_status,ctx.round,ctx.locked,success);
    return !fstat(ctx.audit,&st) && st.st_size<16*1024*1024 && len>0 &&
           (size_t)len<sizeof(line) && write(ctx.audit,line,(size_t)len)==len && !fsync(ctx.audit);
}
static void hard_deadline(int sig)
{
    volatile unsigned char *p=ctx.io;
    size_t size=sizeof(ctx.io);
    struct pam_response *r=ctx.owned;
    int i, count=ctx.owned_count;
    (void)sig;
    while (size--) *p++=0;
    p=ctx.answer; size=sizeof(ctx.answer);
    while (size--) *p++=0;
    if (r && count>0 && count<=(int)S7_MESSAGES)
        for (i=0; i<count; i++) if (r[i].resp)
        {
            p=(volatile unsigned char *)r[i].resp; size=ctx.owned_sizes[i];
            while (size--) *p++=0;
        }
    if (ctx.audit>=0 && deadline_audit_len>0)
        (void)write(ctx.audit,deadline_audit,(size_t)deadline_audit_len);
    /* A slow NSS/PAM backend gets the same fixed failure at the release bound.
     * No success is possible here; this does not restart or retry PAM. */
    if (release_timeout)
        (void)send(ctx.control,timeout_result,sizeof(timeout_result),MSG_NOSIGNAL|MSG_DONTWAIT);
    _exit(1); /* PAM may be blocked outside a callback. Parent MUST waitpid. */
}
static int number(const char *s)
{
    uint64_t n;
    return parse_number(s,INT_MAX,&n) ? (int)n : -1;
}
int main(int argc, char **argv)
{
    struct rlimit core={0,0};
    struct stat st;
    struct pam_conv conv={conversation,NULL};
    struct sigaction action={0};
    struct sigevent event={0};
    sigset_t unblock;
    struct itimerspec timer={0};
    timer_t timer_id=(timer_t)0;
    pam_handle_t *pam=NULL;
    const void *final_user=NULL;
    const char *service=WEX_E1S7_PAM_SERVICE;
#ifdef WEX_E1S7_TEST
    const char *confdir=NULL;
#endif
    uint64_t uid=0,generation=0,now;
    unsigned int i, name_len, limit;
    ssize_t n;
    int success=0, one=1, executable_fd=-1, decoy=0;
    pid_t parent=getppid();
    char name[64]={0};
    memset(&ctx,0,sizeof(ctx)); ctx.control=ctx.dialog=ctx.audit=ctx.peer_pin=-1;
    ctx.peer_pid=ctx.peer_uid=-1;
    ctx.normalized=1;
    ctx.reason=RES_SETUP;
    if (getuid() || geteuid() || setrlimit(RLIMIT_CORE,&core) || prctl(PR_SET_DUMPABLE,0) ||
        prctl(PR_SET_PDEATHSIG,SIGKILL) || getppid()!=parent || parent==1) return 1;
    if (clearenv()) return 1;
    umask(0077);
    for (i=1; i<(unsigned int)argc; i++)
    {
        if (i+1>=(unsigned int)argc) return 1;
        if (!strcmp(argv[i],"--executable-fd")) executable_fd=number(argv[++i]);
        else if (!strcmp(argv[i],"--control-fd")) ctx.control=number(argv[++i]);
        else if (!strcmp(argv[i],"--dialog-fd")) ctx.dialog=number(argv[++i]);
        else if (!strcmp(argv[i],"--audit-fd")) ctx.audit=number(argv[++i]);
        else if (!strcmp(argv[i],"--peer-pin-fd")) ctx.peer_pin=number(argv[++i]);
        else if (!strcmp(argv[i],"--peer-pid")) ctx.peer_pid=number(argv[++i]);
        else if (!strcmp(argv[i],"--peer-uid")) ctx.peer_uid=number(argv[++i]);
#ifdef WEX_E1S7_TEST
        else if (!strcmp(argv[i],"--test-raw-conversation"))
        {
            if (strcmp(argv[++i],"1")) return 1;
            ctx.normalized=0;
        }
        else if (!strcmp(argv[i],"--test-service")) service=argv[++i];
        else if (!strcmp(argv[i],"--test-confdir")) confdir=argv[++i];
        else if (!strcmp(argv[i],"--test-account-dir")) account_directory=argv[++i];
#endif
        else return 1;
    }
    if (executable_fd<3 || executable_fd==ctx.control || executable_fd==ctx.dialog ||
        executable_fd==ctx.audit || executable_fd==ctx.peer_pin ||
        ctx.control<3 || ctx.dialog<3 || ctx.audit<3 || ctx.peer_pin<3 || ctx.peer_pid<=0 || ctx.peer_uid<0 ||
        ctx.control==ctx.dialog || ctx.audit==ctx.control || ctx.audit==ctx.dialog ||
        ctx.peer_pin==ctx.audit || ctx.peer_pin==ctx.control || ctx.peer_pin==ctx.dialog) return 1;
    /* The pinned ELF has already been executed; no PAM helper needs its fd.
     * pass_fds cleared CLOEXEC on it too. Close before any NSS/PAM handoff. */
    if (close(executable_fd)) return 1;
    /* pass_fds necessarily cleared CLOEXEC for our exec. Do not let a PAM
     * helper exec inherit this private transaction or the audit descriptor. */
    if (fcntl(ctx.control,F_SETFD,FD_CLOEXEC) || fcntl(ctx.dialog,F_SETFD,FD_CLOEXEC) ||
        fcntl(ctx.audit,F_SETFD,FD_CLOEXEC) || fcntl(ctx.peer_pin,F_SETFD,FD_CLOEXEC)) return 1;
    if (fstat(ctx.audit,&st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode&0777)!=0600 ||
        st.st_nlink!=1 || !no_access_acl(ctx.audit) || !(fcntl(ctx.audit,F_GETFL)&O_APPEND)) return 1;
    {
        struct ucred peer; socklen_t len=sizeof(peer); int type;
        if (getsockopt(ctx.control,SOL_SOCKET,SO_PEERCRED,&peer,&len) || len!=sizeof(peer) ||
            peer.uid || peer.pid!=parent) return 1;
        len=sizeof(type);
        if (getsockopt(ctx.control,SOL_SOCKET,SO_TYPE,&type,&len) || type!=SOCK_SEQPACKET) return 1;
        len=sizeof(type);
        if (getsockopt(ctx.dialog,SOL_SOCKET,SO_TYPE,&type,&len) || type!=SOCK_SEQPACKET) return 1;
    }
    /* S6 pins the process before passing these descriptors. Verify the pin's
     * PID as well, so an accidentally passed unrelated live pidfd denies. */
    {
        char path[64], line[128]; FILE *f; int pid=-1;
        snprintf(path,sizeof(path),"/proc/self/fdinfo/%d",ctx.peer_pin);
        if (!(f=fopen(path,"re"))) return 1;
        while (fgets(line,sizeof(line),f)) if (sscanf(line,"Pid: %d",&pid)==1) break;
        fclose(f); if (pid!=ctx.peer_pid || !pin_alive()) return 1;
    }
    if (setsockopt(ctx.dialog,SOL_SOCKET,SO_PASSCRED,&one,sizeof(one)) ||
        setsockopt(ctx.dialog,SOL_SOCKET,SO_PASSPIDFD,&one,sizeof(one))) return 1;
    ctx.locked=!mlock(ctx.io,sizeof(ctx.io)); /* Best effort, audited as 0/1. */
    (void)mlock(ctx.answer,sizeof(ctx.answer));
    /* A missing initial request is bounded too. Parent supplies no secrets in argv. */
    now=now_ns(); if (!now) goto done; ctx.deadline=now+S7_SECONDS*UINT64_C(1000000000);
    n=receive_packet(ctx.control,0); ctx.reason=RES_PROTOCOL;
    if (n<=(ssize_t)(S7_HEADER+48) || n>(ssize_t)(S7_HEADER+48+63)) goto done;
    ctx.request=le64(ctx.io+16); ctx.attempt=le32(ctx.io+24); memcpy(ctx.nonce,ctx.io+40,32);
    decoy=bound(S7_DECOY,n);
    if ((!bound(S7_START,n) && !decoy) || !ctx.request || !ctx.attempt || ctx.attempt>S7_ATTEMPTS ||
        le32(ctx.io+28) || le32(ctx.io+32) || le32(ctx.io+36) ||
        !memcmp(ctx.nonce,(unsigned char[32]){0},32)) goto done;
    memcpy(ctx.key,ctx.io+72,16); uid=le64(ctx.io+88); generation=le64(ctx.io+96);
    now=now_ns(); limit=le32(ctx.io+112); name_len=le32(ctx.io+116);
    if (!now || le64(ctx.io+104)<=now || le64(ctx.io+104)-now>S7_SECONDS*UINT64_C(1000000000) ||
        !limit || limit>S7_ATTEMPTS || ctx.attempt>limit || !name_len || name_len>63 ||
        (size_t)n!=S7_HEADER+48+name_len || uid>=UINT32_MAX || !generation ||
        (decoy ? (uid || memcmp(ctx.key,(unsigned char[16]){0},16)) :
                 (!uid || !memcmp(ctx.key,(unsigned char[16]){0},16)))) goto done;
    ctx.deadline=le64(ctx.io+104);
    memcpy(name,ctx.io+120,name_len);
    if (memchr(name,0,name_len) || !valid_name(name)) goto done;
    if (decoy && (strcmp(name,"decoy") || !ctx.normalized)) goto done;
    explicit_bzero(ctx.io,sizeof(ctx.io));
    /* Absolute monotonic timer bounds NSS, module delay, authenticate, account
     * management and pam_end, including modules that never call conversation. */
    action.sa_handler=hard_deadline; sigemptyset(&action.sa_mask);
    deadline_audit_len=snprintf(deadline_audit,sizeof(deadline_audit),
        "E1S7 request=%016llx attempt=%u event=hard-deadline success=0\n",
        (unsigned long long)ctx.request,ctx.attempt);
    if (deadline_audit_len<=0 || (size_t)deadline_audit_len>=sizeof(deadline_audit)) goto done;
    sigemptyset(&unblock); sigaddset(&unblock,SIGALRM);
    event.sigev_notify=SIGEV_SIGNAL; event.sigev_signo=SIGALRM;
    if (sigaction(SIGALRM,&action,NULL) || sigprocmask(SIG_UNBLOCK,&unblock,NULL) ||
        timer_create(CLOCK_MONOTONIC,&event,&timer_id)) goto done;
    timer.it_value.tv_sec=(time_t)(ctx.deadline/1000000000u);
    timer.it_value.tv_nsec=(long)(ctx.deadline%1000000000u);
    if (timer_settime(timer_id,TIMER_ABSTIME,&timer,NULL)) goto done;
    if (ctx.normalized && !fixed_prompt()) goto done;
    if (ctx.release_at)
    {
        header(S7_RESULT,S7_RESULT_SIZE); put32(ctx.io+72,1);
        memcpy(timeout_result,ctx.io,sizeof(timeout_result));
        memset(timeout_result+28,0,12);
        explicit_bzero(ctx.io,sizeof(ctx.io));
        timer.it_value.tv_sec=(time_t)(ctx.release_at/1000000000u);
        timer.it_value.tv_nsec=(long)(ctx.release_at%1000000000u);
        release_timeout=1;
        if (timer_settime(timer_id,TIMER_ABSTIME,&timer,NULL)) goto done;
    }
    ctx.stage=1;
    if (decoy) { ctx.reason=RES_MAP; goto done; }
    if (!same_account(name,ctx.key,uid,generation) || !quiet_dialog()) goto done;
    ctx.reason=RES_PAM; ctx.stage=2;
#ifdef WEX_E1S7_TEST
    if (!confdir || !pam_configuration(confdir,service)) { ctx.pam_status=PAM_SERVICE_ERR; goto done; }
    ctx.pam_status=pam_start_confdir(service,name,&conv,confdir,&pam);
#else
    if (!pam_configuration("/etc/pam.d",service)) { ctx.pam_status=PAM_SERVICE_ERR; goto done; }
    ctx.pam_status=pam_start(service,name,&conv,&pam);
#endif
    if (ctx.pam_status!=PAM_SUCCESS) goto done;
    ctx.stage=3; ctx.pam_status=pam_authenticate(pam,PAM_DISALLOW_NULL_AUTHTOK);
    if (ctx.pam_status!=PAM_SUCCESS || !quiet_dialog()) goto done;
    ctx.reason=RES_PAM; ctx.stage=4; ctx.pam_status=pam_acct_mgmt(pam,0);
    if (ctx.pam_status!=PAM_SUCCESS || !quiet_dialog()) goto done;
    ctx.reason=RES_USER; ctx.stage=5; ctx.pam_status=pam_get_item(pam,PAM_USER,&final_user);
    if (ctx.pam_status!=PAM_SUCCESS || !final_user || strcmp(final_user,name)) goto done;
    if (!same_account((const char *)final_user,ctx.key,uid,generation)) goto done;
    ctx.stage=6; ctx.pam_status=pam_end(pam,PAM_SUCCESS); pam=NULL;
    if (ctx.pam_status!=PAM_SUCCESS || !live() || (ctx.normalized && !ctx.answer_used)) goto done;
    /* Any queued extra answer/cancel/EOF wins over success. */
    { struct pollfd p={ctx.dialog,POLLIN,0}; if (poll(&p,1,0)!=0) { ctx.reason=RES_CANCEL; goto done; } }
    ctx.reason=RES_RNG; ctx.stage=7;
    explicit_bzero(ctx.io,sizeof(ctx.io));
    if (getrandom(ctx.io+S7_HEADER+36,32,0)!=32 || !memcmp(ctx.io+S7_HEADER+36,(unsigned char[32]){0},32)) goto done;
    success=1; ctx.reason=RES_OK;
done:
    if (pam) { int result=pam_end(pam,ctx.pam_status); if (result!=PAM_SUCCESS) success=0; }
    if (ctx.release_at)
    {
        sigset_t pending;
        block_deadline(&unblock);
        timer.it_value.tv_sec=(time_t)(ctx.deadline/1000000000u);
        timer.it_value.tv_nsec=(long)(ctx.deadline%1000000000u);
        if (timer_settime(timer_id,TIMER_ABSTIME,&timer,NULL)) _exit(1);
        /* A release alarm racing pam_end must still emit the fixed failure,
         * rather than becoming an original-deadline exit with no reply. */
        if (sigpending(&pending)) _exit(1);
        if (release_timeout && (sigismember(&pending,SIGALRM) || now_ns()>=ctx.release_at))
            hard_deadline(SIGALRM);
        release_timeout=0;
        restore_deadline(&unblock);
    }
    explicit_bzero(ctx.answer,sizeof(ctx.answer)); ctx.answer_len=0;
    /* Preserve PAM's own delay/lockout accounting; pad all ordinary outcomes,
     * including success, to the same post-answer release time. No sleep while
     * holding the service lock. Cancellation/death/deadline always wins. */
    if (ctx.release_at && !wait_release()) success=0;
    if (!audit_record(success)) { success=0; ctx.reason=RES_AUDIT; }
    if (!success) explicit_bzero(ctx.io,sizeof(ctx.io));
    ctx.round=ctx.index=ctx.style=0; header(S7_RESULT,S7_RESULT_SIZE);
    put32(ctx.io+72,success ? 0 : 1);
    if (success)
    { memcpy(ctx.io+76,ctx.key,16); put64(ctx.io+92,uid); put64(ctx.io+100,generation); }
    if (!live() || !send_packet(ctx.control,S7_RESULT_SIZE)) success=0;
    explicit_bzero(ctx.io,sizeof(ctx.io)); if (ctx.locked) munlock(ctx.io,sizeof(ctx.io));
    explicit_bzero(&ctx,sizeof(ctx)); explicit_bzero(name,sizeof(name));
    return success ? 0 : 1;
}
#endif
