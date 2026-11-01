/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Executed on Linux, ONLY shared pure contract/API-sequence logic. No Wine/PE. */
#include <assert.h>
#include <stdio.h>
#include "g4_native_exit_contract.h"

static wchar_t nonce[]=L"0123456789abcdef0123456789abcdef";
static wchar_t url[]=L"http://127.0.0.1:43210/0123456789abcdef0123456789abcdef/index.html#nonce=0123456789abcdef0123456789abcdef";
static wchar_t *args[]={L"native-exit.exe",L"--chromium",L"--image",WEX_EXIT_IMAGE,
    L"--profile",WEX_EXIT_OUTPUT L"\\profile",L"--log",WEX_EXIT_OUTPUT L"\\chromium.log",
    L"--events",WEX_EXIT_OUTPUT L"\\native-exit.jsonl",L"--nonce",nonce,L"--url",url};

static void schema(void) {
    struct wex_launch v;int i;
    assert(wex_validate(14,args,&v));assert(!v.calibration && v.wait_ms==45000);
#ifdef WEX_DIAGNOSTIC
    assert(v.count==15);
#if WEX_DIAGNOSTIC == 3
    assert(!wcscmp(v.child_args[8],L"--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3"));
#else
    assert(!wcscmp(v.child_args[8],L"--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2"));
#endif
#else
    assert(v.count==8);
#endif
    assert(!wcscmp(v.child_args[0],WEX_EXIT_IMAGE));
    assert(!wcscmp(v.child_args[1],L"--app=http://127.0.0.1:43210/0123456789abcdef0123456789abcdef/index.html#nonce=0123456789abcdef0123456789abcdef"));
    assert(!wcscmp(v.child_args[2],L"--user-data-dir=" WEX_EXIT_OUTPUT L"\\profile"));
    assert(!wcscmp(v.child_args[3],L"--no-first-run"));assert(!wcscmp(v.child_args[4],L"--no-default-browser-check"));
    assert(!wcscmp(v.child_args[5],L"--enable-logging"));
    assert(!wcscmp(v.child_args[6],L"--log-file=" WEX_EXIT_OUTPUT L"\\chromium.log"));
    assert(!wcscmp(v.child_args[7],L"--v=1"));
    /* Mutate EVERY mode/flag/path/profile/log/journal/nonce/URL independently. */
    for (i=1;i<14;i++) {
        wchar_t *saved=args[i];args[i]=L"wrong --no-sandbox";
        assert(!wex_validate(14,args,&v));args[i]=saved;
    }
    for (i=0;i<14;i++) assert(!wex_validate(i,args,&v));
    assert(!wex_validate(15,args,&v));
    {
        wchar_t *saved=args[3];args[3]=L"chrome.exe";assert(!wex_validate(14,args,&v));
        args[3]=L"C:\\foreign\\chrome.exe";assert(!wex_validate(14,args,&v));args[3]=saved;
    }
    {
        const wchar_t *bad[]={L"",L"a",L"0123456789abcdef0123456789abcdef\n",L"g123456789abcdef0123456789abcdef",
            L"http://foreign:80/",L"http://127.0.0.1:0/",L"http://127.0.0.1:65536/",L"http://127.0.0.1:043210/"};
        size_t n;
        for (n=0;n<sizeof(bad)/sizeof(*bad);n++) assert(!wex_url(bad[n],nonce));
        assert(!wex_url(url,L"ffffffffffffffffffffffffffffffff"));
        assert(!wex_url(url,L"0123456789abcdef0123456789abcdeF"));
    }
    {
        wchar_t *cal[]={L"native-exit.exe",L"--calibration",L"--image",WEX_EXIT_CHILD,
            L"--events",WEX_EXIT_CAL_OUTPUT L"\\native-exit.jsonl",L"--nonce",nonce};
        assert(wex_validate(8,cal,&v));assert(v.calibration && v.count==5 && v.wait_ms==5000);
        assert(!wcscmp(v.child_args[2],L"3221225599"));
        for (i=1;i<8;i++) { wchar_t *saved=cal[i];cal[i]=L"other";assert(!wex_validate(8,cal,&v));cal[i]=saved; }
    }
}

static void quoting(void) {
    const wchar_t *input[]={L"",L"plain",L"a b",L"a\"b",L"C:\\a b\\",L"a\\\"b"};
    const wchar_t *expected[]={L"\"\"",L"\"plain\"",L"\"a b\"",L"\"a\\\"b\"",L"\"C:\\a b\\\\\"",L"\"a\\\\\\\"b\""};
    size_t i;
    for (i=0;i<sizeof(input)/sizeof(*input);i++) {
        wchar_t out[128];size_t used=0;
        assert(wex_quote(out,128,&used,input[i]));assert(!wcscmp(out,expected[i]));
        used=0;assert(!wex_quote(out,wcslen(expected[i]),&used,input[i]));
    }
    {
        wchar_t command[4096];struct wex_launch v;
        assert(wex_validate(14,args,&v));assert(wex_command(command,4096,&v));
        assert(wcsstr(command,L"\"--no-first-run\" \"--no-default-browser-check\" \"--enable-logging\""));
#ifdef WEX_DIAGNOSTIC
        printf("COMMAND %ls\n",command);
#endif
        assert(!wcsstr(command,L"--no-sandbox"));assert(!wcsstr(command,L"--disable-"));
        assert(!wex_command(command,20,&v));assert(!wex_command(command,0,&v));
    }
}

struct fake { char calls[64];size_t used;char failed;int polls,race;uint32_t poll_failure,wait,status,observed;const char *phase; };
static void call(struct fake *f,char c) { assert(f->used<63);f->calls[f->used++]=c;f->calls[f->used]=0; }
static int step(void *p,char c) { struct fake *f=p;call(f,c);return f->failed!=c; }
static int create(void *p) { return step(p,'C'); }
static int close_thread(void *p) { return step(p,'T'); }
static int identity(void *p) { return step(p,'I'); }
static int bound(void *p) { return step(p,'B'); }
static int query(void *p) { return step(p,'Q'); }
static int exited(void *p) { struct fake *f=p;f->observed=f->status;return step(p,'X'); }
static int timeout(void *p) { return step(p,'O'); }
static uint32_t poll(void *p) { struct fake *f=p;call(f,'P');return ++f->polls==f->race?f->poll_failure:258; }
static uint32_t wait_child(void *p) { struct fake *f=p;call(f,'W');return f->wait; }
static int failure(void *p,const char *phase) { struct fake *f=p;f->phase=phase;call(f,'F');return 1; }
static int run(struct fake *f) {
    struct wex_ops o={f,create,close_thread,identity,bound,poll,wait_child,query,exited,timeout,failure};
    return wex_observe(&o);
}
static void decisions(void) {
    uint32_t codes[]={0,127,0xc000007fU,259,0xffffffffU};size_t i;
    for (i=0;i<sizeof(codes)/sizeof(*codes);i++) {
        struct fake f={0};char hex[11];f.status=codes[i];assert(!run(&f));
        assert(!strcmp(f.calls,"CTPIPBWQX"));assert(f.observed==codes[i]);
        wex_status_hex(codes[i],hex);
        if (codes[i]==127) assert(!strcmp(hex,"0x0000007f"));
        if (codes[i]==0xc000007fU) assert(!strcmp(hex,"0xc000007f"));
        if (codes[i]==259) assert(!strcmp(hex,"0x00000103"));
    }
    {
        const char stages[]={'C','T','I','B','Q','X'};
        const char *calls[]={"CF","CTF","CTPIF","CTPIPBF","CTPIPBWQF","CTPIPBWQXF"};
        const char *phases[]={"create","close_thread","identity_query","binding_flush","exit_query","exit_flush"};
        for (i=0;i<sizeof(stages);i++) {
            struct fake f={0};f.failed=stages[i];assert(run(&f)==1);
            assert(!strcmp(f.calls,calls[i]));assert(!strcmp(f.phase,phases[i]));
        }
    }
    for (i=1;i<=2;i++) {
        struct fake f={0};f.race=(int)i;assert(run(&f)==1);
        assert(!strcmp(f.phase,i==1?"identity_race_before":"identity_race_after"));
        assert(!strchr(f.calls,'B') && !strchr(f.calls,'W') && !strchr(f.calls,'Q'));
        memset(&f,0,sizeof(f));f.race=(int)i;f.poll_failure=0xffffffffU;
        assert(run(&f)==1);assert(!strchr(f.calls,'B') && !strchr(f.calls,'W'));
    }
    {
        struct fake f={0};f.wait=258;f.status=259;assert(run(&f)==1);
        assert(!strcmp(f.calls,"CTPIPBWO"));assert(!strchr(f.calls,'Q') && !strchr(f.calls,'X'));
        memset(&f,0,sizeof(f));f.wait=258;f.failed='O';assert(run(&f)==1);assert(!strcmp(f.phase,"timeout_flush"));
    }
    {
        uint32_t bad[]={0xffffffffU,0x80,259};
        for (i=0;i<sizeof(bad)/sizeof(*bad);i++) {
            struct fake f={0};f.wait=bad[i];assert(run(&f)==1);
            assert(!strcmp(f.calls,"CTPIPBWF"));assert(!strcmp(f.phase,"wait"));
        }
    }
}
int main(void) {
    schema();quoting();decisions();
    puts("PASS shared native schema/quoting/decision sequence: all failures, bound-before-wait, full32, signaled259 vs timeout");
    return 0;
}
