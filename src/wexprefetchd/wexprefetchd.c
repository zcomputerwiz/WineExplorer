/*
 * wexprefetchd: unprivileged page-aligned PE flat-image cache daemon.
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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <syslog.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/vfs.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <limits.h>

#include "protocol.h"
#include "sha256.h"
#include "trace.h"
#include "ksm_tuner.h"

#define DEFAULT_TRACE_DIR        "/var/cache/wineexplorer/prefetch"

#define DEFAULT_CACHE_DIR        "/var/cache/wineexplorer/images"
#define DEFAULT_CONFIG_PATH      "/etc/wineexplorer/prefetch.conf"
#define DEFAULT_ENABLED          1
#define DEFAULT_MAX_CACHE_MB     8192u
#define DEFAULT_MAX_CONCURRENT   128u  /* a browser start-up has 20+ processes loading images at once */
#define DEFAULT_MAX_IMAGE_MB     1024u
#define DEFAULT_MAX_ENTRIES      4096u
#define DEFAULT_MAX_BUILDS_MIN   120u   /* reads+hashes of unknown files per user per minute */
#define ENTRY_OVERHEAD_BYTES     4096ULL /* charged per cache entry on top of its blocks: images are sparse */

/* statfs f_type of filesystems whose reads can stall forever or are controlled by an unprivileged user */
#define WEXPF_FS_FUSE            0x65735546
#define WEXPF_FS_NFS             0x6969
#define WEXPF_FS_SMB             0x517B
#define WEXPF_FS_CIFS            0xFF534D42
#define WEXPF_FS_SMB2            0xFE534D42
#define WEXPF_FS_9P              0x01021997
#define WEXPF_FS_AFS             0x5346414F
#define WEXPF_FS_CEPH            0x00C36400

#define SD_LISTEN_FDS_START      3
#define IDLE_EXIT_SECONDS        120
#define SOCKET_TIMEOUT_SECONDS   10
#define STALE_TMP_SECONDS        600
#define MTIME_TOUCH_INTERVAL_SEC 600
#define STATUS_WRITE_INTERVAL_S  5

#define MEMO_TOTAL_ENTRIES       4096u
#define MEMO_WAYS                4u
#define MEMO_BUCKETS             (MEMO_TOTAL_ENTRIES / MEMO_WAYS)

#define PE_MAX_SECTIONS          96u
#define PE_PAGE_SIZE             0x1000u
#define PE_SECTOR_MASK           0x1ffULL
#define PE_SECTOR_ALIGN          0x200ULL
#define IMAGE_SCN_MEM_SHARED     0x10000000u
#define IMAGE_SCN_MEM_WRITE      0x80000000u

struct file_identity
{
    uint64_t dev;
    uint64_t ino;
    uint64_t size;
    int64_t  mtim_sec;
    int64_t  mtim_nsec;
    int64_t  ctim_sec;
    int64_t  ctim_nsec;
};

struct memo_entry
{
    int                  valid;
    struct file_identity id;
    uint8_t              sha256[32];
    uint64_t             image_size;
    uint64_t             use_seq;
    int64_t              last_touch_sec;
};

struct pe_section_copy
{
    uint32_t index;
    char     name[9];
    uint32_t va;
    uint64_t file_start;
    uint64_t file_size;
};

struct pe_parsed_image
{
    uint64_t               image_size;
    uint32_t               section_count;
    struct pe_section_copy sections[PE_MAX_SECTIONS];
};

struct daemon_config
{
    int      enabled;
    uint64_t max_cache_bytes;
    uint32_t max_concurrent;
    uint64_t max_image_bytes;
    uint32_t max_entries;          /* cache entry count cap */
    uint32_t max_builds_per_min;   /* per peer uid; 0 = unlimited */
    int      reject_network_fs;    /* refuse sources on FUSE, NFS, SMB, 9p ... */
    int      record_traces;        /* Prefetch=: write a .pf per executable */
    int      readahead;            /* Readahead=: replay the .pf into the page cache when the executable starts */
};

struct daemon_ctx
{
    char                 socket_path[PATH_MAX];
    char                 runtime_dir[PATH_MAX];
    char                 status_path[PATH_MAX];
    char                 cache_dir[PATH_MAX];
    char                 config_path[PATH_MAX];
    int                  foreground;
    int                  socket_activated;
    struct daemon_config cfg;
};

struct worker_arg
{
    struct daemon_ctx *ctx;
    int                conn_fd;
};

struct prune_candidate
{
    char     name[72];
    uint64_t disk_bytes;
    int64_t  mtime_sec;
    int64_t  mtime_nsec;
};

/*
 * Documented global mutable state:
 *
 * 1. g_sig_stop / g_sig_reload: async-signal-safe flags set by POSIX signal handlers for SIGTERM/SIGINT and SIGHUP.
 * 2. g_memo_lock / g_memo / g_memo_seq: 4096-entry identity->(sha256, image_size) memo table protected by g_memo_lock.
 * 3. g_build_lock / g_build_seq: single global mutex serializing all image builds.  It is the outermost lock:
 *    g_memo_lock and g_state_lock are leaf locks that may be taken while it is held, never the other way round.
 * 4. g_state_lock / g_state_cond / g_counters: observability counters (requests, memo_hits, cache_hits, builds,
 *    rejects, bytes_built), active worker count, and last status write timestamp.
 */
static volatile sig_atomic_t g_sig_stop = 0;
static volatile sig_atomic_t g_sig_reload = 0;

static pthread_mutex_t       g_memo_lock = PTHREAD_MUTEX_INITIALIZER;
static struct memo_entry     g_memo[MEMO_TOTAL_ENTRIES];
static uint64_t              g_memo_seq = 0;

static pthread_mutex_t       g_build_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t              g_build_seq = 0;

static pthread_mutex_t       g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        g_state_cond = PTHREAD_COND_INITIALIZER;
static struct
{
    uint64_t requests;
    uint64_t memo_hits;
    uint64_t cache_hits;
    uint64_t builds;
    uint64_t rejects;
    uint64_t bytes_built;
    uint32_t active_workers;
    int64_t  last_activity_mono;
    int64_t  last_status_write_mono;
} g_counters;

static void on_sig_stop( int sig )
{
    (void)sig;
    g_sig_stop = 1;
}

static void on_sig_reload( int sig )
{
    (void)sig;
    g_sig_reload = 1;
}

static int64_t now_monotonic_sec( void )
{
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ) != 0)
        return 0;
    return (int64_t)ts.tv_sec;
}

static int64_t now_monotonic_ms( void )
{
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ) != 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static int64_t now_realtime_sec( void )
{
    struct timespec ts;
    if (clock_gettime( CLOCK_REALTIME, &ts ) != 0)
        return (int64_t)time( NULL );
    return (int64_t)ts.tv_sec;
}

__attribute__((format(printf, 3, 4)))
static void log_msg( int foreground, int priority, const char *fmt, ... )
{
    char buf[1024];
    va_list ap;

    va_start( ap, fmt );
    vsnprintf( buf, sizeof(buf), fmt, ap );
    va_end( ap );

    syslog( priority, "%s", buf );
    if (foreground)
        fprintf( stderr, "wexprefetchd: %s\n", buf );
}

static int close_no_eintr( int fd )
{
    int ret;
    if (fd < 0)
        return 0;
    do
    {
        ret = close( fd );
    } while (ret < 0 && errno == EINTR);
    return ret;
}

static uint16_t read_u16_le( const uint8_t *p )
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le( const uint8_t *p )
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t round_up_u64( uint64_t val, uint64_t align )
{
    uint64_t mask = align - 1ULL;
    if (align == 0 || (align & mask) != 0)
        return val;
    if (val > UINT64_MAX - mask)
        return UINT64_MAX;
    return (val + mask) & ~mask;
}

static const char *status_name( enum wexpf_status st )
{
    switch (st)
    {
    case WEXPF_OK:          return "WEXPF_OK";
    case WEXPF_BAD_REQUEST: return "WEXPF_BAD_REQUEST";
    case WEXPF_NOT_PE:      return "WEXPF_NOT_PE";
    case WEXPF_TOO_LARGE:   return "WEXPF_TOO_LARGE";
    case WEXPF_BUSY:        return "WEXPF_BUSY";
    case WEXPF_IO_ERROR:    return "WEXPF_IO_ERROR";
    case WEXPF_DISABLED:    return "WEXPF_DISABLED";
    case WEXPF_UNSUPPORTED: return "WEXPF_UNSUPPORTED";
    default:                return "WEXPF_UNKNOWN";
    }
}

static void extract_identity( const struct stat *st, struct file_identity *id )
{
    memset( id, 0, sizeof(*id) );
    id->dev       = (uint64_t)st->st_dev;
    id->ino       = (uint64_t)st->st_ino;
    id->size      = (uint64_t)st->st_size;
    id->mtim_sec  = (int64_t)st->st_mtim.tv_sec;
    id->mtim_nsec = (int64_t)st->st_mtim.tv_nsec;
    id->ctim_sec  = (int64_t)st->st_ctim.tv_sec;
    id->ctim_nsec = (int64_t)st->st_ctim.tv_nsec;
}

static int identity_equal( const struct file_identity *a, const struct file_identity *b )
{
    return a->dev == b->dev &&
           a->ino == b->ino &&
           a->size == b->size &&
           a->mtim_sec == b->mtim_sec &&
           a->mtim_nsec == b->mtim_nsec &&
           a->ctim_sec == b->ctim_sec &&
           a->ctim_nsec == b->ctim_nsec;
}

static uint32_t hash_identity( const struct file_identity *id )
{
    uint64_t h = 1469598103934665603ULL;
    const uint64_t words[7] = {
        id->dev,
        id->ino,
        id->size,
        (uint64_t)id->mtim_sec,
        (uint64_t)id->mtim_nsec,
        (uint64_t)id->ctim_sec,
        (uint64_t)id->ctim_nsec
    };
    size_t i;

    for (i = 0; i < 7; i++)
    {
        uint64_t w = words[i];
        h ^= w;
        h *= 1099511628211ULL;
        h ^= (h >> 32);
    }
    return (uint32_t)(h & (MEMO_BUCKETS - 1u));
}

static int memo_lookup( const struct file_identity *id, uint8_t sha_out[32],
                        uint64_t *image_size_out, int64_t *last_touch_out )
{
    uint32_t bucket = hash_identity( id );
    uint32_t base = bucket * MEMO_WAYS;
    uint32_t w;
    int found = 0;

    pthread_mutex_lock( &g_memo_lock );
    for (w = 0; w < MEMO_WAYS; w++)
    {
        struct memo_entry *e = &g_memo[base + w];
        if (e->valid && identity_equal( &e->id, id ))
        {
            e->use_seq = ++g_memo_seq;
            memcpy( sha_out, e->sha256, 32 );
            *image_size_out = e->image_size;
            *last_touch_out = e->last_touch_sec;
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock( &g_memo_lock );
    return found;
}

static void memo_update_touch( const struct file_identity *id, int64_t touch_sec )
{
    uint32_t bucket = hash_identity( id );
    uint32_t base = bucket * MEMO_WAYS;
    uint32_t w;

    pthread_mutex_lock( &g_memo_lock );
    for (w = 0; w < MEMO_WAYS; w++)
    {
        struct memo_entry *e = &g_memo[base + w];
        if (e->valid && identity_equal( &e->id, id ))
        {
            e->last_touch_sec = touch_sec;
            break;
        }
    }
    pthread_mutex_unlock( &g_memo_lock );
}

static void memo_store( const struct file_identity *id, const uint8_t sha[32],
                        uint64_t image_size, int64_t touch_sec )
{
    uint32_t bucket = hash_identity( id );
    uint32_t base = bucket * MEMO_WAYS;
    uint32_t w;
    struct memo_entry *target = NULL;

    pthread_mutex_lock( &g_memo_lock );
    for (w = 0; w < MEMO_WAYS; w++)
    {
        struct memo_entry *e = &g_memo[base + w];
        if (e->valid && identity_equal( &e->id, id ))
        {
            target = e;
            break;
        }
    }
    if (!target)
    {
        for (w = 0; w < MEMO_WAYS; w++)
        {
            struct memo_entry *e = &g_memo[base + w];
            if (!e->valid)
            {
                target = e;
                break;
            }
            if (!target || e->use_seq < target->use_seq)
                target = e;
        }
    }

    target->valid          = 1;
    target->id             = *id;
    memcpy( target->sha256, sha, 32 );
    target->image_size     = image_size;
    target->use_seq        = ++g_memo_seq;
    target->last_touch_sec = touch_sec;
    pthread_mutex_unlock( &g_memo_lock );
}

static int mkdir_p( const char *dir_path, mode_t mode )
{
    char tmp[PATH_MAX];
    size_t len;
    char *p;
    struct stat st;

    if (!dir_path || !dir_path[0])
        return -1;

    len = strlen( dir_path );
    if (len >= sizeof(tmp))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy( tmp, dir_path, len + 1 );
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir( tmp, mode ) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir( tmp, mode ) < 0 && errno != EEXIST)
        return -1;

    if (stat( tmp, &st ) < 0)
        return -1;
    if (!S_ISDIR( st.st_mode ))
    {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static int derive_parent_dir( const char *path, char *out, size_t out_size )
{
    const char *slash = strrchr( path, '/' );
    int n;

    if (!slash)
    {
        n = snprintf( out, out_size, "." );
    }
    else if (slash == path)
    {
        n = snprintf( out, out_size, "/" );
    }
    else
    {
        size_t dir_len = (size_t)(slash - path);
        if (dir_len >= out_size)
            return -1;
        memcpy( out, path, dir_len );
        out[dir_len] = '\0';
        return 0;
    }
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static char *trim_whitespace( char *s )
{
    char *end;
    while (*s && isspace( (unsigned char)*s ))
        s++;
    if (!*s)
        return s;
    end = s + strlen( s ) - 1;
    while (end > s && isspace( (unsigned char)*end ))
        *end-- = '\0';
    return s;
}

static void init_default_config( struct daemon_config *cfg )
{
    cfg->enabled         = DEFAULT_ENABLED;
    cfg->max_cache_bytes = (uint64_t)DEFAULT_MAX_CACHE_MB * 1024ULL * 1024ULL;
    cfg->max_concurrent  = DEFAULT_MAX_CONCURRENT;
    cfg->max_image_bytes = (uint64_t)DEFAULT_MAX_IMAGE_MB * 1024ULL * 1024ULL;
    if (cfg->max_image_bytes > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
        cfg->max_image_bytes = (uint64_t)WEXPF_MAX_IMAGE_SIZE;
    cfg->max_entries        = DEFAULT_MAX_ENTRIES;
    cfg->max_builds_per_min = DEFAULT_MAX_BUILDS_MIN;
    cfg->reject_network_fs  = 1;
    cfg->record_traces      = 1;
    cfg->readahead          = 1;
}

static int parse_bool( const char *val, int *out )
{
    if (strcmp( val, "0" ) == 0 || strcasecmp( val, "false" ) == 0 || strcasecmp( val, "no" ) == 0) *out = 0;
    else if (strcmp( val, "1" ) == 0 || strcasecmp( val, "true" ) == 0 || strcasecmp( val, "yes" ) == 0) *out = 1;
    else return 0;
    return 1;
}

static void load_config( struct daemon_ctx *ctx, int is_reload )
{
    struct daemon_config next;
    FILE *fp;
    char line[512];

    init_default_config( &next );

    fp = fopen( ctx->config_path, "r" );
    if (!fp)
    {
        if (errno != ENOENT)
        {
            log_msg( ctx->foreground, LOG_WARNING,
                     "failed to open config %s: %s (using defaults)",
                     ctx->config_path, strerror( errno ) );
        }
    }
    else
    {
        while (fgets( line, sizeof(line), fp ) != NULL)
        {
            char *hash = strchr( line, '#' );
            char *eq;
            char *key;
            char *val;
            char *endp = NULL;
            unsigned long long parsed_val;

            if (hash)
                *hash = '\0';

            key = trim_whitespace( line );
            if (!*key)
                continue;

            eq = strchr( key, '=' );
            if (!eq)
                continue;
            *eq = '\0';
            key = trim_whitespace( key );
            val = trim_whitespace( eq + 1 );

            if (strcmp( key, "Enabled" ) == 0)
            {
                if (strcmp( val, "0" ) == 0 || strcasecmp( val, "false" ) == 0 || strcasecmp( val, "no" ) == 0)
                    next.enabled = 0;
                else if (strcmp( val, "1" ) == 0 || strcasecmp( val, "true" ) == 0 || strcasecmp( val, "yes" ) == 0)
                    next.enabled = 1;
            }
            else if (strcmp( key, "MaxCacheMB" ) == 0)
            {
                errno = 0;
                parsed_val = strtoull( val, &endp, 10 );
                if (errno == 0 && endp && *endp == '\0' && parsed_val > 0 &&
                    parsed_val <= (ULLONG_MAX / (1024ULL * 1024ULL)))
                {
                    next.max_cache_bytes = (uint64_t)parsed_val * 1024ULL * 1024ULL;
                }
            }
            else if (strcmp( key, "MaxConcurrent" ) == 0)
            {
                errno = 0;
                parsed_val = strtoull( val, &endp, 10 );
                if (errno == 0 && endp && *endp == '\0' && parsed_val >= 1 && parsed_val <= 4096)
                {
                    next.max_concurrent = (uint32_t)parsed_val;
                }
            }
            else if (strcmp( key, "MaxEntries" ) == 0)
            {
                errno = 0;
                parsed_val = strtoull( val, &endp, 10 );
                if (errno == 0 && endp && *endp == '\0' && parsed_val >= 1 && parsed_val <= 1000000)
                    next.max_entries = (uint32_t)parsed_val;
            }
            else if (strcmp( key, "MaxBuildsPerMinute" ) == 0)
            {
                errno = 0;
                parsed_val = strtoull( val, &endp, 10 );
                if (errno == 0 && endp && *endp == '\0' && parsed_val <= 1000000)
                    next.max_builds_per_min = (uint32_t)parsed_val;
            }
            else if (strcmp( key, "RejectNetworkFS" ) == 0)
            {
                if (strcmp( val, "0" ) == 0 || strcasecmp( val, "false" ) == 0 || strcasecmp( val, "no" ) == 0)
                    next.reject_network_fs = 0;
                else if (strcmp( val, "1" ) == 0 || strcasecmp( val, "true" ) == 0 || strcasecmp( val, "yes" ) == 0)
                    next.reject_network_fs = 1;
            }
            else if (strcmp( key, "Prefetch" ) == 0)
                parse_bool( val, &next.record_traces );
            else if (strcmp( key, "Readahead" ) == 0)
                parse_bool( val, &next.readahead );
            else if (strcmp( key, "MaxImageMB" ) == 0)
            {
                errno = 0;
                parsed_val = strtoull( val, &endp, 10 );
                if (errno == 0 && endp && *endp == '\0' && parsed_val > 0)
                {
                    uint64_t bytes;
                    if (parsed_val > (ULLONG_MAX / (1024ULL * 1024ULL)))
                        bytes = (uint64_t)WEXPF_MAX_IMAGE_SIZE;
                    else
                        bytes = (uint64_t)parsed_val * 1024ULL * 1024ULL;
                    if (bytes > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
                        bytes = (uint64_t)WEXPF_MAX_IMAGE_SIZE;
                    next.max_image_bytes = bytes;
                }
            }
        }
        fclose( fp );
    }

    pthread_mutex_lock( &g_state_lock );
    ctx->cfg = next;
    pthread_mutex_unlock( &g_state_lock );
    trace_configure( next.enabled && next.record_traces, next.enabled && next.readahead );

    if (is_reload)
    {
        log_msg( ctx->foreground, LOG_INFO,
                 "config reloaded from %s: Enabled=%d MaxCacheMB=%" PRIu64
                 " MaxConcurrent=%u MaxImageMB=%" PRIu64 " MaxEntries=%u MaxBuildsPerMinute=%u RejectNetworkFS=%d",
                 ctx->config_path, next.enabled,
                 (uint64_t)(next.max_cache_bytes / (1024ULL * 1024ULL)),
                 next.max_concurrent,
                 (uint64_t)(next.max_image_bytes / (1024ULL * 1024ULL)),
                 next.max_entries, next.max_builds_per_min, next.reject_network_fs );
    }
}

static void get_config_snapshot( struct daemon_ctx *ctx, struct daemon_config *out )
{
    pthread_mutex_lock( &g_state_lock );
    *out = ctx->cfg;
    pthread_mutex_unlock( &g_state_lock );
}

static void write_status_file( const struct daemon_ctx *ctx, int force )
{
    uint64_t requests;
    uint64_t memo_hits;
    uint64_t cache_hits;
    uint64_t builds;
    uint64_t rejects;
    uint64_t bytes_built;
    int64_t now_mono = now_monotonic_sec();
    char tmp_path[PATH_MAX];
    char json_buf[512];
    int json_len;
    int n;
    int fd;
    ssize_t written = 0;

    pthread_mutex_lock( &g_state_lock );
    if (!force && g_counters.last_status_write_mono != 0 &&
        (now_mono - g_counters.last_status_write_mono) < STATUS_WRITE_INTERVAL_S)
    {
        pthread_mutex_unlock( &g_state_lock );
        return;
    }
    g_counters.last_status_write_mono = now_mono ? now_mono : 1;
    requests    = g_counters.requests;
    memo_hits   = g_counters.memo_hits;
    cache_hits  = g_counters.cache_hits;
    builds      = g_counters.builds;
    rejects     = g_counters.rejects;
    bytes_built = g_counters.bytes_built;
    pthread_mutex_unlock( &g_state_lock );

    n = snprintf( tmp_path, sizeof(tmp_path), "%s/.prefetch.status.tmp.%ld.%lu",
                  ctx->runtime_dir, (long)getpid(), (unsigned long)pthread_self() );
    if (n < 0 || (size_t)n >= sizeof(tmp_path))
        return;

    json_len = snprintf( json_buf, sizeof(json_buf),
                         "{\n"
                         "  \"requests\": %" PRIu64 ",\n"
                         "  \"memo_hits\": %" PRIu64 ",\n"
                         "  \"cache_hits\": %" PRIu64 ",\n"
                         "  \"builds\": %" PRIu64 ",\n"
                         "  \"rejects\": %" PRIu64 ",\n"
                         "  \"bytes_built\": %" PRIu64 "\n"
                         "}\n",
                         requests, memo_hits, cache_hits, builds, rejects, bytes_built );
    if (json_len < 0 || (size_t)json_len >= sizeof(json_buf))
        return;

    do
    {
        fd = open( tmp_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return;

    while (written < (ssize_t)json_len)
    {
        ssize_t w = write( fd, json_buf + written, (size_t)(json_len - written) );
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            close_no_eintr( fd );
            unlink( tmp_path );
            return;
        }
        written += w;
    }

    if (close_no_eintr( fd ) < 0)
    {
        unlink( tmp_path );
        return;
    }

    if (rename( tmp_path, ctx->status_path ) < 0)
        unlink( tmp_path );
}

static ssize_t pread_all( int fd, uint8_t *buf, size_t count, off_t offset )
{
    size_t total = 0;

    while (total < count)
    {
        size_t chunk = count - total;
        ssize_t r;

        if (chunk > (1u << 26))
            chunk = (1u << 26);

        r = pread( fd, buf + total, chunk, offset + (off_t)total );
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break;
        total += (size_t)r;
    }
    return (ssize_t)total;
}

static int pwrite_all( int fd, const uint8_t *buf, size_t count, off_t offset )
{
    size_t total = 0;

    while (total < count)
    {
        size_t chunk = count - total;
        ssize_t w;

        if (chunk > (1u << 26))
            chunk = (1u << 26);

        w = pwrite( fd, buf + total, chunk, offset + (off_t)total );
        if (w < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
        {
            errno = EIO;
            return -1;
        }
        total += (size_t)w;
    }
    return 0;
}

static enum wexpf_status parse_pe_image( const uint8_t *buf, uint64_t file_size,
                                         uint64_t max_image_bytes,
                                         struct pe_parsed_image *out )
{
    uint32_t e_lfanew;
    uint64_t fh_off;
    uint16_t num_sections;
    uint16_t opt_hdr_size;
    uint64_t opt_off;
    uint16_t opt_magic;
    uint32_t section_alignment;
    uint32_t size_of_image;
    uint64_t image_size;
    uint64_t sec_table_off;
    uint64_t sec_table_bytes;
    uint32_t i;

    memset( out, 0, sizeof(*out) );

    /* DOS header must contain at least 64 bytes with 'MZ' magic and e_lfanew at offset 0x3c. */
    if (file_size < 64)
        return WEXPF_NOT_PE;
    if (buf[0] != 'M' || buf[1] != 'Z')
        return WEXPF_NOT_PE;

    e_lfanew = read_u32_le( buf + 0x3c );
    if ((uint64_t)e_lfanew > file_size || file_size - (uint64_t)e_lfanew < 24ULL)
        return WEXPF_NOT_PE;

    if (buf[e_lfanew] != 'P' || buf[e_lfanew + 1] != 'E' ||
        buf[e_lfanew + 2] != '\0' || buf[e_lfanew + 3] != '\0')
    {
        return WEXPF_NOT_PE;
    }

    fh_off = (uint64_t)e_lfanew + 4ULL;
    num_sections = read_u16_le( buf + fh_off + 2 );
    opt_hdr_size = read_u16_le( buf + fh_off + 16 );

    if (num_sections < 1 || num_sections > PE_MAX_SECTIONS)
        return WEXPF_NOT_PE;

    /* Optional header needs at least 60 bytes to reach SizeOfImage at offset 56. */
    if (opt_hdr_size < 60)
        return WEXPF_NOT_PE;

    opt_off = fh_off + 20ULL;
    if (opt_off > file_size || (uint64_t)opt_hdr_size > file_size - opt_off)
        return WEXPF_NOT_PE;

    opt_magic = read_u16_le( buf + opt_off + 0 );
    if (opt_magic != 0x010bu && opt_magic != 0x020bu)
        return WEXPF_NOT_PE;

    section_alignment = read_u32_le( buf + opt_off + 32 );
    size_of_image     = read_u32_le( buf + opt_off + 56 );

    if (size_of_image == 0 || (uint64_t)size_of_image > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
        return WEXPF_NOT_PE;

    image_size = round_up_u64( (uint64_t)size_of_image, (uint64_t)PE_PAGE_SIZE );
    if (image_size == 0 || image_size > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
        return WEXPF_NOT_PE;
    if (image_size > max_image_bytes)
        return WEXPF_TOO_LARGE;

    sec_table_off   = opt_off + (uint64_t)opt_hdr_size;
    sec_table_bytes = (uint64_t)num_sections * 40ULL;
    if (sec_table_off > file_size || sec_table_bytes > file_size - sec_table_off)
        return WEXPF_NOT_PE;

    if (section_alignment < PE_PAGE_SIZE || (section_alignment & (section_alignment - 1u)) != 0)
        return WEXPF_UNSUPPORTED;

    out->image_size    = image_size;
    out->section_count = 0;

    for (i = 0; i < num_sections; i++)
    {
        const uint8_t *sh = buf + sec_table_off + (uint64_t)i * 40ULL;
        uint32_t virtual_size    = read_u32_le( sh + 8 );
        uint32_t virtual_address = read_u32_le( sh + 12 );
        uint32_t raw_size        = read_u32_le( sh + 16 );
        uint32_t raw_ptr         = read_u32_le( sh + 20 );
        uint32_t characteristics = read_u32_le( sh + 36 );
        uint64_t eff_vsize;
        uint64_t align;
        uint64_t map_size;
        uint64_t map_size_page;
        uint64_t file_start;
        uint64_t raw_end_aligned;
        uint64_t file_len;
        struct pe_section_copy *sec;
        size_t c;

        if ((virtual_address & (PE_PAGE_SIZE - 1u)) != 0)
            return WEXPF_UNSUPPORTED;

        eff_vsize     = virtual_size ? (uint64_t)virtual_size : (uint64_t)raw_size;
        align         = (uint64_t)(section_alignment >= PE_PAGE_SIZE ? section_alignment : PE_PAGE_SIZE);
        map_size      = round_up_u64( eff_vsize, align );
        map_size_page = round_up_u64( map_size, (uint64_t)PE_PAGE_SIZE );

        if (map_size_page > image_size || (uint64_t)virtual_address > image_size - map_size_page)
            return WEXPF_NOT_PE;

        if ((characteristics & (IMAGE_SCN_MEM_SHARED | IMAGE_SCN_MEM_WRITE)) ==
            (IMAGE_SCN_MEM_SHARED | IMAGE_SCN_MEM_WRITE))
        {
            continue;
        }
        if (raw_ptr == 0 || raw_size == 0)
            continue;

        file_start      = (uint64_t)raw_ptr & ~PE_SECTOR_MASK;
        raw_end_aligned = round_up_u64( (uint64_t)raw_ptr + (uint64_t)raw_size, PE_SECTOR_ALIGN );
        if (raw_end_aligned < file_start)
            return WEXPF_NOT_PE;

        file_len = raw_end_aligned - file_start;
        if (file_len > map_size)
            file_len = map_size;

        /* Clip carefully to the end of the source file and image_size using 64-bit bounds checks. */
        if (file_start >= file_size)
            file_len = 0;
        else if (file_len > file_size - file_start)
            file_len = file_size - file_start;

        if ((uint64_t)virtual_address >= image_size)
            file_len = 0;
        else if (file_len > image_size - (uint64_t)virtual_address)
            file_len = image_size - (uint64_t)virtual_address;

        sec = &out->sections[out->section_count++];
        sec->index      = i;
        sec->va         = virtual_address;
        sec->file_start = file_start;
        sec->file_size  = file_len;
        for (c = 0; c < 8; c++)
        {
            uint8_t ch = sh[c];
            if (ch == '\0')
                break;
            sec->name[c] = (ch >= 0x20 && ch <= 0x7e) ? (char)ch : '?';
        }
        sec->name[c] = '\0';
    }

    return WEXPF_OK;
}

static int is_hex_img_filename( const char *name )
{
    size_t i;
    if (strlen( name ) != 68)
        return 0;
    for (i = 0; i < 64; i++)
    {
        char c = name[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0;
    }
    return strcmp( name + 64, ".img" ) == 0;
}

static void cleanup_stale_tmp_files( const struct daemon_ctx *ctx )
{
    DIR *dir;
    int dfd;
    struct dirent *de;
    int64_t now_sec = now_realtime_sec();
    uint32_t removed = 0;

    dir = opendir( ctx->cache_dir );
    if (!dir)
        return;

    rewinddir( dir );
    dfd = dirfd( dir );
    if (dfd < 0)
    {
        closedir( dir );
        return;
    }

    while ((de = readdir( dir )) != NULL)
    {
        struct stat st;
        if (strncmp( de->d_name, ".tmp.", 5 ) != 0)
            continue;
        if (fstatat( dfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW ) < 0)
            continue;
        if (!S_ISREG( st.st_mode ))
            continue;
        if (now_sec - (int64_t)st.st_mtim.tv_sec >= STALE_TMP_SECONDS)
        {
            if (unlinkat( dfd, de->d_name, 0 ) == 0)
                removed++;
        }
    }

    closedir( dir );
    if (removed > 0)
    {
        log_msg( ctx->foreground, LOG_INFO,
                 "removed %u stale temporary files from %s", removed, ctx->cache_dir );
    }
}

static int compare_prune_candidates( const void *a, const void *b )
{
    const struct prune_candidate *ca = (const struct prune_candidate *)a;
    const struct prune_candidate *cb = (const struct prune_candidate *)b;

    if (ca->mtime_sec < cb->mtime_sec)
        return -1;
    if (ca->mtime_sec > cb->mtime_sec)
        return 1;
    if (ca->mtime_nsec < cb->mtime_nsec)
        return -1;
    if (ca->mtime_nsec > cb->mtime_nsec)
        return 1;
    return strcmp( ca->name, cb->name );
}

static void prune_cache_if_needed( const struct daemon_ctx *ctx, const char *just_created_name,
                                   uint64_t max_cache_bytes, uint32_t max_entries )
{
    DIR *dir;
    int dfd;
    struct dirent *de;
    struct prune_candidate *cands = NULL;
    size_t cand_count = 0;
    size_t cand_cap = 0;
    uint64_t total_bytes = 0;
    uint64_t freed_bytes = 0;
    uint64_t entries = 0;
    uint32_t removed_files = 0;
    size_t i;

    dir = opendir( ctx->cache_dir );
    if (!dir)
        return;

    rewinddir( dir );
    dfd = dirfd( dir );
    if (dfd < 0)
    {
        closedir( dir );
        return;
    }

    while ((de = readdir( dir )) != NULL)
    {
        struct stat st;
        uint64_t disk_bytes;

        if (!is_hex_img_filename( de->d_name ))
            continue;
        if (fstatat( dfd, de->d_name, &st, AT_SYMLINK_NOFOLLOW ) < 0)
            continue;
        if (!S_ISREG( st.st_mode ))
            continue;

        /* images are sparse, so blocks alone would let near-empty entries pile up for free */
        disk_bytes = (uint64_t)st.st_blocks * 512ULL + ENTRY_OVERHEAD_BYTES;
        total_bytes += disk_bytes;
        entries++;

        if (just_created_name && strcmp( de->d_name, just_created_name ) == 0)
            continue;

        if (cand_count == cand_cap)
        {
            size_t new_cap = cand_cap ? cand_cap * 2 : 64;
            struct prune_candidate *grown = realloc( cands, new_cap * sizeof(*cands) );
            if (!grown)
                continue;
            cands = grown;
            cand_cap = new_cap;
        }

        snprintf( cands[cand_count].name, sizeof(cands[cand_count].name), "%s", de->d_name );
        cands[cand_count].disk_bytes = disk_bytes;
        cands[cand_count].mtime_sec  = (int64_t)st.st_mtim.tv_sec;
        cands[cand_count].mtime_nsec = (int64_t)st.st_mtim.tv_nsec;
        cand_count++;
    }

    if ((total_bytes > max_cache_bytes || entries > max_entries) && cand_count > 0)
    {
        qsort( cands, cand_count, sizeof(*cands), compare_prune_candidates );
        for (i = 0; i < cand_count && (total_bytes > max_cache_bytes || entries > max_entries); i++)
        {
            if (unlinkat( dfd, cands[i].name, 0 ) == 0)
            {
                entries--;
                if (total_bytes >= cands[i].disk_bytes)
                    total_bytes -= cands[i].disk_bytes;
                else
                    total_bytes = 0;
                freed_bytes += cands[i].disk_bytes;
                removed_files++;
            }
        }
        if (removed_files > 0)
        {
            log_msg( ctx->foreground, LOG_INFO,
                     "cache pruned: removed %u files (%" PRIu64 " bytes), remaining %" PRIu64 " bytes (cap %" PRIu64 " bytes)",
                     removed_files, freed_bytes, total_bytes, max_cache_bytes );
        }
    }

    free( cands );
    closedir( dir );
}

static int open_existing_cache_image( const char *cache_dir, const char *sha_hex,
                                      uint64_t expected_size, int touch_if_stale,
                                      int64_t *last_touch_inout )
{
    char img_path[PATH_MAX];
    int n;
    int fd;
    struct stat st;

    n = snprintf( img_path, sizeof(img_path), "%s/%s.img", cache_dir, sha_hex );
    if (n < 0 || (size_t)n >= sizeof(img_path))
        return -1;

    do
    {
        fd = open( img_path, O_RDONLY | O_CLOEXEC );
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return -1;

    if (fstat( fd, &st ) < 0 || !S_ISREG( st.st_mode ) || (uint64_t)st.st_size != expected_size)
    {
        close_no_eintr( fd );
        return -1;
    }

    if (touch_if_stale)
    {
        int64_t now_sec = now_realtime_sec();
        int64_t prev_touch = (last_touch_inout && *last_touch_inout > 0)
                             ? *last_touch_inout
                             : (int64_t)st.st_mtim.tv_sec;
        if (now_sec - prev_touch >= MTIME_TOUCH_INTERVAL_SEC)
        {
            if (futimens( fd, NULL ) == 0 && last_touch_inout)
                *last_touch_inout = now_sec;
        }
    }

    return fd;
}

/* Per peer uid limit on the expensive path (reading and hashing a file we have not seen): a client cannot make
 * the daemon read, hash and write an unbounded stream of distinct files.  g_rate_lock is a leaf lock. */
#define RATE_SLOTS 32
static struct rate_slot
{
    uid_t    uid;
    int64_t  window_start;
    uint32_t count;
    int      used;
} g_rate[RATE_SLOTS];
static pthread_mutex_t g_rate_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread uid_t  tl_peer_uid = (uid_t)-1;

static int build_rate_allowed( uid_t uid, uint32_t limit )
{
    int64_t now = now_monotonic_ms() / 1000;
    struct rate_slot *slot = NULL;
    int allowed = 1;
    int i;

    if (limit == 0)
        return 1;

    pthread_mutex_lock( &g_rate_lock );
    for (i = 0; i < RATE_SLOTS; i++)
    {
        if (g_rate[i].used && g_rate[i].uid == uid)
        {
            slot = &g_rate[i];
            break;
        }
    }
    if (!slot)
    {
        /* take a free slot, else the one whose window is oldest */
        for (i = 0; i < RATE_SLOTS; i++)
        {
            if (!g_rate[i].used)
            {
                slot = &g_rate[i];
                break;
            }
            if (!slot || g_rate[i].window_start < slot->window_start)
                slot = &g_rate[i];
        }
        slot->used = 1;
        slot->uid = uid;
        slot->window_start = now;
        slot->count = 0;
    }
    if (now - slot->window_start >= 60)
    {
        slot->window_start = now;
        slot->count = 0;
    }
    if (slot->count >= limit)
        allowed = 0;
    else
        slot->count++;
    pthread_mutex_unlock( &g_rate_lock );
    return allowed;
}

/* Sources on filesystems whose reads can block forever or are served by an unprivileged process are refused:
 * the daemon reads the whole file while holding the global build lock. */
static int fd_on_untrusted_fs( int fd )
{
    struct statfs sf;

    if (fstatfs( fd, &sf ) < 0)
        return 1;
    switch ((unsigned long)sf.f_type)
    {
    case WEXPF_FS_FUSE: case WEXPF_FS_NFS: case WEXPF_FS_SMB: case WEXPF_FS_CIFS: case WEXPF_FS_SMB2:
    case WEXPF_FS_9P: case WEXPF_FS_AFS: case WEXPF_FS_CEPH:
        return 1;
    default:
        return 0;
    }
}

static int fd_trusted( int fd )
{
    return !fd_on_untrusted_fs( fd );
}

static void fsync_dir( const char *path )
{
    int dfd = open( path, O_RDONLY | O_DIRECTORY | O_CLOEXEC );

    if (dfd < 0)
        return;
    (void)fsync( dfd );
    close_no_eintr( dfd );
}

static enum wexpf_status build_or_fetch_image( struct daemon_ctx *ctx, int client_fd,
                                               const struct file_identity *orig_id,
                                               const struct daemon_config *cfg,
                                               int *out_fd, uint64_t *out_image_size,
                                               char out_sha_hex[65] )
{
    uint8_t memo_sha[32];
    uint64_t memo_image_size = 0;
    int64_t last_touch = 0;
    uint8_t *buf = NULL;
    ssize_t bytes_read;
    struct stat st_after;
    struct file_identity after_id;
    sha256_ctx sctx;
    uint8_t digest[32];
    char sha_hex[65];
    struct pe_parsed_image parsed;
    enum wexpf_status st;
    int cached_fd = -1;
    int tmp_fd = -1;
    char tmp_path[PATH_MAX];
    char final_path[PATH_MAX];
    char final_name[72];
    uint64_t seq;
    int64_t start_ms;
    int64_t elapsed_ms;
    uint32_t i;
    int n;

    *out_fd = -1;
    *out_image_size = 0;
    out_sha_hex[0] = '\0';

    /* Step 3: Check identity memo first. */
    if (memo_lookup( orig_id, memo_sha, &memo_image_size, &last_touch ))
    {
        int64_t prev_touch = last_touch;
        sha256_hex( memo_sha, sha_hex );
        cached_fd = open_existing_cache_image( ctx->cache_dir, sha_hex, memo_image_size, 1, &last_touch );
        if (cached_fd >= 0)
        {
            if (last_touch != prev_touch)
                memo_update_touch( orig_id, last_touch );
            pthread_mutex_lock( &g_state_lock );
            g_counters.memo_hits++;
            g_counters.cache_hits++;
            pthread_mutex_unlock( &g_state_lock );

            *out_fd = cached_fd;
            *out_image_size = memo_image_size;
            memcpy( out_sha_hex, sha_hex, 65 );
            return WEXPF_OK;
        }
    }

    /*
     * Step 4: Serialize all builds with ONE global mutex (g_build_lock).
     * g_build_lock is the outermost lock; g_memo_lock and g_state_lock are leaf locks (see the lock list above).
     */
    pthread_mutex_lock( &g_build_lock );
    start_ms = now_monotonic_ms();

    /*
     * A request for the same file queued behind us while we were hashing it: the memo now has it, so do not
     * read and hash the whole file again (with many renderers asking for one large DLL that serialized
     * re-hash was long enough to hit the client timeout).  g_memo_lock and g_state_lock are leaf locks and
     * are never held while taking g_build_lock, so taking them here cannot deadlock.
     */
    if (memo_lookup( orig_id, memo_sha, &memo_image_size, &last_touch ))
    {
        sha256_hex( memo_sha, sha_hex );
        cached_fd = open_existing_cache_image( ctx->cache_dir, sha_hex, memo_image_size, 1, &last_touch );
        if (cached_fd >= 0)
        {
            pthread_mutex_unlock( &g_build_lock );
            pthread_mutex_lock( &g_state_lock );
            g_counters.memo_hits++;
            g_counters.cache_hits++;
            pthread_mutex_unlock( &g_state_lock );

            *out_fd = cached_fd;
            *out_image_size = memo_image_size;
            memcpy( out_sha_hex, sha_hex, 65 );
            return WEXPF_OK;
        }
    }

    if (!build_rate_allowed( tl_peer_uid, cfg->max_builds_per_min ))
    {
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_BUSY;
    }

    if (orig_id->size == 0 || orig_id->size > (uint64_t)SIZE_MAX)
    {
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_TOO_LARGE;
    }

    buf = malloc( (size_t)orig_id->size );
    if (!buf)
    {
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    bytes_read = pread_all( client_fd, buf, (size_t)orig_id->size, 0 );
    if (bytes_read < 0 || (uint64_t)bytes_read != orig_id->size)
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    sha256_init( &sctx );
    sha256_update( &sctx, buf, (size_t)orig_id->size );
    sha256_final( &sctx, digest );
    sha256_hex( digest, sha_hex );

    if (fstat( client_fd, &st_after ) < 0)
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }
    extract_identity( &st_after, &after_id );
    if (!identity_equal( orig_id, &after_id ))
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    st = parse_pe_image( buf, orig_id->size, cfg->max_image_bytes, &parsed );
    if (st != WEXPF_OK)
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return st;
    }

    {
        uint64_t data_bytes = 0;
        uint32_t k;

        for (k = 0; k < parsed.section_count; k++)
            data_bytes += parsed.sections[k].file_size;
        if (data_bytes == 0)
        {
            free( buf );
            pthread_mutex_unlock( &g_build_lock );
            return WEXPF_UNSUPPORTED;
        }
    }

    /* Re-check the cache file on disk while holding g_build_lock. */
    cached_fd = open_existing_cache_image( ctx->cache_dir, sha_hex, parsed.image_size, 1, NULL );
    if (cached_fd >= 0)
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );

        memo_store( orig_id, digest, parsed.image_size, now_realtime_sec() );
        pthread_mutex_lock( &g_state_lock );
        g_counters.cache_hits++;
        pthread_mutex_unlock( &g_state_lock );

        *out_fd = cached_fd;
        *out_image_size = parsed.image_size;
        memcpy( out_sha_hex, sha_hex, 65 );
        return WEXPF_OK;
    }

    seq = ++g_build_seq;
    n = snprintf( tmp_path, sizeof(tmp_path), "%s/.tmp.%s.%ld.%" PRIu64,
                  ctx->cache_dir, sha_hex, (long)getpid(), seq );
    if (n < 0 || (size_t)n >= sizeof(tmp_path))
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    n = snprintf( final_path, sizeof(final_path), "%s/%s.img", ctx->cache_dir, sha_hex );
    if (n < 0 || (size_t)n >= sizeof(final_path))
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    n = snprintf( final_name, sizeof(final_name), "%s.img", sha_hex );
    if (n < 0 || (size_t)n >= sizeof(final_name))
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    do
    {
        tmp_fd = open( tmp_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0644 );
    } while (tmp_fd < 0 && errno == EINTR);
    if (tmp_fd < 0)
    {
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    while (ftruncate( tmp_fd, (off_t)parsed.image_size ) < 0)
    {
        if (errno == EINTR)
            continue;
        close_no_eintr( tmp_fd );
        unlink( tmp_path );
        free( buf );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    for (i = 0; i < parsed.section_count; i++)
    {
        const struct pe_section_copy *sec = &parsed.sections[i];
        if (sec->file_size == 0)
            continue;
        if (sec->file_start > orig_id->size || sec->file_size > orig_id->size - sec->file_start)
        {
            close_no_eintr( tmp_fd );
            unlink( tmp_path );
            free( buf );
            pthread_mutex_unlock( &g_build_lock );
            return WEXPF_IO_ERROR;
        }
        if ((uint64_t)sec->va > parsed.image_size || sec->file_size > parsed.image_size - (uint64_t)sec->va)
        {
            close_no_eintr( tmp_fd );
            unlink( tmp_path );
            free( buf );
            pthread_mutex_unlock( &g_build_lock );
            return WEXPF_IO_ERROR;
        }
        if (pwrite_all( tmp_fd, buf + sec->file_start, (size_t)sec->file_size, (off_t)sec->va ) < 0)
        {
            close_no_eintr( tmp_fd );
            unlink( tmp_path );
            free( buf );
            pthread_mutex_unlock( &g_build_lock );
            return WEXPF_IO_ERROR;
        }
    }

    free( buf );
    buf = NULL;

    if (fchmod( tmp_fd, 0644 ) < 0)
    {
        close_no_eintr( tmp_fd );
        unlink( tmp_path );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    /* the entry is trusted by its name and length alone once it is published, so it must be on disk first */
    if (fsync( tmp_fd ) < 0)
    {
        close_no_eintr( tmp_fd );
        unlink( tmp_path );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    if (close_no_eintr( tmp_fd ) < 0)
    {
        unlink( tmp_path );
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }
    tmp_fd = -1;

    /* If <cache_dir>/<hex>.img already exists, keep the existing one and unlink the temp. */
    {
        struct stat st_existing;
        if (stat( final_path, &st_existing ) == 0 &&
            S_ISREG( st_existing.st_mode ) &&
            (uint64_t)st_existing.st_size == parsed.image_size)
        {
            unlink( tmp_path );
        }
        else if (rename( tmp_path, final_path ) < 0)
        {
            unlink( tmp_path );
            pthread_mutex_unlock( &g_build_lock );
            return WEXPF_IO_ERROR;
        }
    }

    fsync_dir( ctx->cache_dir );

    cached_fd = open_existing_cache_image( ctx->cache_dir, sha_hex, parsed.image_size, 0, NULL );
    if (cached_fd < 0)
    {
        pthread_mutex_unlock( &g_build_lock );
        return WEXPF_IO_ERROR;
    }

    prune_cache_if_needed( ctx, final_name, cfg->max_cache_bytes, cfg->max_entries );
    elapsed_ms = now_monotonic_ms() - start_ms;
    if (elapsed_ms < 0)
        elapsed_ms = 0;

    pthread_mutex_unlock( &g_build_lock );

    memo_store( orig_id, digest, parsed.image_size, now_realtime_sec() );

    pthread_mutex_lock( &g_state_lock );
    g_counters.builds++;
    g_counters.bytes_built += parsed.image_size;
    pthread_mutex_unlock( &g_state_lock );

    log_msg( ctx->foreground, LOG_INFO,
             "built flat image sha256=%s source_size=%" PRIu64 " image_size=%" PRIu64 " duration_ms=%" PRId64,
             sha_hex, orig_id->size, parsed.image_size, elapsed_ms );

    *out_fd = cached_fd;
    *out_image_size = parsed.image_size;
    memcpy( out_sha_hex, sha_hex, 65 );
    return WEXPF_OK;
}

static void set_socket_timeouts( int conn_fd )
{
    struct timeval tv;
    tv.tv_sec  = SOCKET_TIMEOUT_SECONDS;
    tv.tv_usec = 0;
    if (setsockopt( conn_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) ) < 0)
    {
        /* Non-fatal: proceed even if setsockopt fails on exotic kernels. */
    }
    if (setsockopt( conn_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv) ) < 0)
    {
    }
}

static ssize_t send_reply( int conn_fd, enum wexpf_status status,
                             uint64_t image_size, const char *sha_hex, int result_fd )
{
    struct wexpf_reply reply;
    struct iovec iov;
    struct msghdr msg;
    union
    {
        struct cmsghdr cm;
        char           buf[CMSG_SPACE( sizeof(int) )];
    } cmsg_buf;
    ssize_t s;

    memset( &reply, 0, sizeof(reply) );
    reply.magic   = WEXPF_MAGIC;
    reply.version = WEXPF_VERSION;
    reply.status  = (int32_t)status;
    if (status == WEXPF_OK && result_fd >= 0)
    {
        reply.layout     = WEXPF_LAYOUT_FLAT;
        reply.image_size = image_size;
        if (sha_hex)
        {
            memcpy( reply.sha256, sha_hex, 64 );
            reply.sha256[64] = '\0';
        }
    }

    memset( &iov, 0, sizeof(iov) );
    iov.iov_base = &reply;
    iov.iov_len  = sizeof(reply);

    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov    = &iov;
    msg.msg_iovlen = 1;

    if (status == WEXPF_OK && result_fd >= 0)
    {
        struct cmsghdr *cmsg;
        memset( &cmsg_buf, 0, sizeof(cmsg_buf) );
        msg.msg_control    = cmsg_buf.buf;
        msg.msg_controllen = sizeof(cmsg_buf.buf);

        cmsg             = CMSG_FIRSTHDR( &msg );
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN( sizeof(int) );
        memcpy( CMSG_DATA( cmsg ), &result_fd, sizeof(int) );
    }

    do
    {
        s = sendmsg( conn_fd, &msg, MSG_NOSIGNAL );
    } while (s < 0 && errno == EINTR);
    return s;
}

static ssize_t recv_request_datagram( int conn_fd, struct wexpf_request *req_out,
                                      int *msg_flags_out, int *client_fd_out,
                                      size_t *fd_count_out )
{
    uint8_t raw_buf[sizeof(struct wexpf_request) + 64];
    struct iovec iov;
    struct msghdr msg;
    union
    {
        struct cmsghdr cm;
        char           buf[CMSG_SPACE( sizeof(int) )];
    } cmsg_buf;
    struct cmsghdr *cmsg;
    ssize_t n;
    size_t fd_count = 0;
    int kept_fd = -1;

    memset( req_out, 0, sizeof(*req_out) );
    *msg_flags_out = 0;
    *client_fd_out = -1;
    *fd_count_out  = 0;

    memset( &iov, 0, sizeof(iov) );
    iov.iov_base = raw_buf;
    iov.iov_len  = sizeof(raw_buf);

    memset( &cmsg_buf, 0, sizeof(cmsg_buf) );
    memset( &msg, 0, sizeof(msg) );
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsg_buf.buf;
    msg.msg_controllen = sizeof(cmsg_buf.buf);

    do
    {
        n = recvmsg( conn_fd, &msg, MSG_CMSG_CLOEXEC );
    } while (n < 0 && errno == EINTR);

    if (n < 0)
        return -1;

    *msg_flags_out = msg.msg_flags;

    for (cmsg = CMSG_FIRSTHDR( &msg ); cmsg != NULL; cmsg = CMSG_NXTHDR( &msg, cmsg ))
    {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN( 0 ))
        {
            size_t payload = (size_t)(cmsg->cmsg_len - CMSG_LEN( 0 ));
            size_t count = payload / sizeof(int);
            const unsigned char *data = CMSG_DATA( cmsg );
            size_t i;

            for (i = 0; i < count; i++)
            {
                int fd;
                memcpy( &fd, data + i * sizeof(int), sizeof(int) );
                if (fd >= 0)
                {
                    if (fd_count == 0)
                        kept_fd = fd;
                    else
                        close_no_eintr( fd );
                    fd_count++;
                }
            }
        }
    }

    if (fd_count != 1 && kept_fd >= 0)
    {
        close_no_eintr( kept_fd );
        kept_fd = -1;
    }

    if ((size_t)n >= sizeof(*req_out))
        memcpy( req_out, raw_buf, sizeof(*req_out) );

    *client_fd_out = kept_fd;
    *fd_count_out  = fd_count;
    return n;
}

static void reject_request( const struct daemon_ctx *ctx, int conn_fd,
                            enum wexpf_status status, const char *reason )
{
    pthread_mutex_lock( &g_state_lock );
    g_counters.rejects++;
    pthread_mutex_unlock( &g_state_lock );

    log_msg( ctx->foreground, LOG_NOTICE,
             "rejected request with %s: %s", status_name( status ), reason );
    send_reply( conn_fd, status, 0, NULL, -1 );
}

static void handle_connection( struct daemon_ctx *ctx, int conn_fd )
{
    struct daemon_config cfg;
    struct wexpf_request req;
    int msg_flags = 0;
    int client_fd = -1;
    size_t fd_count = 0;
    ssize_t n;
    struct stat st;
    int fl;
    struct file_identity id;
    int result_fd = -1;
    uint64_t image_size = 0;
    char sha_hex[65];
    enum wexpf_status status;
    pid_t peer_pid = 0;

    int64_t t_start = now_monotonic_ms();

    set_socket_timeouts( conn_fd );
    get_config_snapshot( ctx, &cfg );

    pthread_mutex_lock( &g_state_lock );
    g_counters.requests++;
    pthread_mutex_unlock( &g_state_lock );

    n = recv_request_datagram( conn_fd, &req, &msg_flags, &client_fd, &fd_count );
    if (n < 0)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "recvmsg failed or timed out" );
        goto out;
    }

    if (!cfg.enabled)
    {
        reject_request( ctx, conn_fd, WEXPF_DISABLED, "service disabled by configuration" );
        goto out;
    }

    if ((size_t)n < sizeof(struct wexpf_request))
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "datagram shorter than wexpf_request" );
        goto out;
    }
    if ((msg_flags & MSG_CTRUNC) != 0 || (msg_flags & MSG_TRUNC) != 0)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "control or payload message truncated" );
        goto out;
    }
    if (req.magic != WEXPF_MAGIC || req.version != WEXPF_VERSION ||
        req.op != WEXPF_OP_GET_IMAGE || req.flags != 0)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "invalid request header fields" );
        goto out;
    }
    if (fd_count != 1 || client_fd < 0)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "expected exactly 1 SCM_RIGHTS descriptor" );
        goto out;
    }

    if (fstat( client_fd, &st ) < 0 || !S_ISREG( st.st_mode ))
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "descriptor is not a regular file" );
        goto out;
    }

    fl = fcntl( client_fd, F_GETFL );
    if (fl < 0 || (fl & O_ACCMODE) == O_WRONLY)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "descriptor is not open for reading" );
        goto out;
    }

    if (st.st_size <= 0)
    {
        reject_request( ctx, conn_fd, WEXPF_BAD_REQUEST, "source file is empty" );
        goto out;
    }

    if ((uint64_t)st.st_size > cfg.max_image_bytes ||
        (uint64_t)st.st_size > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
    {
        reject_request( ctx, conn_fd, WEXPF_TOO_LARGE, "source file exceeds MaxImageMB" );
        goto out;
    }

    if (cfg.reject_network_fs && fd_on_untrusted_fs( client_fd ))
    {
        reject_request( ctx, conn_fd, WEXPF_UNSUPPORTED, "source is on a network, FUSE or other untrusted filesystem" );
        goto out;
    }

    {
        struct ucred cred;
        socklen_t cred_len = sizeof(cred);

        if (getsockopt( conn_fd, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len ) == 0)
        {
            tl_peer_uid = cred.uid;
            peer_pid = cred.pid;
        }
        else tl_peer_uid = (uid_t)-1;
    }

    extract_identity( &st, &id );
    status = build_or_fetch_image( ctx, client_fd, &id, &cfg, &result_fd, &image_size, sha_hex );
    if (status != WEXPF_OK)
    {
        reject_request( ctx, conn_fd, status, "failed to build or validate flat PE image" );
        goto out;
    }

    if (send_reply( conn_fd, WEXPF_OK, image_size, sha_hex, result_fd ) < 0)
        log_msg( ctx->foreground, LOG_WARNING, "reply for sha256=%.12s failed: %s", sha_hex, strerror( errno ) );
    else if (getenv( "WEXPF_VERBOSE" ))
        log_msg( ctx->foreground, LOG_INFO, "served sha256=%.12s source_size=%" PRIu64 " image_size=%" PRIu64 " in %" PRId64 " ms",
                 sha_hex, id.size, image_size, now_monotonic_ms() - t_start );

    if (peer_pid > 0)
        trace_image( peer_pid, client_fd, &st, fd_trusted );

out:
    if (result_fd >= 0)
        close_no_eintr( result_fd );
    if (client_fd >= 0)
        close_no_eintr( client_fd );
    close_no_eintr( conn_fd );
    write_status_file( ctx, 0 );
}

static void handle_busy_in_accept_thread( struct daemon_ctx *ctx, int conn_fd )
{
    struct timeval tv;

    /* Do not read the request here: a client that connects and stays silent would stall every other accept.
     * The queued request, and the descriptor it carries, is dropped when the connection is closed. */
    tv.tv_sec  = 0;
    tv.tv_usec = 100 * 1000;
    (void)setsockopt( conn_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv) );

    pthread_mutex_lock( &g_state_lock );
    g_counters.requests++;
    pthread_mutex_unlock( &g_state_lock );

    reject_request( ctx, conn_fd, WEXPF_BUSY, "MaxConcurrent limit reached" );
    close_no_eintr( conn_fd );
    write_status_file( ctx, 0 );
}

static void *worker_thread_main( void *arg )
{
    struct worker_arg *warg = (struct worker_arg *)arg;
    struct daemon_ctx *ctx  = warg->ctx;
    int conn_fd             = warg->conn_fd;

    free( warg );
    handle_connection( ctx, conn_fd );

    pthread_mutex_lock( &g_state_lock );
    if (g_counters.active_workers > 0)
        g_counters.active_workers--;
    g_counters.last_activity_mono = now_monotonic_sec();
    if (g_counters.active_workers == 0)
        pthread_cond_broadcast( &g_state_cond );
    pthread_mutex_unlock( &g_state_lock );

    return NULL;
}

static int setup_listening_socket( struct daemon_ctx *ctx )
{
    const char *listen_fds_env = getenv( "LISTEN_FDS" );
    const char *listen_pid_env = getenv( "LISTEN_PID" );
    struct sockaddr_un addr;
    int fd;
    int flags;

    if (listen_fds_env && listen_pid_env)
    {
        char *end1 = NULL;
        char *end2 = NULL;
        long fds = strtol( listen_fds_env, &end1, 10 );
        long pid = strtol( listen_pid_env, &end2, 10 );

        if (end1 && *end1 == '\0' && end2 && *end2 == '\0' &&
            fds == 1 && pid == (long)getpid())
        {
            fd = SD_LISTEN_FDS_START;
            flags = fcntl( fd, F_GETFD );
            if (flags < 0 || fcntl( fd, F_SETFD, flags | FD_CLOEXEC ) < 0)
                return -1;
            ctx->socket_activated = 1;
            return fd;
        }
    }

    ctx->socket_activated = 0;
    if (strlen( ctx->socket_path ) >= sizeof(addr.sun_path))
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (mkdir_p( ctx->runtime_dir, 0755 ) < 0)
        return -1;

    if (unlink( ctx->socket_path ) < 0 && errno != ENOENT)
        return -1;

    fd = socket( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0 );
    if (fd < 0)
        return -1;

    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    snprintf( addr.sun_path, sizeof(addr.sun_path), "%s", ctx->socket_path );

    if (bind( fd, (struct sockaddr *)&addr, sizeof(addr) ) < 0)
    {
        close_no_eintr( fd );
        return -1;
    }

    if (chmod( ctx->socket_path, 0666 ) < 0)
    {
        close_no_eintr( fd );
        unlink( ctx->socket_path );
        return -1;
    }

    if (listen( fd, 128 ) < 0)
    {
        close_no_eintr( fd );
        unlink( ctx->socket_path );
        return -1;
    }

    return fd;
}

static int install_signal_handlers( void )
{
    struct sigaction sa;

    memset( &sa, 0, sizeof(sa) );
    sigemptyset( &sa.sa_mask );
    sa.sa_handler = SIG_IGN;
    if (sigaction( SIGPIPE, &sa, NULL ) < 0)
        return -1;

    memset( &sa, 0, sizeof(sa) );
    sigemptyset( &sa.sa_mask );
    sa.sa_handler = on_sig_stop;
    if (sigaction( SIGTERM, &sa, NULL ) < 0)
        return -1;
    if (sigaction( SIGINT, &sa, NULL ) < 0)
        return -1;

    memset( &sa, 0, sizeof(sa) );
    sigemptyset( &sa.sa_mask );
    sa.sa_handler = on_sig_reload;
    if (sigaction( SIGHUP, &sa, NULL ) < 0)
        return -1;

    return 0;
}

static int run_selftest_pe( const char *pe_path )
{
    int fd;
    struct stat st;
    uint8_t *buf;
    ssize_t n;
    struct pe_parsed_image parsed;
    enum wexpf_status status;
    uint32_t i;

    do
    {
        fd = open( pe_path, O_RDONLY | O_CLOEXEC );
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
    {
        fprintf( stderr, "selftest-pe: open(%s) failed: %s\n", pe_path, strerror( errno ) );
        return 1;
    }

    if (fstat( fd, &st ) < 0 || !S_ISREG( st.st_mode ) || st.st_size <= 0 ||
        (uint64_t)st.st_size > (uint64_t)WEXPF_MAX_IMAGE_SIZE)
    {
        fprintf( stderr, "selftest-pe: invalid file size or type for %s\n", pe_path );
        close_no_eintr( fd );
        return 1;
    }

    buf = malloc( (size_t)st.st_size );
    if (!buf)
    {
        fprintf( stderr, "selftest-pe: out of memory\n" );
        close_no_eintr( fd );
        return 1;
    }

    n = pread_all( fd, buf, (size_t)st.st_size, 0 );
    close_no_eintr( fd );
    if (n < 0 || (uint64_t)n != (uint64_t)st.st_size)
    {
        fprintf( stderr, "selftest-pe: short read on %s\n", pe_path );
        free( buf );
        return 1;
    }

    status = parse_pe_image( buf, (uint64_t)st.st_size, (uint64_t)WEXPF_MAX_IMAGE_SIZE, &parsed );
    free( buf );
    if (status != WEXPF_OK)
    {
        fprintf( stderr, "selftest-pe: parse failed with %s (%d)\n", status_name( status ), (int)status );
        return 1;
    }

    for (i = 0; i < parsed.section_count; i++)
    {
        const struct pe_section_copy *sec = &parsed.sections[i];
        printf( "section %2u %-8s va=%08x file_start=%08" PRIx64 " size=%08" PRIx64 "\n",
                sec->index, sec->name, sec->va, sec->file_start, sec->file_size );
    }
    printf( "image_size=%" PRIu64 " (0x%08" PRIx64 ")\n", parsed.image_size, parsed.image_size );
    return 0;
}

/* --ksm-tuner: the adaptive KSM tuner loop.  Needs write access to /sys/kernel/mm/ksm, so it runs as its own
 * unit (wexksmtuned.service), not as the unprivileged cache daemon. */
static int run_ksm_tuner( int foreground )
{
    const char *dir = getenv( "WEXPF_KSM_DIR" );
    const char *meminfo = getenv( "WEXPF_MEMINFO" );
    const char *iv = getenv( "WEXPF_KSM_INTERVAL_MS" );
    long interval_ms = iv && atol( iv ) > 0 ? atol( iv ) : 5000;
    long max_steps = getenv( "WEXPF_KSM_STEPS" ) ? atol( getenv( "WEXPF_KSM_STEPS" ) ) : 0; /* tests */
    struct ksm_cfg cfg;
    struct ksm_state st, last;
    struct ksm_sample smp;
    int have_state = 0, warned = 0;
    struct timespec ts;

    dir = dir && *dir ? dir : "/sys/kernel/mm/ksm";
    meminfo = meminfo && *meminfo ? meminfo : "/proc/meminfo";
    ksm_cfg_defaults( &cfg );
    memset( &last, 0, sizeof(last) );
    last.run = -1;
    openlog( "wexksmtuned", LOG_PID | LOG_NDELAY, LOG_DAEMON );
    if (install_signal_handlers() < 0) return 1;

    while (!g_sig_stop)
    {
        if (ksm_read_sample( meminfo, dir, &smp ) == 0)
        {
            if (!have_state) { ksm_state_init( &cfg, &st, &smp ); have_state = 1; }
            else ksm_tune_step( &cfg, &st, &smp );
            if (ksm_apply( dir, &cfg, &st, &last ) < 0 && !warned)
            {
                log_msg( foreground, LOG_WARNING, "cannot write %s: %s", dir, strerror( errno ) );
                warned = 1;
            }
        }
        else if (!warned)
        {
            log_msg( foreground, LOG_WARNING, "cannot read %s", meminfo );
            warned = 1;
        }
        if (max_steps && --max_steps == 0) break;
        ts.tv_sec = interval_ms / 1000;
        ts.tv_nsec = (interval_ms % 1000) * 1000000L;
        nanosleep( &ts, NULL );
    }
    closelog();
    return 0;
}

int main( int argc, char **argv )
{
    struct daemon_ctx ctx;
    int arg_ksm = 0;
    const char *env_trace;
    const char *env_fg;
    const char *env_sock;
    const char *env_cache;
    const char *env_cfg;
    const char *selftest_path = NULL;
    int arg_fg = 0;
    int listen_fd = -1;
    pthread_attr_t detached_attr;
    int i;
    int n;

    for (i = 1; i < argc; i++)
    {
        if (strcmp( argv[i], "--foreground" ) == 0)
        {
            arg_fg = 1;
        }
        else if (strcmp( argv[i], "--ksm-tuner" ) == 0)
        {
            arg_ksm = 1;
        }
        else if (strcmp( argv[i], "--selftest-pe" ) == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf( stderr, "usage: %s --selftest-pe <file>\n", argv[0] );
                return 2;
            }
            selftest_path = argv[++i];
        }
        else if (strncmp( argv[i], "--selftest-pe=", 14 ) == 0)
        {
            selftest_path = argv[i] + 14;
        }
        else
        {
            fprintf( stderr, "unknown argument: %s\n", argv[i] );
            return 2;
        }
    }

    if (selftest_path)
        return run_selftest_pe( selftest_path );

    memset( &ctx, 0, sizeof(ctx) );
    env_fg = getenv( "WEXPF_FOREGROUND" );
    ctx.foreground = arg_fg || (env_fg && strcmp( env_fg, "1" ) == 0);
    if (arg_ksm) return run_ksm_tuner( ctx.foreground );

    openlog( "wexprefetchd", LOG_PID | LOG_NDELAY, LOG_DAEMON );

    env_sock  = getenv( "WEXPF_SOCKET" );
    env_cache = getenv( "WEXPF_CACHE_DIR" );
    env_cfg   = getenv( "WEXPF_CONFIG" );

    n = snprintf( ctx.socket_path, sizeof(ctx.socket_path), "%s",
                  (env_sock && env_sock[0]) ? env_sock : WEXPF_SOCKET_PATH );
    if (n < 0 || (size_t)n >= sizeof(ctx.socket_path))
    {
        log_msg( ctx.foreground, LOG_ERR, "socket path too long" );
        closelog();
        return 1;
    }

    if (derive_parent_dir( ctx.socket_path, ctx.runtime_dir, sizeof(ctx.runtime_dir) ) < 0)
    {
        log_msg( ctx.foreground, LOG_ERR, "failed to derive runtime directory for %s", ctx.socket_path );
        closelog();
        return 1;
    }

    n = snprintf( ctx.status_path, sizeof(ctx.status_path), "%s/prefetch.status", ctx.runtime_dir );
    if (n < 0 || (size_t)n >= sizeof(ctx.status_path))
    {
        log_msg( ctx.foreground, LOG_ERR, "status file path too long" );
        closelog();
        return 1;
    }

    n = snprintf( ctx.cache_dir, sizeof(ctx.cache_dir), "%s",
                  (env_cache && env_cache[0]) ? env_cache : DEFAULT_CACHE_DIR );
    if (n < 0 || (size_t)n >= sizeof(ctx.cache_dir))
    {
        log_msg( ctx.foreground, LOG_ERR, "cache directory path too long" );
        closelog();
        return 1;
    }

    n = snprintf( ctx.config_path, sizeof(ctx.config_path), "%s",
                  (env_cfg && env_cfg[0]) ? env_cfg : DEFAULT_CONFIG_PATH );
    if (n < 0 || (size_t)n >= sizeof(ctx.config_path))
    {
        log_msg( ctx.foreground, LOG_ERR, "config path too long" );
        closelog();
        return 1;
    }

    if (install_signal_handlers() < 0)
    {
        log_msg( ctx.foreground, LOG_ERR, "failed to install signal handlers: %s", strerror( errno ) );
        closelog();
        return 1;
    }

    load_config( &ctx, 0 );

    if (mkdir_p( ctx.cache_dir, 0755 ) < 0)
    {
        log_msg( ctx.foreground, LOG_ERR, "failed to create cache dir %s: %s",
                 ctx.cache_dir, strerror( errno ) );
        closelog();
        return 1;
    }

    if (mkdir_p( ctx.runtime_dir, 0755 ) < 0)
    {
        log_msg( ctx.foreground, LOG_WARNING, "failed to create runtime dir %s: %s",
                 ctx.runtime_dir, strerror( errno ) );
    }

    cleanup_stale_tmp_files( &ctx );

    env_trace = getenv( "WEXPF_TRACE_DIR" );
    if (trace_init( env_trace && *env_trace ? env_trace : DEFAULT_TRACE_DIR ) < 0)
    {
        log_msg( ctx.foreground, LOG_WARNING, "no trace directory (%s): prefetch traces and readahead off",
                 strerror( errno ) );
        trace_configure( 0, 0 );
        ctx.cfg.record_traces = ctx.cfg.readahead = 0;
    }

    listen_fd = setup_listening_socket( &ctx );
    if (listen_fd < 0)
    {
        log_msg( ctx.foreground, LOG_ERR, "failed to set up listening socket %s: %s",
                 ctx.socket_path, strerror( errno ) );
        closelog();
        return 1;
    }

    if (pthread_attr_init( &detached_attr ) != 0 ||
        pthread_attr_setdetachstate( &detached_attr, PTHREAD_CREATE_DETACHED ) != 0 ||
        pthread_attr_setstacksize( &detached_attr, 256 * 1024 ) != 0)
    {
        log_msg( ctx.foreground, LOG_ERR, "failed to initialize pthread attributes" );
        close_no_eintr( listen_fd );
        closelog();
        return 1;
    }

    g_counters.last_activity_mono = now_monotonic_sec();
    write_status_file( &ctx, 1 );

    log_msg( ctx.foreground, LOG_INFO,
             "started (socket=%s activated=%d cache_dir=%s Enabled=%d MaxCacheMB=%" PRIu64
             " MaxConcurrent=%u MaxImageMB=%" PRIu64 ")",
             ctx.socket_path, ctx.socket_activated, ctx.cache_dir,
             ctx.cfg.enabled,
             (uint64_t)(ctx.cfg.max_cache_bytes / (1024ULL * 1024ULL)),
             ctx.cfg.max_concurrent,
             (uint64_t)(ctx.cfg.max_image_bytes / (1024ULL * 1024ULL)) );

    while (!g_sig_stop)
    {
        struct pollfd pfd;
        int pr;

        if (g_sig_reload)
        {
            g_sig_reload = 0;
            load_config( &ctx, 1 );
        }
        trace_tick();

        if (ctx.socket_activated)
        {
            int64_t now_mono = now_monotonic_sec();
            int idle_timeout = 0;

            pthread_mutex_lock( &g_state_lock );
            if (g_counters.active_workers == 0 &&
                (now_mono - g_counters.last_activity_mono) >= IDLE_EXIT_SECONDS)
            {
                idle_timeout = 1;
            }
            pthread_mutex_unlock( &g_state_lock );

            if (idle_timeout)
            {
                log_msg( ctx.foreground, LOG_INFO,
                         "idle for %d seconds under socket activation, exiting",
                         IDLE_EXIT_SECONDS );
                break;
            }
        }

        pfd.fd      = listen_fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;

        pr = poll( &pfd, 1, 1000 );
        if (pr < 0)
        {
            if (errno == EINTR)
                continue;
            log_msg( ctx.foreground, LOG_ERR, "poll failed: %s", strerror( errno ) );
            break;
        }

        if (pr == 0 || (pfd.revents & POLLIN) == 0)
            continue;

        {
            int conn_fd;
            int busy = 0;
            struct worker_arg *warg;
            pthread_t tid;

            do
            {
                conn_fd = accept4( listen_fd, NULL, NULL, SOCK_CLOEXEC );
            } while (conn_fd < 0 && errno == EINTR);

            if (conn_fd < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                log_msg( ctx.foreground, LOG_WARNING, "accept4 failed: %s", strerror( errno ) );
                continue;
            }

            pthread_mutex_lock( &g_state_lock );
            g_counters.last_activity_mono = now_monotonic_sec();
            if (g_counters.active_workers >= ctx.cfg.max_concurrent)
                busy = 1;
            else
                g_counters.active_workers++;
            pthread_mutex_unlock( &g_state_lock );

            if (busy)
            {
                handle_busy_in_accept_thread( &ctx, conn_fd );
                continue;
            }

            warg = malloc( sizeof(*warg) );
            if (!warg)
            {
                pthread_mutex_lock( &g_state_lock );
                if (g_counters.active_workers > 0)
                    g_counters.active_workers--;
                if (g_counters.active_workers == 0)
                    pthread_cond_broadcast( &g_state_cond );
                pthread_mutex_unlock( &g_state_lock );

                handle_busy_in_accept_thread( &ctx, conn_fd );
                continue;
            }

            warg->ctx     = &ctx;
            warg->conn_fd = conn_fd;

            if (pthread_create( &tid, &detached_attr, worker_thread_main, warg ) != 0)
            {
                free( warg );
                pthread_mutex_lock( &g_state_lock );
                if (g_counters.active_workers > 0)
                    g_counters.active_workers--;
                if (g_counters.active_workers == 0)
                    pthread_cond_broadcast( &g_state_cond );
                pthread_mutex_unlock( &g_state_lock );

                handle_busy_in_accept_thread( &ctx, conn_fd );
                continue;
            }
        }
    }

    close_no_eintr( listen_fd );
    if (!ctx.socket_activated)
        unlink( ctx.socket_path );

    pthread_mutex_lock( &g_state_lock );
    while (g_counters.active_workers > 0)
        pthread_cond_wait( &g_state_cond, &g_state_lock );
    pthread_mutex_unlock( &g_state_lock );

    trace_flush_all();
    write_status_file( &ctx, 1 );
    pthread_attr_destroy( &detached_attr );
    log_msg( ctx.foreground, LOG_INFO, "shutting down cleanly" );
    closelog();
    return 0;
}
