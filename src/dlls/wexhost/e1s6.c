/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * E1-S6 lab-only metadata transport, without credentials or launch authority.
 */
#if 0
#pragma makedep unix
#endif
#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winternl.h>
#include <winerror.h>
#include "wine/server.h"
#include "wexhost_unix.h"
#include "e1s6_protocol.h"

#if defined(WEX_E1S6_TRANSPORT_TEST) && defined(SONAME_LIBDBUS_1) && defined(__linux__)
#include <dlfcn.h>
#include <dbus/dbus.h>
#include <sys/prctl.h>

#define SO_PEERPIDFD_LAB 77
#define SO_PASSPIDFD_LAB 76
#define BUS_TIMEOUT 1000
#define DBUS_FUNCTIONS(X) \
    X(dbus_bus_get_private) X(dbus_bus_get_unique_name) \
    X(dbus_connection_set_exit_on_disconnect) X(dbus_connection_get_is_connected) \
    X(dbus_connection_close) X(dbus_connection_unref) \
    X(dbus_connection_send_with_reply_and_block) \
    X(dbus_error_init) X(dbus_error_free) \
    X(dbus_message_new_method_call) X(dbus_message_append_args) \
    X(dbus_message_get_args) X(dbus_message_get_sender) X(dbus_message_unref)
#define DECLARE(x) static __typeof__(x) *p_##x;
DBUS_FUNCTIONS(DECLARE)
#undef DECLARE

/* One conversation at a time per broker. S17/S18 must serialize prompts too.
 * IDs are monotonically allocated local references, never pointers or grants. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static DBusConnection *connection;
static char owner[256];
static UINT64 next_id, active_id;
static HANDLE bound_pipe;
static int data_fd = -1, endorse_fd = -1, pin_fd = -1, service_pin = -1;
static pid_t child_pid = -1;
static pid_t service_pid;
static unsigned char challenge[E1S6_ENDORSE_SIZE], broker_reg[E1S6_REG_SIZE];
static unsigned int phase;
static UINT64 sequence;
static NTSTATUS failure_status;
static int failure_errno;
static const char *failure_source;

/* Diagnostic memory only, never authority. The post-fork stub cannot retain a
 * logging FD; publish its error before _exit and read it only after waitpid. */
static struct child_error
{
    NTSTATUS status;
    int error;
} *child_error;

/* Only the private stub uses these exit codes. Keep every post-fork failure
 * distinguishable without retaining stderr or another inherited descriptor. */
enum child_failure
{
    CHILD_GID = 64, CHILD_PDEATHSIG, CHILD_PARENT, CHILD_DUMPABLE,
    CHILD_DUP, CHILD_CLOEXEC, CHILD_CLOSE_RANGE, CHILD_NO_CLOSE_RANGE,
    CHILD_NULL_OPEN, CHILD_NULL_DUP, CHILD_HELLO, CHILD_ANSWER_READY,
    CHILD_ANSWER_SIZE, CHILD_ECHO, CHILD_EOF_READY, CHILD_EOF
};

static void child_failed( unsigned int code, NTSTATUS status, int error )
{
    child_error->status = status;
    child_error->error = error;
    _exit( code );
}

static void audit_child_status( int status )
{
    static const char *const sites[] =
    {
        "child-drop-gid", "child-parent-death-signal", "child-parent-changed", "child-nondumpable",
        "child-dup-channel", "child-cloexec", "child-close-range", "child-close-range-unavailable",
        "child-null-open", "child-null-dup", "child-hello-send", "child-answer-ready",
        "child-answer-size", "child-echo-send", "child-eof-ready", "child-eof"
    };
    C_ASSERT( ARRAY_SIZE(sites) == CHILD_EOF - CHILD_GID + 1 );
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        fprintf( stderr, "E1S6_CHILD wexhost wait_status=%08x\n", (unsigned int)status );
    if (WIFEXITED(status) && WEXITSTATUS(status))
    {
        unsigned int code = WEXITSTATUS(status);
        int known = code >= CHILD_GID && code <= CHILD_EOF;
        fprintf( stderr, "E1S6_DENY wexhost site=%s child_exit=%u source=child status=%08x errno=%d\n",
                 known ? sites[code - CHILD_GID] : "child-exit-unknown", code,
                 (unsigned int)(known && child_error ? child_error->status : STATUS_UNSUCCESSFUL),
                 known && child_error ? child_error->error : 0 );
    }
}

/* Fixed site names only: no packet bytes or caller-supplied diagnostics. This
 * entire implementation, including its audit, is excluded from shipping. */
static int denied_error( const char *site, const char *source, NTSTATUS status, int error )
{
    failure_source = source;
    failure_status = status;
    failure_errno = error;
    fprintf( stderr, "E1S6_DENY wexhost site=%s phase=%u sequence=%llu source=%s status=%08x errno=%d\n",
             site, phase, (unsigned long long)sequence, source, (unsigned int)status, error );
    return 0;
}

/* Only source=ntstatus is a returned NTSTATUS. Checks have a local denial
 * status; errno sources have a local unsuccessful status and the captured
 * syscall errno. Never use ambient errno for a successful syscall/mismatch. */
static int denied( const char *site )
{
    return denied_error( site, "check", STATUS_ACCESS_DENIED, 0 );
}

static int denied_errno( const char *site )
{
    return denied_error( site, "errno", STATUS_UNSUCCESSFUL, errno );
}

static int denied_again( const char *site )
{
    return denied_error( site, failure_source, failure_status, failure_errno );
}

static int denied_bus( const char *site, DBusError *error )
{
    /* Error names are D-Bus identifiers; do not print arbitrary reply text. */
    fprintf( stderr, "E1S6_DBUS wexhost site=%s name=%.128s\n", site, error->name ? error->name : "none" );
    return denied_error( site, "dbus", STATUS_UNSUCCESSFUL, 0 );
}

#define REQUIRE(c, site) do { if (!(c)) { denied( site ); goto done; } } while (0)
#define REQUIRE_ERRNO(c, site) do { if (!(c)) { denied_errno( site ); goto done; } } while (0)
#define REQUIRE_OK(c, site) do { if (!(c)) { denied_again( site ); goto done; } } while (0)

/* Short/EOF/timeouts are not errno failures. Return values remain auditable. */
static int sent( ssize_t result, unsigned int size, const char *site )
{
    if (result < 0) return denied_errno( site );
    if (result == size) return 1;
    fprintf( stderr, "E1S6_IO wexhost site=%s result=%lld expected=%u\n", site, (long long)result, size );
    return denied_error( site, "short-send", STATUS_UNSUCCESSFUL, 0 );
}

static int ready( int fd, int timeout, int quiet, const char *site )
{
    struct pollfd p = { fd, POLLIN, 0 };
    int ret = poll( &p, 1, timeout );
    if (ret < 0) return denied_errno( site );
    if (quiet ? !ret : ret == 1 && p.revents == POLLIN) return 1;
    fprintf( stderr, "E1S6_POLL wexhost site=%s result=%d revents=%x\n", site, ret, p.revents );
    return denied_error( site, "poll-state", !ret ? STATUS_TIMEOUT : STATUS_ACCESS_DENIED, 0 );
}

static int load_bus(void)
{
    static void *module;
    if (!module && !(module = dlopen( SONAME_LIBDBUS_1, RTLD_NOW ))) return denied( "load-bus-library" );
#define LOAD(x) if (!(p_##x = dlsym( module, #x ))) return denied( "load-" #x );
    DBUS_FUNCTIONS(LOAD)
#undef LOAD
    return 1;
}

static DBusMessage *bus_reply( DBusMessage *msg, const char *site )
{
    DBusError error;
    DBusMessage *reply;
    p_dbus_error_init( &error );
    reply = p_dbus_connection_send_with_reply_and_block( connection, msg, BUS_TIMEOUT, &error );
    if (!reply) denied_bus( site, &error );
    p_dbus_error_free( &error );
    return reply;
}

static DBusMessage *call( const char *dest, const char *path, const char *interface,
                          const char *method, int type, const void *value )
{
    DBusMessage *msg, *reply;
    if (!(msg = p_dbus_message_new_method_call( dest, path, interface, method )))
    { denied_error( "bus-call-allocation", "allocation", STATUS_NO_MEMORY, 0 ); return NULL; }
    if (type != DBUS_TYPE_INVALID && !p_dbus_message_append_args( msg, type, value, DBUS_TYPE_INVALID ))
    { denied_error( "bus-call-arguments", "allocation", STATUS_NO_MEMORY, 0 ); p_dbus_message_unref( msg ); return NULL; }
    reply = bus_reply( msg, "bus-call-reply" );
    p_dbus_message_unref( msg );
    return reply;
}

static int bus_identity( char *unique, unsigned int *pid, unsigned int *uid )
{
    static const char *const dest = DBUS_SERVICE_DBUS, *const path = DBUS_PATH_DBUS;
    const char *name = E1S6_BUS, *value;
    DBusMessage *reply;
    DBusError error;
    int ok = 0;
    if (!(reply = call( dest, path, dest, "GetNameOwner", DBUS_TYPE_STRING, &name ))) return denied_again( "bus-get-owner" );
    p_dbus_error_init( &error );
    ok = p_dbus_message_get_args( reply, &error, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID );
    if (!ok) denied_bus( "bus-owner-reply", &error );
    else if (value[0] != ':' || strlen( value ) >= 256) ok = denied( "bus-owner-reply" );
    else strcpy( unique, value );
    p_dbus_error_free( &error );
    p_dbus_message_unref( reply );
    if (!ok) return 0;
    name = unique;
    if (!(reply = call( dest, path, dest, "GetConnectionUnixProcessID", DBUS_TYPE_STRING, &name )))
        return denied_again( "bus-get-pid" );
    p_dbus_error_init( &error );
    ok = p_dbus_message_get_args( reply, &error, DBUS_TYPE_UINT32, pid, DBUS_TYPE_INVALID );
    if (!ok) denied_bus( "bus-pid-reply", &error );
    p_dbus_error_free( &error );
    p_dbus_message_unref( reply );
    if (!ok) return 0;
    if (!(reply = call( dest, path, dest, "GetConnectionUnixUser", DBUS_TYPE_STRING, &name )))
        return denied_again( "bus-get-uid" );
    p_dbus_error_init( &error );
    ok = p_dbus_message_get_args( reply, &error, DBUS_TYPE_UINT32, uid, DBUS_TYPE_INVALID );
    if (!ok) denied_bus( "bus-uid-reply", &error );
    p_dbus_error_free( &error );
    p_dbus_message_unref( reply );
    if (!ok) return 0;
    if (!*pid || *uid) return denied( "bus-root-identity" );
    return 1;
}

static int live(void)
{
    char unique[256];
    unsigned int pid, uid;
    if (!connection || !p_dbus_connection_get_is_connected( connection )) return denied( "live-bus" );
    if (service_pin < 0) return denied( "live-service-pin" );
    if (!ready( service_pin, 0, 1, "live-service-pin" )) return 0;
    if (!bus_identity( unique, &pid, &uid )) return 0;
    if (strcmp( unique, owner )) return denied( "live-owner" );
    return 1;
}

static void cleanup(void)
{
    int *fds[] = { &data_fd, &endorse_fd, &pin_fd, &service_pin };
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(fds); i++) if (*fds[i] >= 0) { close( *fds[i] ); *fds[i] = -1; }
    if (connection) { p_dbus_connection_close( connection ); p_dbus_connection_unref( connection ); connection = NULL; }
    if (child_pid > 0)
    {
        int status;
        pid_t ret;
        /* Unreaped owned child: this PID cannot be recycled. Kill and reap even
         * on timeout/partial setup. No global process or prefix cleanup. */
        kill( child_pid, SIGKILL );
        do ret = waitpid( child_pid, &status, 0 ); while (ret < 0 && errno == EINTR);
        if (ret == child_pid) audit_child_status( status );
        else denied_errno( "cleanup-child-wait" );
        child_pid = -1;
    }
    if (child_error) { munmap( child_error, sizeof(*child_error) ); child_error = NULL; }
    active_id = phase = sequence = 0;
    memset( challenge, 0, sizeof(challenge) );
    memset( broker_reg, 0, sizeof(broker_reg) );
}

static int receive_packet( unsigned char *buffer, unsigned int size )
{
    struct iovec io = { buffer, size };
    char ancillary[256];
    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    struct ucred cred;
    int have_cred = 0, have_pin = 0, valid = 1;
    ssize_t n;
    /* Record sizes are bounded before receive. MSG_TRUNC reports excess bytes. */
    if (!live()) return 0;
    if (!ready( data_fd, BUS_TIMEOUT, 0, "receive-ready" )) return 0;
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = ancillary; msg.msg_controllen = sizeof(ancillary);
    n = recvmsg( data_fd, &msg, MSG_TRUNC | MSG_CMSG_CLOEXEC );
    if (n < 0) return denied_errno( "receive-packet" );
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
        if (cmsg->cmsg_level != SOL_SOCKET) { denied( "receive-cmsg-level" ); valid = 0; continue; }
        if (cmsg->cmsg_type == SCM_CREDENTIALS && cmsg->cmsg_len == CMSG_LEN(sizeof(cred)))
        {
            memcpy( &cred, CMSG_DATA(cmsg), sizeof(cred) );
            if (have_cred++ || cred.pid != service_pid || cred.uid)
            { denied( "receive-credentials" ); valid = 0; }
        }
        else if (cmsg->cmsg_type == 4 || cmsg->cmsg_type == SCM_RIGHTS) /* SCM_PIDFD = 4 */
        {
            size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int), i;
            int *fds = (int *)CMSG_DATA(cmsg);
            if (cmsg->cmsg_type != 4 || count != 1 || have_pin++)
            { denied( "receive-pidfd-shape" ); valid = 0; }
            for (i = 0; i < count; i++)
            {
                if (fds[i] < 0) { denied( "receive-pidfd-live" ); valid = 0; }
                else if (!ready( fds[i], 0, 1, "receive-pidfd-live" )) valid = 0;
                if (fds[i] >= 0) close( fds[i] );
            }
        }
        else { denied( "receive-cmsg-type" ); valid = 0; }
    }
    if (!valid) return 0;
    if (have_cred != 1 || have_pin != 1) return denied( "receive-missing-identity" );
    if (n != size || (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return denied( "receive-size" );
    return live();
}

static int endorse( unsigned int operation, pid_t child )
{
    HANDLE fd_handle = 0, pin_handle = 0, data_handle = 0;
    unsigned char data[E1S6_ENDORSE_SIZE];
    NTSTATUS status;
    int ok = 0;
    REQUIRE_OK( live(), "endorse-live" );
    /* alloc_handle rejects mapped access == 0. These handles only carry FDs
     * for metadata inspection; retain normal DACL checks and no inheritance. */
    if ((status = wine_server_fd_to_handle( endorse_fd, FILE_READ_ATTRIBUTES, 0, &fd_handle )))
    { denied_error( "endorse-import-control", "ntstatus", status, 0 ); goto done; }
    if ((status = wine_server_fd_to_handle( pin_fd, FILE_READ_ATTRIBUTES, 0, &pin_handle )))
    { denied_error( "endorse-import-pin", "ntstatus", status, 0 ); goto done; }
    if ((status = wine_server_fd_to_handle( data_fd, FILE_READ_ATTRIBUTES, 0, &data_handle )))
    { denied_error( "endorse-import-data", "ntstatus", status, 0 ); goto done; }
    memcpy( data, challenge, sizeof(data) );
    e1s6_put32( data, wine_server_obj_handle( fd_handle ) );
    e1s6_put32( data + 4, wine_server_obj_handle( pin_handle ) );
    /* Formerly reserved input; overwritten by broker PID/session in output. */
    e1s6_put32( data + 16, wine_server_obj_handle( data_handle ) );
    e1s6_put64( data + 72, sequence );
    e1s6_put32( data + 80, operation ); e1s6_put32( data + 84, child );
    SERVER_START_REQ( get_pipe_caller_binding )
    {
        req->pipe = wine_server_obj_handle( bound_pipe );
        wine_server_add_data( req, data, sizeof(data) );
        status = wine_server_call( req );
        if (!status)
        {
            NtClose( wine_server_ptr_handle( reply->process ) );
            NtClose( wine_server_ptr_handle( reply->token ) );
            NtClose( wine_server_ptr_handle( reply->writer_process ) );
            NtClose( wine_server_ptr_handle( reply->writer_token ) );
        }
    }
    SERVER_END_REQ;
    if (status) denied_error( "endorse-server", "ntstatus", status, 0 );
    else { sequence++; ok = 1; }
done:
    if (data_handle) NtClose( data_handle );
    if (fd_handle) NtClose( fd_handle );
    if (pin_handle) NtClose( pin_handle );
    return ok;
}

static int nonzero( const unsigned char *bytes, unsigned int size )
{
    unsigned int i;
    for (i = 0; i < size; i++) if (bytes[i]) return 1;
    return 0;
}

static int valid_reg( const unsigned char *r, unsigned int role )
{
    return e1s6_u32(r) == 0x10007 && e1s6_u32(r+4) == E1S6_REG_SIZE && !e1s6_u64(r+8) &&
           e1s6_u32(r+16) == role && !e1s6_u32(r+20) && nonzero(r+24, 16) &&
           e1s6_u64(r+40) && e1s6_u64(r+48) && e1s6_u64(r+56) && e1s6_u64(r+64) &&
           nonzero(r+72, 16) && nonzero(r+88, 16) && !e1s6_u32(r+108);
}

static int open_channel( HANDLE pipe, unsigned char *output )
{
    DBusMessage *msg = NULL, *reply = NULL;
    const char *sender;
    unsigned char *bytes;
    unsigned int pid, uid, again_pid, again_uid, version = 1;
    dbus_uint64_t caps = 0;
    struct ucred cred;
    socklen_t len;
    char again[256];
    int pair[2] = { -1, -1 }, count, parsed, control = -1, remote = -1, ok = 0, one = 1;
    int control_pin = -1;
    DBusError error;

    REQUIRE_OK( load_bus(), "open-load-bus" );
    p_dbus_error_init( &error );
    connection = p_dbus_bus_get_private( DBUS_BUS_SYSTEM, &error );
    if (!connection) denied_bus( "open-connect-bus", &error );
    p_dbus_error_free( &error );
    if (!connection) goto done;
    p_dbus_connection_set_exit_on_disconnect( connection, FALSE );
    REQUIRE_OK( bus_identity( owner, &pid, &uid ), "open-bus-identity" );
    REQUIRE_ERRNO( !socketpair( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair ), "open-pin-pair" );
    pin_fd = pair[0]; pair[0] = -1;
    REQUIRE( (msg = p_dbus_message_new_method_call( owner, E1S6_PATH, E1S6_BUS, "Open" )), "open-message" );
    REQUIRE( p_dbus_message_append_args( msg, DBUS_TYPE_UNIX_FD, &pair[1], DBUS_TYPE_UINT32, &version,
             DBUS_TYPE_UINT32, &version, DBUS_TYPE_UINT64, &caps, DBUS_TYPE_INVALID ), "open-arguments" );
    if (!(reply = bus_reply( msg, "open-bus-reply" ))) goto done;
    sender = p_dbus_message_get_sender( reply );
    REQUIRE( sender && !strcmp( sender, owner ), "open-reply-owner" );
    p_dbus_error_init( &error );
    parsed = p_dbus_message_get_args( reply, &error,
        DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &bytes, &count,
        DBUS_TYPE_UNIX_FD, &control, DBUS_TYPE_UNIX_FD, &remote, DBUS_TYPE_INVALID );
    if (!parsed) denied_bus( "open-reply-shape", &error );
    p_dbus_error_free( &error );
    if (!parsed) goto done;
    REQUIRE( count == sizeof(challenge), "open-challenge-size" );
    memcpy( challenge, bytes, sizeof(challenge) );
    endorse_fd = control; control = -1; data_fd = remote; remote = -1;
    len = sizeof(cred);
    REQUIRE_ERRNO( !getsockopt( data_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len ), "open-peercred" );
    REQUIRE( len == sizeof(cred), "open-peercred-size" );
    REQUIRE( cred.pid == pid && cred.uid == uid, "open-peer-identity" );
    service_pid = pid;
    REQUIRE_ERRNO( !setsockopt( data_fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one) ), "open-passcred" );
    REQUIRE_ERRNO( !setsockopt( data_fd, SOL_SOCKET, SO_PASSPIDFD_LAB, &one, sizeof(one) ), "open-passpidfd" );
    len = sizeof(service_pin);
    REQUIRE_ERRNO( !getsockopt( data_fd, SOL_SOCKET, SO_PEERPIDFD_LAB, &service_pin, &len ), "open-peerpidfd" );
    REQUIRE( len == sizeof(service_pin), "open-peerpidfd-size" );
    len = sizeof(cred);
    REQUIRE_ERRNO( !getsockopt( endorse_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len ), "open-control-peercred" );
    REQUIRE( len == sizeof(cred) && cred.pid == pid && cred.uid == uid && !uid,
             "open-control-peer-identity" );
    len = sizeof(control_pin);
    REQUIRE_ERRNO( !getsockopt( endorse_fd, SOL_SOCKET, SO_PEERPIDFD_LAB, &control_pin, &len ), "open-control-peerpidfd" );
    REQUIRE( len == sizeof(control_pin) && control_pin >= 0 && service_pin >= 0, "open-control-peerpidfd-size" );
    /* Equal kernel PIDs with both acquired pins still live identify the same
     * process, including on kernels where pidfds share an anon_inode inode. */
    REQUIRE_OK( ready( control_pin, 0, 1, "open-control-pin-live" ), "open-control-pin-live" );
    REQUIRE_OK( ready( service_pin, 0, 1, "open-data-pin-live" ), "open-data-pin-live" );
    REQUIRE_OK( bus_identity( again, &again_pid, &again_uid ), "open-owner-identity" );
    REQUIRE( !strcmp( again, owner ) &&
             pid == again_pid && uid == again_uid, "open-owner-recheck" );
    bound_pipe = pipe;
    REQUIRE_OK( endorse( E1S6_OPEN, 0 ), "open-endorsement" );
    if (!sent( send( data_fd, "E1S6OPEN", 8, MSG_NOSIGNAL ), 8, "open-send" )) goto done;
    REQUIRE_OK( receive_packet( output, E1S6_OPEN_SIZE ), "open-receive" );
    REQUIRE( valid_reg(output, 1), "open-broker-registration" );
    REQUIRE( valid_reg(output + E1S6_REG_SIZE, 2), "open-verifier-registration" );
    REQUIRE( !memcmp(output + 40, output + E1S6_REG_SIZE + 40, 32), "open-registration-generations" );
    REQUIRE( !memcmp(output + 88, output + E1S6_REG_SIZE + 88, 24), "open-registration-channel-session" );
    REQUIRE( e1s6_u64(output + 40) == e1s6_u64(challenge + 24), "open-root-generation" );
    REQUIRE( e1s6_u64(output + 64) == e1s6_u64(challenge + 32), "open-verifier-generation" );
    /* Re-endorsements name this server-issued generation, never choose it. */
    e1s6_put64( challenge + 8, e1s6_u64( output + 56 ) );
    memcpy( broker_reg, output, E1S6_REG_SIZE );
    phase = 1; ok = 1;
done:
    if (msg) p_dbus_message_unref( msg );
    if (reply) p_dbus_message_unref( reply );
    if (pair[0] >= 0) close( pair[0] );
    if (pair[1] >= 0) close( pair[1] );
    if (control >= 0) close( control );
    if (remote >= 0) close( remote );
    if (control_pin >= 0) close( control_pin );
    return ok;
}

static int start_child( unsigned char *buffer, int wrong_child )
{
    unsigned char packet[E1S6_REG_SIZE + E1S6_ANSWER_SIZE + 4];
    struct iovec io = { packet, sizeof(packet) };
    char ancillary[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = { 0 };
    struct cmsghdr *cmsg;
    int pair[2], one = 1, ok = 0;
    pid_t endorsed_child;
    pid_t parent = getpid();
    if (phase != 1) return denied( "child-phase" );
    if (socketpair( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair )) return denied_errno( "child-pair" );
    /* Before fork/hello, so the first child message has credentials + pidfd. */
    REQUIRE_ERRNO( !setsockopt( pair[0], SOL_SOCKET, SO_PASSCRED, &one, sizeof(one) ), "child-passcred" );
    REQUIRE_ERRNO( !setsockopt( pair[0], SOL_SOCKET, SO_PASSPIDFD_LAB, &one, sizeof(one) ), "child-passpidfd" );
    child_error = mmap( NULL, sizeof(*child_error), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );
    if (child_error == MAP_FAILED)
    { child_error = NULL; denied_errno( "child-audit-memory" ); goto done; }
    if ((child_pid = fork()) == 0)
    {
        int fd, nullfd, ret;
        ssize_t n;
        unsigned char record[E1S6_ANSWER_SIZE];
        struct pollfd p = { 3, POLLIN, 0 };
        /* Native metadata stub only. Remain non-dumpable: exec of an ordinary
         * interpreter would reset the flag. After a multithreaded fork use only
         * kernel/async-signal-safe operations; never Wine, malloc or D-Bus. */
        if (setresgid( getgid(), getgid(), getgid() )) child_failed( CHILD_GID, STATUS_UNSUCCESSFUL, errno );
        if (prctl( PR_SET_PDEATHSIG, SIGKILL )) child_failed( CHILD_PDEATHSIG, STATUS_UNSUCCESSFUL, errno );
        if (getppid() != parent) child_failed( CHILD_PARENT, STATUS_ACCESS_DENIED, 0 );
        if (prctl( PR_SET_DUMPABLE, 0 )) child_failed( CHILD_DUMPABLE, STATUS_UNSUCCESSFUL, errno );
        if (dup2( pair[1], 3 ) < 0) child_failed( CHILD_DUP, STATUS_UNSUCCESSFUL, errno );
        if (fcntl( 3, F_SETFD, FD_CLOEXEC ) < 0) child_failed( CHILD_CLOEXEC, STATUS_UNSUCCESSFUL, errno );
#ifdef SYS_close_range
        if (syscall( SYS_close_range, 4, ~0U, 0 )) child_failed( CHILD_CLOSE_RANGE, STATUS_UNSUCCESSFUL, errno );
#else
        child_failed( CHILD_NO_CLOSE_RANGE, STATUS_NOT_SUPPORTED, 0 );
#endif
        if ((nullfd = open( "/dev/null", O_RDWR )) < 0) child_failed( CHILD_NULL_OPEN, STATUS_UNSUCCESSFUL, errno );
        for (fd = 0; fd < 3; fd++)
            if (dup2( nullfd, fd ) < 0) child_failed( CHILD_NULL_DUP, STATUS_UNSUCCESSFUL, errno );
        if (nullfd > 3) close( nullfd );
        n = send( 3, "E1S6CHLD", 8, MSG_NOSIGNAL );
        if (n != 8) child_failed( CHILD_HELLO, STATUS_UNSUCCESSFUL, n < 0 ? errno : 0 );
        ret = poll( &p, 1, 5000 );
        if (ret != 1) child_failed( CHILD_ANSWER_READY, !ret ? STATUS_TIMEOUT : STATUS_UNSUCCESSFUL, ret < 0 ? errno : 0 );
        n = recv( 3, record, sizeof(record), MSG_TRUNC );
        if (!n) { close( 3 ); _exit( 0 ); } /* verifier EOF cancels before an exchange */
        if (n != (ssize_t)sizeof(record)) child_failed( CHILD_ANSWER_SIZE, STATUS_UNSUCCESSFUL, n < 0 ? errno : 0 );
        n = send( 3, record, sizeof(record), MSG_NOSIGNAL );
        if (n != sizeof(record)) child_failed( CHILD_ECHO, STATUS_UNSUCCESSFUL, n < 0 ? errno : 0 );
        ret = poll( &p, 1, 5000 );
        if (ret != 1) child_failed( CHILD_EOF_READY, !ret ? STATUS_TIMEOUT : STATUS_UNSUCCESSFUL, ret < 0 ? errno : 0 );
        n = recv( 3, record, 1, 0 );
        if (n) child_failed( CHILD_EOF, STATUS_UNSUCCESSFUL, n < 0 ? errno : 0 );
        close( 3 ); _exit( 0 );
    }
    REQUIRE_ERRNO( child_pid > 0, "child-fork" );
    endorsed_child = wrong_child ? getpid() : child_pid;
    REQUIRE_OK( endorse( E1S6_CHILD_START, endorsed_child ), "child-endorsement" );
    memcpy( packet, broker_reg, E1S6_REG_SIZE );
    memcpy( packet + E1S6_REG_SIZE, buffer + E1S6_REG_SIZE, E1S6_ANSWER_SIZE );
    e1s6_put32( packet + E1S6_REG_SIZE + E1S6_ANSWER_SIZE, endorsed_child );
    msg.msg_iov = &io; msg.msg_iovlen = 1;
    msg.msg_control = ancillary; msg.msg_controllen = sizeof(ancillary);
    cmsg = CMSG_FIRSTHDR( &msg ); cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int)); memcpy( CMSG_DATA(cmsg), &pair[0], sizeof(int) );
    if (!sent( sendmsg( data_fd, &msg, MSG_NOSIGNAL ), sizeof(packet), "child-send" )) goto done;
    REQUIRE_OK( receive_packet( buffer, E1S6_REG_SIZE ), "child-receive" );
    REQUIRE( valid_reg(buffer, 3), "child-registration" );
    REQUIRE( !memcmp(buffer + 40, broker_reg + 40, 32), "child-registration-generations" );
    REQUIRE( !memcmp(buffer + 88, broker_reg + 88, 24), "child-registration-channel-session" );
    phase = 2; ok = 1;
done:
    close( pair[0] ); close( pair[1] );
    return ok;
}
#endif

NTSTATUS e1s6_transport( void *args )
{
    struct e1s6_transport_params *params = args;
    params->err = ERROR_NOT_SUPPORTED;
#if defined(WEX_E1S6_TRANSPORT_TEST) && defined(SONAME_LIBDBUS_1) && defined(__linux__)
    {
        unsigned char *buffer = (void *)(ULONG_PTR)params->buffer;
        int ok = 0;
        pthread_mutex_lock( &lock );
        failure_source = "check";
        failure_status = STATUS_ACCESS_DENIED;
        failure_errno = 0;
        if (params->operation == E1S6_CLOSE && params->channel == active_id && active_id)
        { cleanup(); params->channel = 0; params->err = 0; }
        else if (params->operation == E1S6_OPEN && !params->channel && buffer && params->size == E1S6_OPEN_SIZE)
        {
            cleanup();
            if (next_id == ~(UINT64)0) denied( "channel-id-exhausted" );
            if (next_id != ~(UINT64)0 && open_channel( (HANDLE)(ULONG_PTR)params->pipe, buffer ))
            { params->channel = active_id = ++next_id; params->err = 0; }
            else { cleanup(); params->err = ERROR_ACCESS_DENIED; }
        }
        else if (params->channel && params->channel == active_id && buffer &&
                 params->pipe == (ULONG_PTR)bound_pipe && params->size == E1S6_REG_SIZE + E1S6_ANSWER_SIZE)
        {
            if (params->operation == E1S6_CHILD_START || params->operation == E1S6_TEST_CHILD_IMPOSTOR)
                ok = start_child( buffer, params->operation == E1S6_TEST_CHILD_IMPOSTOR );
            else if (params->operation == E1S6_TEST_KILL_CHILD && child_pid > 0)
            {
                pid_t ret;
                int status;
                if (kill( child_pid, SIGKILL )) denied_errno( "inject-child-kill" );
                else
                {
                    do ret = waitpid( child_pid, &status, 0 ); while (ret < 0 && errno == EINTR);
                    if (ret < 0) denied_errno( "inject-child-wait" );
                    else
                    {
                        child_pid = -1;
                        ok = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
                        if (!ok) { audit_child_status( status ); denied( "inject-child-status" ); }
                    }
                }
            }
            else if (params->operation == E1S6_TEST_WRONG_OWNER)
            {
                const char *unique = p_dbus_bus_get_unique_name( connection );
                if (unique && strlen(unique) < sizeof(owner)) { strcpy( owner, unique ); ok = 1; }
                else denied( "inject-owner-name" );
            }
            else if (params->operation == E1S6_TEST_DISCONNECT && child_pid > 0)
            {
                unsigned int i;
                int status;
                pid_t ret;
                close( data_fd ); data_fd = -1;
                /* Prove that verifier EOF cancellation closes the child channel:
                 * observe natural child exit BEFORE cleanup's kill fallback. */
                for (i = 0; i < 100; i++)
                {
                    ret = waitpid( child_pid, &status, WNOHANG );
                    if (ret < 0)
                    {
                        if (errno == EINTR) continue;
                        denied_errno( "disconnect-child-wait" );
                        break;
                    }
                    if (ret == child_pid)
                    {
                        child_pid = -1;
                        ok = WIFEXITED(status) && !WEXITSTATUS(status);
                        if (!ok)
                        {
                            audit_child_status( status );
                            if (WIFEXITED(status) && WEXITSTATUS(status) >= CHILD_GID &&
                                WEXITSTATUS(status) <= CHILD_EOF && child_error)
                                denied_error( "disconnect-child-status", "child", child_error->status, child_error->error );
                            else denied( "disconnect-child-status" );
                        }
                        break;
                    }
                    poll( NULL, 0, 10 );
                }
                if (i == 100) denied_error( "disconnect-child-timeout", "timeout", STATUS_TIMEOUT, 0 );
                cleanup(); params->channel = 0;
            }
            else if (params->operation == E1S6_EXCHANGE && (phase == 2 || phase == 3))
            {
                unsigned char expected[E1S6_ANSWER_SIZE];
                memcpy( expected, buffer + E1S6_REG_SIZE, sizeof(expected) );
                if (!endorse( E1S6_EXCHANGE, 0 )) denied_again( "exchange-endorsement" );
                else if (sent( send( data_fd, buffer, params->size, MSG_NOSIGNAL ), params->size, "exchange-send" ))
                {
                    if (!receive_packet( buffer + E1S6_REG_SIZE, E1S6_ANSWER_SIZE )) denied_again( "exchange-receive" );
                    else if (memcmp( expected, buffer + E1S6_REG_SIZE, sizeof(expected) )) denied( "exchange-echo" );
                    else ok = 1;
                }
                if (ok) phase = 3;
            }
            else denied( "operation-phase" );
            if (ok) params->err = 0;
            else { cleanup(); params->channel = 0; params->err = ERROR_ACCESS_DENIED; }
        }
        else if (params->channel && params->channel == active_id && buffer &&
                 params->pipe == (ULONG_PTR)bound_pipe && params->size == E1S6_REDEEM_REQ_SIZE)
        {
            if (params->operation == E1S6_REDEEM && phase >= 1)
            {
                if (!endorse( 4, 0 )) denied_again( "redeem-endorsement" );
                else if (sent( send( data_fd, buffer, params->size, MSG_NOSIGNAL ), params->size, "redeem-send" ))
                {
                    if (!receive_packet( buffer, E1S6_REDEEM_RESP_SIZE )) denied_again( "redeem-receive" );
                    else if (e1s6_u32( buffer ) != 0) denied( "redeem-status" );
                    else ok = 1;
                }
            }
            else denied( "operation-phase" );
            if (ok) params->err = 0;
            else { cleanup(); params->channel = 0; params->err = ERROR_ACCESS_DENIED; }
        }
        else if (params->channel && params->channel == active_id && buffer &&
                 params->pipe == (ULONG_PTR)bound_pipe && params->size == E1S6_STORAGE_QUERY_REQ_SIZE)
        {
            if (params->operation == E1S6_STORAGE_QUERY && phase >= 1)
            {
                if (!endorse( 6, 0 )) denied_again( "storage-endorsement" );
                else if (sent( send( data_fd, buffer, params->size, MSG_NOSIGNAL ), params->size, "storage-send" ))
                {
                    if (!receive_packet( buffer, E1S6_STORAGE_QUERY_RESP_SIZE )) denied_again( "storage-receive" );
                    else if (e1s6_u32( buffer ) != 0) denied( "storage-status" );
                    else ok = 1;
                }
            }
            else denied( "operation-phase" );
            if (ok) params->err = 0;
            else { cleanup(); params->channel = 0; params->err = ERROR_ACCESS_DENIED; }
        }
        else denied( "channel-parameters" );
        if (params->err && active_id && params->channel == active_id) cleanup();
        pthread_mutex_unlock( &lock );
    }
#endif
    if (params->err)
    {
        params->channel = 0;
        if (params->buffer && params->size <= 512)
            memset( (void *)(ULONG_PTR)params->buffer, 0, params->size );
    }
    return STATUS_SUCCESS;
}
