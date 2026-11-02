/*
 * Prefetch traces: record the images each Wine process loads, write a .pf v17 file per executable, replay it.
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "trace.h"
#include "pf_v17.h"
#include "readahead.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/xattr.h>
#include <unistd.h>

#define PE_FILE_DLL_FLAG  0x2000u
#define FILETIME_UNIX_EPOCH_SECS 11644473600ULL

struct session
{
    pid_t    pid;
    uid_t    uid;
    char    *priv;         /* NUL-separated paths of the trace this daemon could not open, for the client to warm */
    size_t   priv_len;
    int64_t  start_sec;
    char    *exe;          /* NULL until a non-DLL image is seen */
    char   **files;
    unsigned count;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct session  g_sessions[TRACE_MAX_SESSIONS];
static char            g_dir[PATH_MAX / 2];
static int             g_record, g_replay;

static struct { uint32_t hash; int64_t sec; } g_replayed[TRACE_MAX_SESSIONS];
static unsigned g_replayed_next;

static int64_t mono_sec( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return ts.tv_sec;
}

int trace_init( const char *dir )
{
    if (strlen( dir ) >= sizeof(g_dir)) return -1;
    strcpy( g_dir, dir );
    if (mkdir( dir, 0755 ) < 0 && errno != EEXIST) return -1;
    return 0;
}

void trace_configure( int record, int replay )
{
    pthread_mutex_lock( &g_lock );
    g_record = record;
    g_replay = replay;
    pthread_mutex_unlock( &g_lock );
}

/* UTF-8 to UTF-16 (BMP and surrogates).  Returns the length in code units, or -1 if invalid or too long. */
int trace_path_to_utf16( const char *utf8, uint16_t *out, size_t out_chars )
{
    const unsigned char *s = (const unsigned char *)utf8;
    size_t n = 0;

    while (*s)
    {
        uint32_t c;
        int extra, i;

        if (*s < 0x80) { c = *s; extra = 0; }
        else if ((*s & 0xe0) == 0xc0) { c = *s & 0x1f; extra = 1; }
        else if ((*s & 0xf0) == 0xe0) { c = *s & 0x0f; extra = 2; }
        else if ((*s & 0xf8) == 0xf0) { c = *s & 0x07; extra = 3; }
        else return -1;
        s++;
        for (i = 0; i < extra; i++, s++)
        {
            if ((*s & 0xc0) != 0x80) return -1;
            c = (c << 6) | (*s & 0x3f);
        }
        if (c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) return -1;
        if (c >= 0x10000)
        {
            if (n + 2 >= out_chars) return -1;
            c -= 0x10000;
            out[n++] = 0xd800 | (c >> 10);
            out[n++] = 0xdc00 | (c & 0x3ff);
        }
        else
        {
            if (n + 1 >= out_chars) return -1;
            out[n++] = c;
        }
    }
    out[n] = 0;
    return (int)n;
}

static int utf16_to_utf8( const uint16_t *in, size_t len, char *out, size_t out_size )
{
    size_t i, n = 0;

    for (i = 0; i < len; i++)
    {
        uint32_t c = in[i];
        if (c >= 0xd800 && c < 0xdc00)
        {
            if (i + 1 >= len || in[i + 1] < 0xdc00 || in[i + 1] >= 0xe000) return -1;
            c = 0x10000 + ((c - 0xd800) << 10) + (in[++i] - 0xdc00);
        }
        else if (c >= 0xdc00 && c < 0xe000) return -1;
        if (c == 0) return -1;
        if (c < 0x80) { if (n + 2 > out_size) return -1; out[n++] = c; }
        else if (c < 0x800) { if (n + 3 > out_size) return -1; out[n++] = 0xc0 | (c >> 6); out[n++] = 0x80 | (c & 0x3f); }
        else if (c < 0x10000) { if (n + 4 > out_size) return -1; out[n++] = 0xe0 | (c >> 12); out[n++] = 0x80 | ((c >> 6) & 0x3f); out[n++] = 0x80 | (c & 0x3f); }
        else { if (n + 5 > out_size) return -1; out[n++] = 0xf0 | (c >> 18); out[n++] = 0x80 | ((c >> 12) & 0x3f); out[n++] = 0x80 | ((c >> 6) & 0x3f); out[n++] = 0x80 | (c & 0x3f); }
    }
    out[n] = 0;
    return (int)n;
}

static uint32_t rd32( const uint8_t *p ) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* Reads the file list (as UTF-8 paths, malloc'ed) of a .pf this daemon wrote.  Returns the count, or -1. */
int trace_read_pf( const char *pf_path, char **paths, unsigned max_paths, uint32_t *run_count )
{
    uint8_t *buf;
    struct stat st;
    int fd, count = -1;
    uint32_t moff, mcount, soff, ssize, i;

    fd = open( pf_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC );
    if (fd < 0) return -1;
    if (fstat( fd, &st ) < 0 || !S_ISREG( st.st_mode ) || st.st_size < PF_V17_METRICS_OFFSET || st.st_size > (1 << 24))
    {
        close( fd );
        return -1;
    }
    buf = malloc( st.st_size );
    if (!buf || read( fd, buf, st.st_size ) != st.st_size) goto done;
    if (rd32( buf ) != PF_V17_VERSION || rd32( buf + 4 ) != PF_V17_SIGNATURE || rd32( buf + 12 ) != st.st_size) goto done;

    moff = rd32( buf + 84 ); mcount = rd32( buf + 84 + 4 );
    soff = rd32( buf + 84 + 16 ); ssize = rd32( buf + 84 + 20 );
    if (run_count) *run_count = rd32( buf + 84 + 60 );
    if (mcount > max_paths) goto done;
    count = 0;
    for (i = 0; i < mcount && count >= 0; i++)
    {
        uint64_t e = (uint64_t)moff + (uint64_t)i * PF_V17_METRICS_ENTRY_SIZE;
        uint32_t so, sl;
        char utf8[PF_V17_MAX_STRING_CHARS * 4 + 1];

        if (e + PF_V17_METRICS_ENTRY_SIZE > (uint64_t)st.st_size) { count = -1; break; }
        so = rd32( buf + e + 8 ); sl = rd32( buf + e + 12 );
        if (sl == 0 || sl > PF_V17_MAX_STRING_CHARS || (uint64_t)so + (sl + 1) * 2 > ssize ||
            (uint64_t)soff + so + (sl + 1) * 2 > (uint64_t)st.st_size) { count = -1; break; }
        {
            uint16_t u16[PF_V17_MAX_STRING_CHARS];
            unsigned k;
            for (k = 0; k < sl; k++) u16[k] = buf[soff + so + 2 * k] | (buf[soff + so + 2 * k + 1] << 8);
            if (utf16_to_utf8( u16, sl, utf8, sizeof(utf8) ) < 0 || utf8[0] != '/') { count = -1; break; }
        }
        if (paths)
        {
            paths[count] = strdup( utf8 );
            if (!paths[count]) { count = -1; break; }
        }
        count++;
    }
done:
    if (count < 0 && paths)
    {
        /* callers free paths[0..return) only on success, so release the partial list here */
        unsigned k;
        for (k = 0; k < max_paths; k++) { free( paths[k] ); paths[k] = NULL; }
    }
    free( buf );
    close( fd );
    return count;
}

static void free_session( struct session *s )
{
    unsigned i;
    for (i = 0; i < s->count; i++) free( s->files[i] );
    free( s->files );
    free( s->exe );
    free( s->priv );
    memset( s, 0, sizeof(*s) );
}

static const char *base_name( const char *path )
{
    const char *p = strrchr( path, '/' );
    return p ? p + 1 : path;
}

/* upper-case base name of the executable, cut to what a .pf header holds; returns the length */
static int exe_base_upper( const char *exe, uint16_t out[PF_V17_MAX_EXE_CHARS + 1] )
{
    uint16_t wide[PF_V17_MAX_STRING_CHARS + 1];
    int n = trace_path_to_utf16( base_name( exe ), wide, PF_V17_MAX_STRING_CHARS + 1 );
    int i;

    if (n <= 0) return 0;
    if (n > (int)PF_V17_MAX_EXE_CHARS) n = PF_V17_MAX_EXE_CHARS;
    for (i = 0; i < n; i++) out[i] = (wide[i] >= 'a' && wide[i] <= 'z') ? wide[i] - 'a' + 'A' : wide[i];
    out[n] = 0;
    return n;
}

static void pf_name_for( const char *exe, char *name, size_t size, uint32_t *hash_out )
{
    uint16_t full[PF_V17_MAX_STRING_CHARS + 1], base[PF_V17_MAX_EXE_CHARS + 1];
    int n = trace_path_to_utf16( exe, full, PF_V17_MAX_STRING_CHARS + 1 );
    int i;
    uint32_t hash;

    if (n < 0) n = 0;
    for (i = 0; i < n; i++) if (full[i] >= 'a' && full[i] <= 'z') full[i] -= 'a' - 'A';
    hash = pf_prefetch_hash_xp( full, n );
    *hash_out = hash;
    if (!exe_base_upper( exe, base )) snprintf( name, size, "UNKNOWN-%08X.pf", (unsigned)hash );
    else pf_make_name( base, hash, name, size );
}

/* A POSIX ACL as the kernel stores it ("system.posix_acl_access" / "_default"): version 2 and sorted entries.
 * Owner (the daemon) full access, the named user and the mask read-only, nobody else anything. */
static void set_user_acl( const char *dir, uid_t uid, int is_default, unsigned user_perm, unsigned mask_perm )
{
    struct { uint16_t tag, perm; uint32_t id; } e[5] =
    {
        { 0x01, 7, 0xffffffffu },          /* USER_OBJ */
        { 0x02, user_perm, uid },          /* USER */
        { 0x04, 0, 0xffffffffu },          /* GROUP_OBJ */
        { 0x10, mask_perm, 0xffffffffu },  /* MASK */
        { 0x20, 0, 0xffffffffu },          /* OTHER */
    };
    uint8_t buf[4 + sizeof(e)];
    uint32_t version = 2;

    memcpy( buf, &version, 4 );
    memcpy( buf + 4, e, sizeof(e) );
    setxattr( dir, is_default ? "system.posix_acl_default" : "system.posix_acl_access", buf, sizeof(buf), 0 );
}

/* <dir>/<uid>: the C:\Windows\Prefetch folder holds one directory per user.  A trace lists the paths a user's
 * programs load, so the daemon owns it and only that user may read it (an ACL; where the filesystem has none, nobody
 * but the daemon can). */
static int user_dir( uid_t uid, char *out, size_t size )
{
    if (snprintf( out, size, "%s/%u", g_dir, (unsigned)uid ) >= (int)size) return -1;
    if (mkdir( out, 0750 ) == 0)
    {
        set_user_acl( out, uid, 0, 5, 5 );   /* r-x on the directory */
        set_user_acl( out, uid, 1, 4, 4 );   /* new files: r-- (their mode is 0640) */
    }
    else if (errno != EEXIST) return -1;
    return 0;
}

/* writes <dir>/<uid>/<NAME>-<HASH>.pf from a finished session; the previous run count is carried over */
static void write_session( const struct session *s )
{
    char udir[PATH_MAX / 2 + 16];
    struct pf_file *files = NULL;
    uint16_t (*wide)[PF_V17_MAX_STRING_CHARS + 1] = NULL;
    uint16_t base[PF_V17_MAX_EXE_CHARS + 1];
    struct pf_info info;
    char name[96], path[PATH_MAX], tmp[PATH_MAX];
    uint8_t *out = NULL;
    size_t out_len = 0;
    uint32_t hash, runs = 0;
    unsigned i, n = 0;
    struct stat st;
    int fd;

    if (!s->exe || s->count < 2 || user_dir( s->uid, udir, sizeof(udir) ) < 0) return;
    pf_name_for( s->exe, name, sizeof(name), &hash );
    snprintf( path, sizeof(path), "%s/%s", udir, name );

    if (stat( path, &st ) < 0)
    {
        /* a new executable: stay under the cap (per user) */
        DIR *d = opendir( udir );
        struct dirent *e;
        unsigned have = 0;
        if (d) { while ((e = readdir( d ))) if (strstr( e->d_name, ".pf" )) have++; closedir( d ); }
        if (have >= TRACE_MAX_PF_FILES) return;
    }
    else trace_read_pf( path, NULL, TRACE_MAX_PF_FILES, &runs );

    files = calloc( s->count, sizeof(*files) );
    wide = calloc( s->count, sizeof(*wide) );
    if (!files || !wide) goto out;
    for (i = 0; i < s->count && n < PF_V17_MAX_FILES; i++)
    {
        if (trace_path_to_utf16( s->files[i], wide[n], PF_V17_MAX_STRING_CHARS + 1 ) <= 0) continue;
        files[n].path_utf16 = wide[n];
        n++;
    }
    if (n < 2) goto out;

    memset( &info, 0, sizeof(info) );
    if (!exe_base_upper( s->exe, base )) goto out;
    info.exe_name_utf16 = base;
    info.path_hash = hash;
    info.run_count = runs + 1;
    info.last_run_filetime = ((uint64_t)time( NULL ) + FILETIME_UNIX_EPOCH_SECS) * 10000000ULL;
    info.files = files;
    info.file_count = n;
    if (pf_write_v17( &info, &out, &out_len ) < 0) goto out;

    snprintf( tmp, sizeof(tmp), "%s/.%s.tmp", udir, name );
    fd = open( tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0640 );
    if (fd < 0) goto out;
    if (write( fd, out, out_len ) != (ssize_t)out_len || fsync( fd ) < 0 || close( fd ) < 0 || rename( tmp, path ) < 0)
        unlink( tmp );
out:
    free( out );
    free( files );
    free( wide );
}

static void finish_session( struct session *s )
{
    if (g_record) write_session( s );
    free_session( s );
}

/* files the daemon could open are warmed by this thread, which owns them */
struct replay_job
{
    char             pf[PATH_MAX];
    struct ra_item  *items;
    struct ra_range *ranges;
    unsigned         n;
};

static void *replay_thread( void *arg )
{
    struct replay_job *job = arg;
    struct ra_stats stats;
    unsigned i;

    ra_replay( job->items, job->n, RA_AUTO, &stats );
    if (getenv( "WEXPF_VERBOSE" ))
        fprintf( stderr, "readahead: %u files, %llu ranges from %s\n", job->n,
                 (unsigned long long)stats.ranges_issued, job->pf );
    for (i = 0; i < job->n; i++) close( job->items[i].fd );
    free( job->items );
    free( job->ranges );
    free( job );
    return NULL;
}

#define TRACE_PRIV_MAX 60000   /* what one reply datagram carries to the client */

/* Opens every file of the user's trace for the program.  Files this daemon can read are warmed by a thread; the
 * paths it cannot open (a private home directory, another mount namespace) go to s->priv for the client to warm
 * itself.  The list is the user's own trace, so it shows them nothing they did not load.  g_lock held. */
static void maybe_replay( struct session *s, int (*fd_ok)( int ) )
{
    char name[96], udir[PATH_MAX / 2 + 16];
    char *paths[TRACE_MAX_PF_FILES] = { 0 };
    struct replay_job *job;
    uint32_t hash, key;
    pthread_t tid;
    pthread_attr_t attr;
    int64_t now = mono_sec();
    int count;
    unsigned i;

    if (!g_replay || user_dir( s->uid, udir, sizeof(udir) ) < 0) return;
    pf_name_for( s->exe, name, sizeof(name), &hash );
    key = hash ^ ((uint32_t)s->uid * 2654435761u);
    for (i = 0; i < TRACE_MAX_SESSIONS; i++)
        if (g_replayed[i].hash == key && g_replayed[i].sec && now - g_replayed[i].sec < TRACE_REPLAY_MIN_SECS) return;
    job = calloc( 1, sizeof(*job) );
    if (!job) return;
    snprintf( job->pf, sizeof(job->pf), "%s/%s", udir, name );
    count = trace_read_pf( job->pf, paths, TRACE_MAX_PF_FILES, NULL );
    if (count <= 0) { free( job ); return; }
    job->items = calloc( count, sizeof(*job->items) );
    job->ranges = calloc( count, sizeof(*job->ranges) );
    s->priv = calloc( 1, TRACE_PRIV_MAX );
    if (!job->items || !job->ranges || !s->priv) goto fail;

    for (i = 0; i < (unsigned)count; i++)
    {
        struct stat st;
        size_t len = strlen( paths[i] ) + 1;
        int fd = open( paths[i], O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC );

        if (fd < 0)
        {
            if (s->priv_len + len <= TRACE_PRIV_MAX)
            {
                memcpy( s->priv + s->priv_len, paths[i], len );
                s->priv_len += len;
            }
            continue;
        }
        if (fstat( fd, &st ) < 0 || !S_ISREG( st.st_mode ) || st.st_size <= 0 || !fd_ok( fd ))
        {
            close( fd );
            continue;
        }
        job->ranges[job->n].offset = 0;
        job->ranges[job->n].length = (uint64_t)st.st_size < RA_MAX_RANGE_BYTES ? (uint64_t)st.st_size : RA_MAX_RANGE_BYTES;
        job->items[job->n].fd = fd;
        job->items[job->n].ranges = &job->ranges[job->n];
        job->items[job->n].range_count = 1;
        job->n++;
    }
    if (!s->priv_len) { free( s->priv ); s->priv = NULL; }
    g_replayed[g_replayed_next].hash = key;
    g_replayed[g_replayed_next].sec = now;
    g_replayed_next = (g_replayed_next + 1) % TRACE_MAX_SESSIONS;
    if (job->n)
    {
        pthread_attr_init( &attr );
        pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
        pthread_attr_setstacksize( &attr, 512 * 1024 );
        if (pthread_create( &tid, &attr, replay_thread, job ) == 0) job = NULL;
        pthread_attr_destroy( &attr );
    }
fail:
    if (job)
    {
        for (i = 0; i < job->n; i++) close( job->items[i].fd );
        free( job->items );
        free( job->ranges );
        free( job );
    }
    for (i = 0; i < TRACE_MAX_PF_FILES; i++) free( paths[i] );
}

/* true for a PE file whose header says it is not a DLL */
static int is_exe_image( int fd )
{
    uint8_t dos[64], nt[24];
    uint32_t lfanew;

    if (pread( fd, dos, sizeof(dos), 0 ) != sizeof(dos) || dos[0] != 'M' || dos[1] != 'Z') return 0;
    lfanew = rd32( dos + 0x3c );
    if (lfanew > (1u << 20) || pread( fd, nt, sizeof(nt), lfanew ) != sizeof(nt) || nt[0] != 'P' || nt[1] != 'E') return 0;
    return !((nt[22] | (nt[23] << 8)) & PE_FILE_DLL_FLAG);
}

int trace_image( pid_t pid, uid_t uid, int fd, int (*fd_ok)( int ) )
{
    char link[64], path[PATH_MAX];
    ssize_t n;
    struct session *s = NULL, *oldest = NULL;
    unsigned i;
    int is_exe, hint = 0;

    if ((!g_record && !g_replay) || !g_dir[0]) return 0;
    snprintf( link, sizeof(link), "/proc/self/fd/%d", fd );
    n = readlink( link, path, sizeof(path) - 1 );
    if (n <= 0 || path[0] != '/') return 0;
    path[n] = 0;
    if (n >= 10 && !strcmp( path + n - 10, " (deleted)" )) return 0;
    is_exe = is_exe_image( fd );

    pthread_mutex_lock( &g_lock );
    for (i = 0; i < TRACE_MAX_SESSIONS; i++)
    {
        if (g_sessions[i].pid == pid) { s = &g_sessions[i]; break; }
        if (!g_sessions[i].pid) { if (!s) s = &g_sessions[i]; }
        else if (!oldest || g_sessions[i].start_sec < oldest->start_sec) oldest = &g_sessions[i];
    }
    if (s && s->pid != pid && s->pid == 0) { s->pid = pid; s->uid = uid; s->start_sec = mono_sec(); }
    else if (!s || s->pid != pid)
    {
        if (!oldest) goto out;
        finish_session( oldest );
        s = oldest;
        s->pid = pid;
        s->uid = uid;
        s->start_sec = mono_sec();
    }

    if (is_exe && !s->exe)
    {
        s->exe = strdup( path );
        if (s->exe) maybe_replay( s, fd_ok );
    }
    hint = s->priv_len > 0;
    for (i = 0; i < s->count; i++) if (!strcmp( s->files[i], path )) goto out;
    if (s->count < PF_V17_MAX_FILES)
    {
        char **grown = realloc( s->files, (s->count + 1) * sizeof(*grown) );
        if (grown)
        {
            s->files = grown;
            s->files[s->count] = strdup( path );
            if (s->files[s->count]) s->count++;
        }
    }
out:
    pthread_mutex_unlock( &g_lock );
    return hint;
}

/* Hands the process the paths of its trace that the daemon could not open, once.  Returns the byte count. */
size_t trace_take_private( pid_t pid, uid_t uid, char *buf, size_t cap )
{
    size_t n = 0;
    unsigned i;

    pthread_mutex_lock( &g_lock );
    for (i = 0; i < TRACE_MAX_SESSIONS; i++)
    {
        struct session *s = &g_sessions[i];
        if (s->pid != pid || s->uid != uid || !s->priv_len) continue;
        if (s->priv_len <= cap) { n = s->priv_len; memcpy( buf, s->priv, n ); }
        free( s->priv );
        s->priv = NULL;
        s->priv_len = 0;
        break;
    }
    pthread_mutex_unlock( &g_lock );
    return n;
}

void trace_tick( void )
{
    int64_t now = mono_sec();
    unsigned i;

    pthread_mutex_lock( &g_lock );
    for (i = 0; i < TRACE_MAX_SESSIONS; i++)
    {
        struct session *s = &g_sessions[i];
        if (!s->pid) continue;
        if (now - s->start_sec >= TRACE_WINDOW_SECONDS || (kill( s->pid, 0 ) < 0 && errno == ESRCH))
            finish_session( s );
    }
    pthread_mutex_unlock( &g_lock );
}

void trace_flush_all( void )
{
    unsigned i;

    pthread_mutex_lock( &g_lock );
    for (i = 0; i < TRACE_MAX_SESSIONS; i++)
        if (g_sessions[i].pid) finish_session( &g_sessions[i] );
    pthread_mutex_unlock( &g_lock );
}
