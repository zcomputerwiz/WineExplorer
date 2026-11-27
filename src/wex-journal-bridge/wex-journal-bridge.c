/*
 * wex-journal-bridge: selected systemd journal entries to the Event Log, and the Event Log's records to the journal
 * (docs/EVENTLOG_JOURNAL.md, J1)
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

/* Runs as its own account (wexjournal, in systemd-journal only), from wex-journal-bridge.service with its socket
 * passed by systemd (wex-journal-bridge.socket) or, for tests, bound by itself (--socket PATH). It admits one
 * connection at a time, from session 0's account only (SO_PEERCRED uid): session 0's EventLog service. It reads the
 * system journal (users' journals never), maps an entry only when the fields journald itself sets (the underscore
 * fields) match a rule, rate-limits, and sends the event; the service's own records come back and are written to the
 * journal. Wire format: wine/wex_journal.h. */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>

#include "wex_journal.h"
#include "wex_peer.h"

/* systemd's catalog (Debian 13, systemd 257) */
#define MID_JOB_FAILED        "be02cf6855d2428ba40df7e9d022f03d"  /* a start job has failed */
#define MID_UNIT_FAILED       "d9b373ed55a64feb8242e02dbe79a49c"  /* unit failed */
#define MID_RESTART_SCHEDULED "5eb03494b6584870a536b337290809b3"  /* automatic restart scheduled */
#define MID_PROCESS_EXIT      "98e322203f7a4ed290d09fe03c09fe15"  /* unit process exited */
#define MID_COREDUMP          "fc2e22bc6ee647b6b90729ab34a250b1"  /* a process dumped core */

#define EVT_ERROR       1
#define EVT_WARNING     2
#define EVT_INFORMATION 4

#define RATE_PER_SECOND 10.0
#define RATE_BURST      50.0
#define RATE_TOTAL      100.0   /* all events to the service, per second */
#define LOOKAHEAD_USEC  (2 * 1000000ULL)
#define OUT_MAX         (256 * 1024)  /* bytes queued for the service before the journal is not read */
#define MAX_UNITS       64
#define CONFIG_PATH     "/etc/wineexplorer/journal-events.conf"

static int foreground;
static int first_start_now;  /* --first-start now: without a stored cursor, from now instead of from this boot */
static uid_t session0_uid = (uid_t)-1, own_uid, coredump_uid = (uid_t)-1;
static int security_to_journal, coredumps_all;
static sd_bus *bus;

struct unit_map { char unit[128]; char source[WEXJ_MAX_SOURCE + 1]; };
static struct unit_map units[MAX_UNITS];
static int unit_count;

static void log_msg( int prio, const char *fmt, ... )
{
    va_list args;
    char buf[512];

    va_start( args, fmt );
    vsnprintf( buf, sizeof(buf), fmt, args );
    va_end( args );
    if (foreground) fprintf( stderr, "wex-journal-bridge: %s\n", buf );
    else sd_journal_print( prio, "%s", buf );
}

static uint64_t now_usec(void)
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ---- configuration (root's file, nobody else may write it) ---- */

static void add_unit( const char *unit, const char *source )
{
    if (unit_count >= MAX_UNITS || strlen( unit ) >= sizeof(units[0].unit) || !*source ||
        strlen( source ) > WEXJ_MAX_SOURCE || strpbrk( source, "\\/" ))
        return;
    strcpy( units[unit_count].unit, unit );
    strcpy( units[unit_count].source, source );
    unit_count++;
}

static void load_config( const char *path )
{
    char line[512], *p, *src;
    struct stat st;
    FILE *f;
    int fd;

    /* defaults: WineExplorer's own session 0 units, and the network */
    add_unit( "wex-session0.service", "WineExplorer Session 0" );
    add_unit( "wex-session0-services.service", "WineExplorer Session 0" );
    add_unit( "NetworkManager.service", "NetworkManager" );
    if ((fd = open( path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC )) == -1) return;
    if (fstat( fd, &st ) || st.st_uid != 0 || (st.st_mode & 022) || !S_ISREG( st.st_mode ))
    {
        log_msg( LOG_WARNING, "%s is not root's or may be written by others: ignored", path );
        close( fd );
        return;
    }
    if (!(f = fdopen( fd, "r" )))
    {
        close( fd );
        return;
    }
    while (fgets( line, sizeof(line), f ))
    {
        if ((p = strpbrk( line, "\r\n" ))) *p = 0;
        if (line[0] == '#' || !line[0]) continue;
        if (!strcmp( line, "security_to_journal=yes" )) security_to_journal = 1;
        else if (!strcmp( line, "coredumps_to_application=all" )) coredumps_all = 1;
        else if (!strncmp( line, "unit=", 5 ) && (src = strstr( line, " source=" )))
        {
            *src = 0;
            add_unit( line + 5, src + 8 );
        }
    }
    fclose( f );
}

/* ---- text ---- */

/* control characters and ANSI escape sequences become spaces (R6) */
static void sanitize( char *s )
{
    unsigned char *p = (unsigned char *)s;

    while (*p)
    {
        if (*p == 0x1b)
        {
            unsigned char *q = p + 1;
            if (*q == '[')
            {
                q++;
                while (*q && !(*q >= 0x40 && *q <= 0x7e)) q++;
                if (*q) q++;
            }
            else if (*q) q++;
            while (p < q) *p++ = ' ';
            continue;
        }
        if (*p < 0x20 || *p == 0x7f) *p = ' ';
        p++;
    }
}

/* cut a UTF-8 string to at most max characters, at a character boundary */
static void cut_chars( char *s, size_t max )
{
    size_t chars = 0;
    char *p;

    for (p = s; *p; p++)
    {
        if (((unsigned char)*p & 0xc0) == 0x80) continue;
        if (chars++ == max)
        {
            *p = 0;
            return;
        }
    }
}

/* the value of a field of the current entry into buf; 0 when it has none */
static int field( sd_journal *j, const char *name, char *buf, size_t size )
{
    const void *data;
    size_t len, nlen = strlen( name );

    buf[0] = 0;
    if (sd_journal_get_data( j, name, &data, &len ) < 0 || len <= nlen) return 0;
    len -= nlen + 1;
    if (len >= size) len = size - 1;
    memcpy( buf, (const char *)data + nlen + 1, len );
    buf[len] = 0;
    return 1;
}

/* ---- rate limits: a token bucket per key ---- */

struct bucket { char key[160]; double tokens; uint64_t last; unsigned int dropped; };
static struct bucket buckets[256];
static struct bucket total_bucket;          /* journal to Event Log, all rules */
static struct bucket total_journal_bucket;  /* Event Log to journal, all sources (any user can make sources, and a full
                                             * table reuses slots: this one is never reset) */
static uint64_t last_report;

static int bucket_take( struct bucket *b, double rate, double burst )
{
    uint64_t now = now_usec();

    if (!b->last) b->tokens = burst;
    else b->tokens += (double)(now - b->last) / 1000000.0 * rate;
    if (b->tokens > burst) b->tokens = burst;
    b->last = now;
    if (b->tokens < 1.0) return 0;
    b->tokens -= 1.0;
    return 1;
}

static struct bucket *bucket_get( const char *key )
{
    unsigned int i, free_slot = sizeof(buckets) / sizeof(buckets[0]);

    for (i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++)
    {
        if (!buckets[i].key[0]) { if (free_slot == sizeof(buckets) / sizeof(buckets[0])) free_slot = i; continue; }
        if (!strcmp( buckets[i].key, key )) return &buckets[i];
    }
    if (free_slot == sizeof(buckets) / sizeof(buckets[0])) free_slot = 0;  /* full: reuse the first */
    memset( &buckets[free_slot], 0, sizeof(buckets[0]) );
    snprintf( buckets[free_slot].key, sizeof(buckets[0].key), "%s", key );
    return &buckets[free_slot];
}

/* ---- frames to the service ---- */

struct out_frame { struct out_frame *next; size_t size, sent; char data[]; };
static struct out_frame *out_head, *out_tail;
static size_t out_bytes;

static void queue_frame( unsigned short type, const void *body, size_t size )
{
    struct out_frame *f;
    struct wexj_header hdr;

    if (sizeof(hdr) + size > WEXJ_MAX_FRAME || !(f = malloc( sizeof(*f) + sizeof(hdr) + size ))) return;
    hdr.magic = WEXJ_MAGIC;
    hdr.version = WEXJ_VERSION;
    hdr.type = type;
    hdr.size = sizeof(hdr) + size;
    hdr.reserved = 0;
    memcpy( f->data, &hdr, sizeof(hdr) );
    if (size) memcpy( f->data + sizeof(hdr), body, size );
    f->size = hdr.size;
    f->sent = 0;
    f->next = NULL;
    if (out_tail) out_tail->next = f;
    else out_head = f;
    out_tail = f;
    out_bytes += f->size;
}

/* one event, without the rate limit (the limit's own notice) */
static void send_event_raw( unsigned int log, const char *source, unsigned short type, unsigned int id,
                            const char *cursor, int num, const char **strings )
{
    char body[WEXJ_MAX_FRAME - sizeof(struct wexj_header)], tmp[WEXJ_MAX_STRING * 4 + 1];
    struct wexj_event ev = { id, type, 0, log, 0 };
    size_t pos = sizeof(ev), len;
    int i;

    len = strlen( source ) + 1;
    memcpy( body + pos, source, len );
    pos += len;
    len = strlen( cursor ) + 1;
    if (len > WEXJ_MAX_CURSOR) return;
    memcpy( body + pos, cursor, len );
    pos += len;
    for (i = 0; i < num && i < WEXJ_MAX_STRINGS; i++)
    {
        snprintf( tmp, sizeof(tmp), "%s", strings[i] ? strings[i] : "" );
        sanitize( tmp );
        cut_chars( tmp, WEXJ_MAX_STRING + 1 );  /* the service cuts to the cap and marks the cut */
        len = strlen( tmp ) + 1;
        if (pos + len > sizeof(body)) break;
        memcpy( body + pos, tmp, len );
        pos += len;
    }
    ev.num_strings = i;
    memcpy( body, &ev, sizeof(ev) );
    queue_frame( WEXJ_EVENT, body, pos );
}

static void send_event( const char *rule, unsigned int log, const char *source, unsigned short type, unsigned int id,
                        const char *cursor, int num, const char **strings )
{
    char key[160];
    struct bucket *b;

    snprintf( key, sizeof(key), "E|%s|%s", rule, source );
    b = bucket_get( key );
    if (!bucket_take( b, RATE_PER_SECOND, RATE_BURST ) || !bucket_take( &total_bucket, RATE_TOTAL, RATE_TOTAL ))
    {
        b->dropped++;
        return;
    }
    send_event_raw( log, source, type, id, cursor, num, strings );
}

/* once a minute: what each limit dropped (an event for the Event Log direction, a journal entry for the other) */
static void report_drops(void)
{
    uint64_t now = now_usec();
    unsigned int i;

    if (now - last_report < 60 * 1000000ULL) return;
    last_report = now;
    for (i = 0; i < sizeof(buckets) / sizeof(buckets[0]); i++)
    {
        char rule[160], *source, count[16];
        const char *strings[3];

        if (!buckets[i].dropped) continue;
        snprintf( count, sizeof(count), "%u", buckets[i].dropped );
        buckets[i].dropped = 0;
        if (buckets[i].key[0] == 'J')
        {
            sd_journal_send( "MESSAGE=%s Event Log records of source %s were not written to the journal: over the "
                             "rate limit", count, buckets[i].key + 2, "PRIORITY=4", "SYSLOG_IDENTIFIER=wex-eventlog",
                             NULL );
            continue;
        }
        snprintf( rule, sizeof(rule), "%s", buckets[i].key + 2 );
        if (!(source = strchr( rule, '|' ))) continue;
        *source++ = 0;
        strings[0] = rule;
        strings[1] = source;
        strings[2] = count;
        send_event_raw( WEXJ_LOG_SYSTEM, "Linux Kernel", EVT_WARNING, 5, "", 3, strings );
    }
}

/* ---- units: description, restart delay, failures ---- */

struct unit_state { char unit[128]; unsigned int failures; char exit_status[32]; char exit_code[16]; };
static struct unit_state unit_states[128];

static struct unit_state *unit_state( const char *unit )
{
    unsigned int i, slot = 0;

    for (i = 0; i < sizeof(unit_states) / sizeof(unit_states[0]); i++)
    {
        if (!strcmp( unit_states[i].unit, unit )) return &unit_states[i];
        if (!unit_states[i].unit[0] && !slot) slot = i + 1;
    }
    i = slot ? slot - 1 : 0;
    memset( &unit_states[i], 0, sizeof(unit_states[i]) );
    snprintf( unit_states[i].unit, sizeof(unit_states[i].unit), "%s", unit );
    return &unit_states[i];
}

/* the unit's object path over systemd's D-Bus interface (read-only use: properties any account may read) */
static int unit_path( const char *unit, char **path )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *p;
    int r;

    if (!bus && sd_bus_open_system( &bus ) < 0) return -1;
    r = sd_bus_call_method( bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                            "org.freedesktop.systemd1.Manager", "GetUnit", &err, &reply, "s", unit );
    if (r >= 0 && sd_bus_message_read( reply, "o", &p ) >= 0) *path = strdup( p );
    else r = -1;
    sd_bus_error_free( &err );
    sd_bus_message_unref( reply );
    return r < 0 || !*path ? -1 : 0;
}

static void unit_description( const char *unit, char *buf, size_t size )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char *path = NULL, *desc = NULL;

    snprintf( buf, size, "%s", unit );
    if (unit_path( unit, &path )) return;
    if (sd_bus_get_property_string( bus, "org.freedesktop.systemd1", path, "org.freedesktop.systemd1.Unit",
                                    "Description", &err, &desc ) >= 0 && desc && *desc)
        snprintf( buf, size, "%s", desc );
    sd_bus_error_free( &err );
    free( desc );
    free( path );
}

static uint64_t unit_restart_msec( const char *unit )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    uint64_t usec = 0;
    char *path = NULL;

    if (unit_path( unit, &path )) return 0;
    sd_bus_get_property_trivial( bus, "org.freedesktop.systemd1", path, "org.freedesktop.systemd1.Service",
                                 "RestartUSec", &err, 't', &usec );
    sd_bus_error_free( &err );
    free( path );
    return usec / 1000;
}

/* the reason of a failure, in words (the 7000 event's second string: text, not a token) */
static void failure_text( const char *result, const struct unit_state *us, char *buf, size_t size )
{
    if (!strcmp( result, "exit-code" ))
        snprintf( buf, size, "the process exited with code %s", us->exit_status[0] ? us->exit_status : "(unknown)" );
    else if (!strcmp( result, "signal" ))
        snprintf( buf, size, "the process was stopped by signal %s", us->exit_status[0] ? us->exit_status : "(unknown)" );
    else if (!strcmp( result, "core-dump" ))
        snprintf( buf, size, "the process dumped core (signal %s)", us->exit_status[0] ? us->exit_status : "(unknown)" );
    else if (!strcmp( result, "timeout" )) snprintf( buf, size, "the start timed out" );
    else if (!strcmp( result, "watchdog" )) snprintf( buf, size, "the watchdog timed out" );
    else if (!strcmp( result, "resources" )) snprintf( buf, size, "its resources could not be set up" );
    else if (!strcmp( result, "start-limit-hit" )) snprintf( buf, size, "it was started too often in a short time" );
    else if (!strcmp( result, "dependency" )) snprintf( buf, size, "a unit it depends on did not start" );
    else snprintf( buf, size, "the result was \"%s\"", result[0] ? result : "failed" );
}

/* a unit that failed waits up to LOOKAHEAD_USEC (plus its restart delay) for PID 1's word on it: a failed start job
 * makes it 7000, a scheduled restart 7031, another failure or the end of the wait 7034; so 7031 and 7034 exclude
 * each other (R5) */
struct pending { char unit[128]; char desc[256]; char result[64]; char cursor[WEXJ_MAX_CURSOR]; uint64_t deadline; };
static struct pending pendings[32];

static struct pending *pending_find( const char *unit )
{
    unsigned int i;

    for (i = 0; i < sizeof(pendings) / sizeof(pendings[0]); i++)
        if (pendings[i].unit[0] && !strcmp( pendings[i].unit, unit )) return &pendings[i];
    return NULL;
}

static void emit_7034( struct pending *p )
{
    struct unit_state *us = unit_state( p->unit );
    char count[16];
    const char *strings[2] = { p->desc, count };

    snprintf( count, sizeof(count), "%u", us->failures );
    send_event( "service-crash", WEXJ_LOG_SYSTEM, "Service Control Manager", EVT_ERROR, 7034, p->cursor, 2, strings );
    p->unit[0] = 0;
}

static void expire_pendings(void)
{
    uint64_t now = now_usec();
    unsigned int i;

    for (i = 0; i < sizeof(pendings) / sizeof(pendings[0]); i++)
        if (pendings[i].unit[0] && now >= pendings[i].deadline) emit_7034( &pendings[i] );
}

/* rows 1-3: entries of PID 1 (its _PID and _UID are journald's) */
static void handle_pid1( sd_journal *j, const char *mid, const char *cursor )
{
    char unit[128], result[64], text[256], desc[256], count[16], delay[32];
    const char *strings[5];
    struct unit_state *us;
    struct pending *p;

    if (!field( j, "UNIT", unit, sizeof(unit) ) || !unit[0]) return;
    if ((p = pending_find( unit )))
    {
        us = unit_state( unit );
        if (!strcmp( mid, MID_JOB_FAILED ))
        {
            failure_text( p->result, us, text, sizeof(text) );
            strings[0] = p->desc;
            strings[1] = text;
            send_event( "service-start", WEXJ_LOG_SYSTEM, "Service Control Manager", EVT_ERROR, 7000, cursor, 2,
                        strings );
            p->unit[0] = 0;
            return;
        }
        if (!strcmp( mid, MID_RESTART_SCHEDULED ))
        {
            snprintf( count, sizeof(count), "%u", us->failures );
            snprintf( delay, sizeof(delay), "%llu", (unsigned long long)unit_restart_msec( unit ) );
            /* five strings, the message using %1 %2 %3 %5 (Windows 11's manifest, read on the host): %4 is the
             * action's code (SC_ACTION_RESTART, 1), %5 its text */
            strings[0] = p->desc;
            strings[1] = count;
            strings[2] = delay;
            strings[3] = "1";
            strings[4] = "Restart the service";
            send_event( "service-restart", WEXJ_LOG_SYSTEM, "Service Control Manager", EVT_ERROR, 7031, cursor, 5,
                        strings );
            p->unit[0] = 0;
            return;
        }
        /* a failure again: the earlier one had no restart; other entries (resource accounting, which comes between
         * the failure and the scheduled restart) decide nothing */
        if (strcmp( mid, MID_UNIT_FAILED )) return;
        emit_7034( p );
    }
    if (!strcmp( mid, MID_PROCESS_EXIT ))
    {
        us = unit_state( unit );
        field( j, "EXIT_STATUS", us->exit_status, sizeof(us->exit_status) );
        field( j, "EXIT_CODE", us->exit_code, sizeof(us->exit_code) );
    }
    else if (!strcmp( mid, MID_UNIT_FAILED ))
    {
        unsigned int i;

        us = unit_state( unit );
        us->failures++;
        for (i = 0; i < sizeof(pendings) / sizeof(pendings[0]) && pendings[i].unit[0]; i++);
        if (i == sizeof(pendings) / sizeof(pendings[0])) emit_7034( &pendings[0] );
        if (i == sizeof(pendings) / sizeof(pendings[0])) i = 0;
        p = &pendings[i];
        snprintf( p->unit, sizeof(p->unit), "%s", unit );
        unit_description( unit, p->desc, sizeof(p->desc) );
        field( j, "UNIT_RESULT", p->result, sizeof(p->result) );
        snprintf( p->cursor, sizeof(p->cursor), "%s", cursor );
        /* a scheduled restart comes after the unit's restart delay: the wait covers it */
        p->deadline = now_usec() + LOOKAHEAD_USEC + unit_restart_msec( unit ) * 1000;
    }
    else if (!strcmp( mid, MID_JOB_FAILED ))
    {
        /* a start job that failed without a failure of the unit before it (a dependency, a timeout) */
        us = unit_state( unit );
        field( j, "JOB_RESULT", result, sizeof(result) );
        unit_description( unit, desc, sizeof(desc) );
        failure_text( result, us, text, sizeof(text) );
        strings[0] = desc;
        strings[1] = text;
        send_event( "service-start", WEXJ_LOG_SYSTEM, "Service Control Manager", EVT_ERROR, 7000, cursor, 2,
                    strings );
    }
}

/* rows 4-7: kernel entries (_TRANSPORT=kernel is journald's) */
static void handle_kernel( sd_journal *j, int prio, const char *msg, const char *cursor )
{
    char dev[128], subsystem[32];
    const char *strings[1], *p;

    field( j, "_KERNEL_SUBSYSTEM", subsystem, sizeof(subsystem) );
    if (prio <= 3 && ((p = strstr( msg, "I/O error, dev " )) || !strcmp( subsystem, "block" ) ||
                      !strcmp( subsystem, "scsi" )))
    {
        if (p)
        {
            size_t n = strcspn( p + 15, ", " );
            snprintf( dev, sizeof(dev), "/dev/%.*s", (int)(n < 64 ? n : 64), p + 15 );
        }
        else if (!field( j, "_UDEV_DEVNODE", dev, sizeof(dev) ) && !field( j, "_KERNEL_DEVICE", dev, sizeof(dev) ))
            snprintf( dev, sizeof(dev), "(unknown)" );
        strings[0] = dev;
        send_event( "disk", WEXJ_LOG_SYSTEM, "Disk", EVT_ERROR, 11, cursor, 1, strings );
        return;
    }
    strings[0] = msg;
    if (prio <= 3 && (strstr( msg, "EXT4-fs error" ) || strstr( msg, "BTRFS error" ) ||
                      (strstr( msg, "XFS (" ) && strstr( msg, "rror" ))))
        send_event( "fs", WEXJ_LOG_LINUX, "Linux Kernel", EVT_ERROR, 2, cursor, 1, strings );
    else if (strstr( msg, "Out of memory: Killed process" ) || !strncmp( msg, "oom-kill:", 9 ))
        send_event( "oom", WEXJ_LOG_LINUX, "Linux Kernel", EVT_ERROR, 3, cursor, 1, strings );
    else if (prio <= 2)
        send_event( "kernel", WEXJ_LOG_LINUX, "Linux Kernel", EVT_ERROR, 1, cursor, 1, strings );
}

/* a line of Wine's debug output (scope 3: never in the Event Log), whatever wrote it */
static int is_wine_debug_line( const char *msg )
{
    int i;

    for (i = 0; i < 4; i++) if (!isxdigit( (unsigned char)msg[i] )) return 0;
    return !strncmp( msg + 4, ":trace:", 7 ) || !strncmp( msg + 4, ":warn:", 6 ) ||
           !strncmp( msg + 4, ":err:", 5 ) || !strncmp( msg + 4, ":fixme:", 7 );
}

static int is_wine_process( const char *comm, const char *exe )
{
    static const char *comms[] = { "wine", "wine64", "wineserver", "wine-preloader", "wine64-preloader" };
    unsigned int i;

    for (i = 0; i < sizeof(comms) / sizeof(comms[0]); i++) if (!strcmp( comm, comms[i] )) return 1;
    return !strncmp( exe, "/usr/local/lib/wine/", 20 ) || !strncmp( exe, "/usr/lib/wine/", 14 ) ||
           !strncmp( exe, "/usr/local/bin/wine", 19 );
}

static void process_entry( sd_journal *j )
{
    char transport[16], pid[16], uid[16], comm[64], exe[256], unit[128], ident[64], mid[40], prio_s[8];
    char msg[4096], *cursor = NULL;
    unsigned int i;
    int prio;

    field( j, "_TRANSPORT", transport, sizeof(transport) );
    field( j, "_PID", pid, sizeof(pid) );
    field( j, "_UID", uid, sizeof(uid) );
    field( j, "_COMM", comm, sizeof(comm) );
    field( j, "_EXE", exe, sizeof(exe) );
    field( j, "_SYSTEMD_UNIT", unit, sizeof(unit) );
    field( j, "SYSLOG_IDENTIFIER", ident, sizeof(ident) );
    field( j, "MESSAGE_ID", mid, sizeof(mid) );
    field( j, "MESSAGE", msg, sizeof(msg) );
    prio = field( j, "PRIORITY", prio_s, sizeof(prio_s) ) ? atoi( prio_s ) : 6;

    /* scope 3, first by journald's own fields, then the debug-line shape as a second net */
    if (is_wine_process( comm, exe ) || is_wine_debug_line( msg )) return;
    /* our own journal entries (the Event Log's records): by journald's _UID, not the identifier alone */
    if ((uid_t)strtoul( uid, NULL, 10 ) == own_uid && uid[0] && !strcmp( ident, "wex-eventlog" )) return;
    if (prio >= 7) return;
    if (sd_journal_get_cursor( j, &cursor ) < 0 || !cursor) return;

    if (!strcmp( pid, "1" ) && !strcmp( uid, "0" )) handle_pid1( j, mid, cursor );
    else if (!strcmp( transport, "kernel" )) handle_kernel( j, prio, msg, cursor );
    else if (!strncmp( unit, "systemd-coredump@", 17 ) && !strcmp( mid, MID_COREDUMP ) && uid[0] &&
             ((uid_t)strtoul( uid, NULL, 10 ) == 0 || (uid_t)strtoul( uid, NULL, 10 ) == coredump_uid))
    {
        char cuid[16], prog[256], sig[32];
        const char *strings[2] = { prog, sig };

        field( j, "COREDUMP_UID", cuid, sizeof(cuid) );
        if (!field( j, "COREDUMP_EXE", prog, sizeof(prog) )) field( j, "COREDUMP_COMM", prog, sizeof(prog) );
        if (!field( j, "COREDUMP_SIGNAL_NAME", sig, sizeof(sig) )) field( j, "COREDUMP_SIGNAL", sig, sizeof(sig) );
        send_event( "coredump", (coredumps_all || (cuid[0] && strtoul( cuid, NULL, 10 ) < 1000)) ?
                    WEXJ_LOG_APPLICATION : WEXJ_LOG_LINUX, "Linux Kernel", EVT_ERROR, 4, cursor, 2, strings );
    }
    else if (unit[0] && prio <= 3)
    {
        for (i = 0; i < (unsigned int)unit_count; i++)
        {
            const char *strings[1] = { msg };

            if (strcmp( units[i].unit, unit )) continue;
            /* the source is the configuration's, never a field of the entry */
            send_event( units[i].unit, WEXJ_LOG_LINUX, units[i].source, EVT_ERROR, 1, cursor, 1, strings );
            break;
        }
    }
    free( cursor );
}

/* ---- frames from the service (untrusted, R6) ---- */

/* a NUL-terminated string inside [*p, end); NULL when there is none */
static const char *take( const char **p, const char *end )
{
    const char *s = *p, *nul = memchr( s, 0, end - s );

    if (!nul) return NULL;
    *p = nul + 1;
    return s;
}

static int handle_record( const char *body, size_t size )
{
    const char *p = body + sizeof(struct wexj_record), *end = body + size, *log, *source, *sid, *s;
    char text[8192], safe_log[64], safe_source[WEXJ_MAX_SOURCE * 4 + 1], safe_sid[256], key[160], one[WEXJ_MAX_STRING * 4 + 1];
    struct wexj_record rec;
    struct bucket *b;
    size_t pos = 0;
    int i, prio;

    if (size < sizeof(rec)) return 0;
    memcpy( &rec, body, sizeof(rec) );
    if (rec.num_strings > WEXJ_MAX_STRINGS) return 0;
    if (!(log = take( &p, end )) || !(source = take( &p, end )) || !(sid = take( &p, end ))) return 0;
    snprintf( safe_log, sizeof(safe_log), "%s", log );
    snprintf( safe_source, sizeof(safe_source), "%s", source );
    snprintf( safe_sid, sizeof(safe_sid), "%s", sid );
    sanitize( safe_log );
    sanitize( safe_source );
    sanitize( safe_sid );
    text[0] = 0;
    for (i = 0; i < rec.num_strings; i++)
    {
        if (!(s = take( &p, end ))) return 0;
        snprintf( one, sizeof(one), "%s", s );
        sanitize( one );
        cut_chars( one, WEXJ_MAX_STRING );
        pos += snprintf( text + pos, pos < sizeof(text) ? sizeof(text) - pos : 0, "%s%s", i ? " | " : "", one );
        if (pos >= sizeof(text)) pos = sizeof(text) - 1;
    }
    if (p != end) return 0;
    if (!strcasecmp( safe_log, "Security" ) && !security_to_journal) return 1;
    snprintf( key, sizeof(key), "J|%s", safe_source );
    b = bucket_get( key );
    if (!bucket_take( b, RATE_PER_SECOND, RATE_BURST ) || !bucket_take( &total_journal_bucket, RATE_TOTAL, RATE_TOTAL ))
    {
        b->dropped++;
        return 1;
    }
    prio = rec.event_type == EVT_ERROR ? 3 : rec.event_type == EVT_WARNING ? 4 : 6;
    sd_journal_send( "MESSAGE=%s/%s %u: %s", safe_log, safe_source, rec.event_id, text,
                     "PRIORITY=%d", prio,
                     "SYSLOG_IDENTIFIER=wex-eventlog",
                     "WEX_EVENTLOG_LOG=%s", safe_log,
                     "WEX_EVENTLOG_SOURCE=%s", safe_source,
                     "WEX_EVENT_ID=%u", rec.event_id,
                     "WEX_EVENT_TYPE=%u", rec.event_type,
                     "WEX_EVENT_RECORD=%u", rec.record_number,
                     "WEX_EVENT_USER_SID=%s", safe_sid,
                     NULL );
    return 1;
}

/* ---- one connection of session 0's service ---- */

static int read_full( int fd, void *buf, size_t size, int timeout_ms )
{
    char *p = buf;

    while (size)
    {
        struct pollfd pfd = { fd, POLLIN, 0 };
        ssize_t n;

        if (poll( &pfd, 1, timeout_ms ) <= 0) return -1;
        if ((n = recv( fd, p, size, 0 )) <= 0) return -1;
        p += n;
        size -= n;
    }
    return 0;
}

static int flush_out( int fd )
{
    while (out_head)
    {
        struct out_frame *f = out_head;
        ssize_t n = send( fd, f->data + f->sent, f->size - f->sent, MSG_NOSIGNAL | MSG_DONTWAIT );

        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        f->sent += n;
        if (f->sent < f->size) return 0;
        out_head = f->next;
        if (!out_head) out_tail = NULL;
        out_bytes -= f->size;
        free( f );
    }
    return 0;
}

static void drop_out(void)
{
    while (out_head)
    {
        struct out_frame *f = out_head;
        out_head = f->next;
        free( f );
    }
    out_tail = NULL;
    out_bytes = 0;
}

/* a new connection: session 0's account (by uid) gives its fd, anything else is refused here; -1 */
static int accept_peer( int listen_fd )
{
    uid_t uid = (uid_t)-1;
    pid_t pid = -1;
    int fd = accept4( listen_fd, NULL, NULL, SOCK_CLOEXEC );

    if (fd == -1) return -1;
    /* session 0's account only, by uid: the socket's group would also admit elevated processes */
    if (wex_peer_cred( fd, &uid, &pid ) || uid != session0_uid)
    {
        log_msg( LOG_WARNING, "refused a connection from uid %d (pid %d): not session 0's account", (int)uid, (int)pid );
        close( fd );
        return -1;
    }
    return fd;
}

/* serve session 0's service on fd until it goes; a newer connection of session 0 (the service restarted) replaces it:
 * its fd is returned, else -1 */
static int serve( int fd, int listen_fd )
{
    int next = -1;
    static char in[2 * WEXJ_MAX_FRAME];
    char cursor[WEXJ_MAX_CURSOR + 1];
    struct wexj_header hdr;
    sd_journal *j = NULL;
    size_t have = 0;
    int more = 1;

    /* HELLO: the cursor of the last entry the service stored */
    if (read_full( fd, &hdr, sizeof(hdr), 10000 ) || hdr.magic != WEXJ_MAGIC || hdr.version != WEXJ_VERSION ||
        hdr.type != WEXJ_HELLO || hdr.size <= sizeof(hdr) || hdr.size > sizeof(hdr) + WEXJ_MAX_CURSOR ||
        read_full( fd, cursor, hdr.size - sizeof(hdr), 10000 ) || cursor[hdr.size - sizeof(hdr) - 1])
    {
        log_msg( LOG_WARNING, "no valid HELLO from the service: connection closed" );
        return -1;
    }
    if (sd_journal_open( &j, SD_JOURNAL_SYSTEM | SD_JOURNAL_LOCAL_ONLY ) < 0)
    {
        log_msg( LOG_ERR, "cannot open the system journal" );
        return -1;
    }
    if (cursor[0])
    {
        if (sd_journal_seek_cursor( j, cursor ) < 0 || sd_journal_next( j ) <= 0 ||
            sd_journal_test_cursor( j, cursor ) <= 0)
        {
            /* vacuumed or rotated past it: from the oldest entry kept, and the service says there is a gap */
            log_msg( LOG_WARNING, "the stored cursor is no longer in the journal: from the oldest entry" );
            queue_frame( WEXJ_GAP, NULL, 0 );
            sd_journal_seek_head( j );
        }
    }
    else if (first_start_now)
    {
        sd_journal_seek_tail( j );
        sd_journal_previous( j );
    }
    else
    {
        /* the first start: from this boot */
        sd_id128_t boot;
        char match[64];

        if (sd_id128_get_boot( &boot ) >= 0)
        {
            snprintf( match, sizeof(match), "_BOOT_ID=" SD_ID128_FORMAT_STR, SD_ID128_FORMAT_VAL( boot ) );
            sd_journal_add_match( j, match, 0 );
        }
        sd_journal_seek_head( j );
    }
    log_msg( LOG_INFO, "serving session 0's EventLog service" );

    for (;;)
    {
        struct pollfd pfd[3];
        int i, timeout, jfd;

        /* back-pressure: the journal is read only while the service keeps up */
        for (i = 0; i < 200 && out_bytes < OUT_MAX; i++)
        {
            int r = sd_journal_next( j );
            if (r <= 0) { more = 0; break; }
            more = 1;
            process_entry( j );
        }
        expire_pendings();
        report_drops();
        if (flush_out( fd )) break;

        timeout = (more && out_bytes < OUT_MAX) ? 0 : 500;
        jfd = sd_journal_get_fd( j );
        pfd[0].fd = fd;
        pfd[0].events = POLLIN | (out_head ? POLLOUT : 0);
        pfd[1].fd = listen_fd;
        pfd[1].events = POLLIN;
        pfd[2].fd = jfd;
        pfd[2].events = jfd >= 0 ? sd_journal_get_events( j ) : 0;
        pfd[0].revents = pfd[1].revents = pfd[2].revents = 0;
        if (poll( pfd, jfd >= 0 ? 3 : 2, timeout ) < 0 && errno != EINTR) break;
        if (jfd >= 0) sd_journal_process( j );
        if ((pfd[1].revents & POLLIN) && (next = accept_peer( listen_fd )) != -1)
        {
            log_msg( LOG_INFO, "a new connection of session 0's service replaces the current one" );
            break;
        }
        if (pfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (pfd[0].revents & POLLIN)
        {
            ssize_t n = recv( fd, in + have, sizeof(in) - have, MSG_DONTWAIT );

            if (n <= 0 && !(n < 0 && (errno == EAGAIN || errno == EINTR))) break;
            if (n > 0) have += n;
            while (have >= sizeof(hdr))
            {
                memcpy( &hdr, in, sizeof(hdr) );
                if (hdr.magic != WEXJ_MAGIC || hdr.version != WEXJ_VERSION || hdr.size < sizeof(hdr) ||
                    hdr.size > WEXJ_MAX_FRAME || hdr.type != WEXJ_RECORD)
                    goto bad;
                if (have < hdr.size) break;
                if (!handle_record( in + sizeof(hdr), hdr.size - sizeof(hdr) )) goto bad;
                memmove( in, in + hdr.size, have - hdr.size );
                have -= hdr.size;
            }
        }
    }
    goto done;
bad:
    log_msg( LOG_WARNING, "a malformed frame from the service: connection closed" );
done:
    drop_out();
    sd_journal_close( j );
    return next;
}

/* ---- main ---- */

static uid_t account_uid( const char *name )
{
    struct passwd *pw = getpwnam( name );
    return pw ? pw->pw_uid : (uid_t)-1;
}

int main( int argc, char **argv )
{
    const char *socket_path = NULL, *config = CONFIG_PATH, *session0_user = "wexsys";
    int listen_fd, i;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "--socket" ) && i + 1 < argc) socket_path = argv[++i];
        else if (!strcmp( argv[i], "--config" ) && i + 1 < argc) config = argv[++i];
        else if (!strcmp( argv[i], "--session0-user" ) && i + 1 < argc) session0_user = argv[++i];
        else if (!strcmp( argv[i], "--foreground" )) foreground = 1;
        else if (!strcmp( argv[i], "--first-start" ) && i + 1 < argc) first_start_now = !strcmp( argv[++i], "now" );
        else
        {
            fprintf( stderr, "usage: wex-journal-bridge [--socket PATH] [--config FILE] [--session0-user NAME] "
                     "[--first-start boot|now] [--foreground]\n" );
            return 2;
        }
    }
    signal( SIGPIPE, SIG_IGN );
    own_uid = getuid();
    if ((session0_uid = account_uid( session0_user )) == (uid_t)-1)
    {
        log_msg( LOG_ERR, "no account %s (session 0's)", session0_user );
        return 1;
    }
    coredump_uid = account_uid( "systemd-coredump" );
    load_config( config );

    if (!socket_path && sd_listen_fds( 1 ) == 1) listen_fd = SD_LISTEN_FDS_START;
    else
    {
        struct sockaddr_un addr;

        if (!socket_path) socket_path = WEXJ_SOCKET;
        memset( &addr, 0, sizeof(addr) );
        addr.sun_family = AF_UNIX;
        if (strlen( socket_path ) >= sizeof(addr.sun_path)) return 2;
        strcpy( addr.sun_path, socket_path );
        unlink( socket_path );
        if ((listen_fd = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0 )) == -1 ||
            bind( listen_fd, (struct sockaddr *)&addr, sizeof(addr) ) == -1 || listen( listen_fd, 4 ) == -1)
        {
            log_msg( LOG_ERR, "cannot listen on %s: %s", socket_path, strerror( errno ) );
            return 1;
        }
        /* tests only (--socket): the uid check decides who is served; the installed unit's socket is the socket
         * unit's, 0660 for wexjournal:wexsys */
        chmod( socket_path, 0666 );
    }

    for (;;)
    {
        int fd = accept_peer( listen_fd );

        while (fd != -1)
        {
            int next = serve( fd, listen_fd );
            close( fd );
            fd = next;
        }
    }
}
