/*
 * WineExplorer host services - the computer's hardware for Device Manager (sysfs, the udev database, libkmod)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* Everything here is readable without privileges.  The report is raw: Device Manager decides what is a device, its
 * class and its name.  One item per line, "<tag> <text>":
 *   D <path below /sys/devices>    starts a device; S subsystem, R driver, M module (kernel module of the driver),
 *                                  F its ACPI node (path); ACPI nodes have "A physical_node <path>"
 *   A <attribute> <value>          sysfs attributes, E <key> <value> uevent, P <key> <value> udev database (ID_*)
 *   X io|mem|irq|dma <start> [end] resources
 *   K <module>                     starts a kernel module; k <key> <value> its modinfo, path, mtime, builtin
 *   G release|vendor|cpu|dma|loaded <value>   the kernel, the processor, DMA channels, loaded modules
 * libkmod is loaded at run time (it reads compressed modules); without it modules have no details. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

struct out { char *data; size_t len, size; BOOL failed; };

static void put( struct out *o, const char *fmt, ... )
{
    va_list args;
    int n;

    if (o->failed) return;
    for (;;)
    {
        va_start( args, fmt );
        n = vsnprintf( o->data + o->len, o->size - o->len, fmt, args );
        va_end( args );
        if (n < 0) { o->failed = TRUE; return; }
        if (o->len + n < o->size) { o->len += n; return; }
        o->size = o->size * 2 > o->len + n + 4096 ? o->size * 2 : o->len + n + 4096;
        if (!(o->data = realloc( o->data, o->size ))) { o->failed = TRUE; return; }
    }
}

/* first line of a small file, control characters made spaces, trailing blanks removed */
static BOOL read_value( const char *dir, const char *name, char *buf, size_t size )
{
    char path[PATH_MAX];
    size_t n;
    FILE *f;
    char *p;

    snprintf( path, sizeof(path), "%s/%s", dir, name );
    if (!(f = fopen( path, "r" ))) return FALSE;
    n = fread( buf, 1, size - 1, f );
    fclose( f );
    buf[n] = 0;
    if ((p = strchr( buf, '\n' ))) *p = 0;
    for (p = buf; *p; p++) if ((unsigned char)*p < 0x20) *p = ' ';
    while (p > buf && p[-1] == ' ') *--p = 0;
    return buf[0] != 0;
}

static BOOL link_name( const char *dir, const char *name, char *buf, size_t size )
{
    char path[PATH_MAX], target[PATH_MAX], *base;
    ssize_t n;

    snprintf( path, sizeof(path), "%s/%s", dir, name );
    if ((n = readlink( path, target, sizeof(target) - 1 )) <= 0) return FALSE;
    target[n] = 0;
    base = strrchr( target, '/' );
    snprintf( buf, size, "%s", base ? base + 1 : target );
    return TRUE;
}

/* where a link points, below /sys/devices */
static BOOL link_path( const char *dir, const char *name, char *buf, size_t size )
{
    char path[PATH_MAX], target[PATH_MAX];

    snprintf( path, sizeof(path), "%s/%s", dir, name );
    if (!realpath( path, target ) || strncmp( target, "/sys/devices/", 13 )) return FALSE;
    snprintf( buf, size, "%s", target + 13 );
    return TRUE;
}

static const char *const subsystems[] =
{
    "pci", "pnp", "usb", "hid", "input", "hdaudio", "sound", "block", "scsi", "serio", "net", "drm", "power_supply",
    "cpu", "acpi", "platform", "nvme", "virtio", "mmc", "sdio", "bluetooth", "i2c", "firewire", "video4linux", "tty",
    "thunderbolt", "ieee80211", "printer", "usbmisc", "rfkill", NULL
};

static const char *const attributes[] =
{
    "class", "vendor", "device", "subsystem_vendor", "subsystem_device", "revision", "irq", "idVendor", "idProduct",
    "bcdDevice", "manufacturer", "product", "serial", "bDeviceClass", "bInterfaceClass", "bInterfaceSubClass",
    "bInterfaceProtocol", "bNumInterfaces", "bInterfaceNumber", "interface", "speed", "busnum", "devnum", "id",
    "name", "phys", "removable", "size", "ro", "type", "status", "hid", "path", "vendor_name", "chip_name",
    "model_name", "technology", "uid", "model", "rev", "firmware_rev", "enabled", "port_no",
    "device/model", "device/vendor", "device/rev", "device/type", "id/bustype", "id/vendor", "id/product", NULL
};

static BOOL wanted( const char *subsystem )
{
    const char *const *s;
    for (s = subsystems; *s; s++) if (!strcmp( *s, subsystem )) return TRUE;
    return FALSE;
}

/* the monitor's name and manufacturer from its EDID (descriptor 0xFC) */
static void put_edid( struct out *o, const char *dir )
{
    unsigned char edid[256];
    char path[PATH_MAX], name[16];
    int i, j;
    size_t n;
    FILE *f;

    snprintf( path, sizeof(path), "%s/edid", dir );
    if (!(f = fopen( path, "rb" ))) return;
    n = fread( edid, 1, sizeof(edid), f );
    fclose( f );
    if (n < 128) return;
    put( o, "A edid_vendor %c%c%c\n", '@' + ((edid[8] >> 2) & 0x1f),
         '@' + (((edid[8] & 3) << 3) | (edid[9] >> 5)), '@' + (edid[9] & 0x1f) );
    put( o, "A edid_product %02X%02X\n", edid[11], edid[10] );
    for (i = 54; i + 18 <= 126; i += 18)
    {
        if (edid[i] || edid[i + 1] || edid[i + 3] != 0xfc) continue;
        for (j = 0; j < 13 && edid[i + 5 + j] != '\n'; j++) name[j] = edid[i + 5 + j] < 0x20 ? ' ' : edid[i + 5 + j];
        name[j] = 0;
        while (j && name[j - 1] == ' ') name[--j] = 0;
        if (j) put( o, "A edid_name %s\n", name );
    }
}

static void put_resources( struct out *o, const char *dir, const char *subsystem )
{
    char path[PATH_MAX], line[256];
    unsigned long long start, end, flags;
    FILE *f;

    if (!strcmp( subsystem, "pci" ))
    {
        snprintf( path, sizeof(path), "%s/resource", dir );
        if (!(f = fopen( path, "r" ))) return;
        while (fgets( line, sizeof(line), f ))
        {
            if (sscanf( line, "%llx %llx %llx", &start, &end, &flags ) != 3 || !end) continue;
            if (flags & 0x100) put( o, "X io %llx %llx\n", start, end );
            else if (flags & 0x200) put( o, "X mem %llx %llx\n", start, end );
        }
        fclose( f );
    }
    else if (!strcmp( subsystem, "pnp" ))
    {
        char type[8];

        snprintf( path, sizeof(path), "%s/resources", dir );
        if (!(f = fopen( path, "r" ))) return;
        while (fgets( line, sizeof(line), f ))
        {
            if (sscanf( line, "%7s %llx-%llx", type, &start, &end ) == 3 && (!strcmp( type, "io" ) || !strcmp( type, "mem" )))
                put( o, "X %s %llx %llx\n", type, start, end );
            else if (sscanf( line, "%7s %llu", type, &start ) == 2 && (!strcmp( type, "irq" ) || !strcmp( type, "dma" )))
                put( o, "X %s %llu\n", type, start );
        }
        fclose( f );
    }
}

/* udev keeps what it learned (names from the hardware databases, model strings) in /run/udev/data */
static void put_udev( struct out *o, const char *subsystem, const char *sysname, const char *major, const char *minor )
{
    char path[PATH_MAX], line[1024], *eq, *end;
    FILE *f;

    if (major[0]) snprintf( path, sizeof(path), "/run/udev/data/%c%s:%s", strcmp( subsystem, "block" ) ? 'c' : 'b',
                            major, minor );
    else snprintf( path, sizeof(path), "/run/udev/data/+%s:%s", subsystem, sysname );
    if (!(f = fopen( path, "r" ))) return;
    while (fgets( line, sizeof(line), f ))
    {
        if (strncmp( line, "E:ID_", 5 ) || !(eq = strchr( line, '=' ))) continue;
        if ((end = strchr( eq, '\n' ))) *end = 0;
        if (strstr( line, "_ENC=" )) continue;
        *eq = 0;
        put( o, "P %s %s\n", line + 2, eq + 1 );
    }
    fclose( f );
}

struct modules { char **names; size_t count, size; };

static void add_module( struct modules *m, const char *name )
{
    size_t i;
    for (i = 0; i < m->count; i++) if (!strcmp( m->names[i], name )) return;
    if (m->count == m->size)
    {
        char **names = realloc( m->names, (m->size = m->size * 2 + 32) * sizeof(*names) );
        if (!names) return;
        m->names = names;
    }
    if ((m->names[m->count] = strdup( name ))) m->count++;
}

static void put_device( struct out *o, const char *dir, const char *rel, const char *subsystem, struct modules *mods )
{
    const char *const *a;
    char value[512], driver[256], module[256], major[16] = "", minor[16] = "", path[PATH_MAX], line[600];
    const char *sysname = strrchr( dir, '/' ) + 1;
    FILE *f;

    put( o, "D %s\nS %s\n", rel, subsystem );
    if (link_name( dir, "driver", driver, sizeof(driver) ))
    {
        put( o, "R %s\n", driver );
        if (link_name( dir, "driver/module", module, sizeof(module) )) put( o, "M %s\n", module );
        else snprintf( module, sizeof(module), "%s", driver );
        add_module( mods, module );
    }
    /* the ACPI node of the device, and the device of an ACPI node */
    if (link_path( dir, "firmware_node", path, sizeof(path) )) put( o, "F %s\n", path );
    if (!strcmp( subsystem, "acpi" ) && link_path( dir, "physical_node", path, sizeof(path) ))
        put( o, "A physical_node %s\n", path );
    for (a = attributes; *a; a++)
        if (read_value( dir, *a, value, sizeof(value) )) put( o, "A %s %s\n", *a, value );
    if (!strcmp( subsystem, "drm" )) put_edid( o, dir );
    put_resources( o, dir, subsystem );

    snprintf( path, sizeof(path), "%s/uevent", dir );
    if ((f = fopen( path, "r" )))
    {
        while (fgets( line, sizeof(line), f ))
        {
            char *eq = strchr( line, '=' ), *end = strchr( line, '\n' );
            if (!eq || !end) continue;   /* longer than the line buffer: not useful here */
            *end = 0;
            *eq = 0;
            if (!strcmp( line, "MAJOR" )) snprintf( major, sizeof(major), "%s", eq + 1 );
            if (!strcmp( line, "MINOR" )) snprintf( minor, sizeof(minor), "%s", eq + 1 );
            if (!strcmp( line, "MODALIAS" ) && strlen( eq + 1 ) > 200) continue;
            put( o, "E %s %s\n", line, eq + 1 );
        }
        fclose( f );
    }
    put_udev( o, subsystem, sysname, major, minor );
}

static void walk( struct out *o, const char *dir, const char *rel, int depth, struct modules *mods )
{
    char path[PATH_MAX], subsystem[64], child[PATH_MAX];
    struct dirent *de;
    struct stat st;
    DIR *d;

    snprintf( path, sizeof(path), "%s/uevent", dir );
    if (*rel && !access( path, R_OK ) && link_name( dir, "subsystem", subsystem, sizeof(subsystem) ) &&
        wanted( subsystem ))
        put_device( o, dir, rel, subsystem, mods );

    if (depth > 24 || !(d = opendir( dir ))) return;
    while ((de = readdir( d )))
    {
        if (de->d_name[0] == '.' || !strcmp( de->d_name, "power" ) || !strcmp( de->d_name, "subsystem" )) continue;
        snprintf( path, sizeof(path), "%s/%s", dir, de->d_name );
        if (lstat( path, &st ) || !S_ISDIR( st.st_mode )) continue;
        snprintf( child, sizeof(child), "%s%s%s", rel, *rel ? "/" : "", de->d_name );
        /* virtual devices: only network interfaces and block devices (shown as hidden devices) */
        if (!strcmp( child, "virtual" ) || !strcmp( child, "system" ))
        {
            DIR *v = opendir( path );
            if (!v) continue;
            while ((de = readdir( v )))
            {
                char sub[PATH_MAX], subrel[PATH_MAX];
                if (strcmp( de->d_name, "net" ) && strcmp( de->d_name, "block" ) && strcmp( de->d_name, "cpu" )) continue;
                snprintf( sub, sizeof(sub), "%s/%s", path, de->d_name );
                snprintf( subrel, sizeof(subrel), "%s/%s", child, de->d_name );
                walk( o, sub, subrel, depth + 1, mods );
            }
            closedir( v );
            continue;
        }
        walk( o, path, child, depth + 1, mods );
    }
    closedir( d );
}

/* libkmod */
struct kmod_ctx;
struct kmod_module;
struct kmod_list;
static struct kmod_ctx *(*p_kmod_new)( const char *, const char * const * );
static struct kmod_ctx *(*p_kmod_unref)( struct kmod_ctx * );
static int (*p_kmod_module_new_from_name)( struct kmod_ctx *, const char *, struct kmod_module ** );
static int (*p_kmod_module_new_from_loaded)( const struct kmod_ctx *, struct kmod_list ** );
static struct kmod_module *(*p_kmod_module_get_module)( const struct kmod_list * );
static struct kmod_module *(*p_kmod_module_unref)( struct kmod_module * );
static int (*p_kmod_module_unref_list)( struct kmod_list * );
static const char *(*p_kmod_module_get_name)( const struct kmod_module * );
static const char *(*p_kmod_module_get_path)( const struct kmod_module * );
static int (*p_kmod_module_get_initstate)( const struct kmod_module * );
static int (*p_kmod_module_get_info)( const struct kmod_module *, struct kmod_list ** );
static struct kmod_list *(*p_kmod_module_get_dependencies)( const struct kmod_module * );
static const char *(*p_kmod_module_info_get_key)( const struct kmod_list * );
static const char *(*p_kmod_module_info_get_value)( const struct kmod_list * );
static void (*p_kmod_module_info_free_list)( struct kmod_list * );
static struct kmod_list *(*p_kmod_list_next)( const struct kmod_list *, const struct kmod_list * );

static BOOL load_kmod(void)
{
    static int loaded = -1;
    void *lib;

    if (loaded >= 0) return loaded;
    loaded = 0;
    if (!(lib = dlopen( "libkmod.so.2", RTLD_NOW )))
    {
        WARN( "libkmod not available, kernel modules have no details\n" );
        return FALSE;
    }
#define LOAD(name) if (!(p_##name = dlsym( lib, #name ))) return FALSE;
    LOAD( kmod_new )
    LOAD( kmod_unref )
    LOAD( kmod_module_new_from_name )
    LOAD( kmod_module_new_from_loaded )
    LOAD( kmod_module_get_module )
    LOAD( kmod_module_unref )
    LOAD( kmod_module_unref_list )
    LOAD( kmod_module_get_name )
    LOAD( kmod_module_get_path )
    LOAD( kmod_module_get_initstate )
    LOAD( kmod_module_get_info )
    LOAD( kmod_module_info_get_key )
    LOAD( kmod_module_info_get_value )
    LOAD( kmod_module_info_free_list )
    LOAD( kmod_list_next )
    LOAD( kmod_module_get_dependencies )
#undef LOAD
    loaded = 1;
    return TRUE;
}

static void put_module( struct out *o, struct kmod_ctx *ctx, const char *name, const char *release )
{
    static const char *const keys[] = { "author", "description", "license", "version", "signer", "vermagic", NULL };
    struct kmod_module *mod;
    struct kmod_list *info = NULL, *l;
    const char *path, *const *k;
    char kernel[PATH_MAX];
    BOOL author = FALSE;
    struct stat st;

    put( o, "K %s\n", name );
    if (!ctx || p_kmod_module_new_from_name( ctx, name, &mod ) < 0) return;
    path = p_kmod_module_get_path( mod );
    if (!path || p_kmod_module_get_initstate( mod ) == 0 /* KMOD_MODULE_BUILTIN */)
    {
        put( o, "k builtin 1\n" );
        snprintf( kernel, sizeof(kernel), "/boot/vmlinuz-%s", release );
        path = kernel;
    }
    put( o, "k path %s\n", path );
    if (!stat( path, &st )) put( o, "k mtime %lld\n", (long long)st.st_mtime );
    if (p_kmod_module_get_info( mod, &info ) >= 0)
    {
        for (l = info; l; l = p_kmod_list_next( info, l ))
        {
            const char *key = p_kmod_module_info_get_key( l ), *value = p_kmod_module_info_get_value( l );
            if (!key || !value || strchr( value, '\n' )) continue;
            for (k = keys; *k; k++) if (!strcmp( key, *k )) break;
            if (!*k || (!strcmp( key, "author" ) && author)) continue;   /* the first author only */
            if (!strcmp( key, "author" )) author = TRUE;
            put( o, "k %s %s\n", key, value );
        }
        p_kmod_module_info_free_list( info );
    }
    if ((info = p_kmod_module_get_dependencies( mod )))
    {
        for (l = info; l; l = p_kmod_list_next( info, l ))
        {
            struct kmod_module *dep = p_kmod_module_get_module( l );
            put( o, "k depends %s\n", p_kmod_module_get_name( dep ) );
            p_kmod_module_unref( dep );
        }
        p_kmod_module_unref_list( info );
    }
    p_kmod_module_unref( mod );
}

static void put_cpu( struct out *o )
{
    char line[512], *colon;
    BOOL vendor = FALSE;
    FILE *f = fopen( "/proc/cpuinfo", "r" );

    if (!f) return;
    while (fgets( line, sizeof(line), f ))
    {
        if (!(colon = strchr( line, ':' ))) continue;
        colon[strcspn( colon, "\n" )] = 0;
        if (!vendor && !strncmp( line, "vendor_id", 9 ))
        {
            put( o, "G vendor %s\n", colon + 2 );
            vendor = TRUE;
        }
        if (strncmp( line, "model name", 10 )) continue;
        put( o, "G cpu %s\n", colon + 2 );
        break;
    }
    fclose( f );
}

static void put_dma( struct out *o )
{
    char line[256];
    unsigned int channel;
    int n;
    FILE *f = fopen( "/proc/dma", "r" );

    if (!f) return;
    while (fgets( line, sizeof(line), f ))
    {
        if (sscanf( line, " %u: %n", &channel, &n ) < 1) continue;
        line[strcspn( line, "\n" )] = 0;
        put( o, "G dma %u %s\n", channel, line + n );
    }
    fclose( f );
}

static struct out report_text( void )
{
    struct out o = {0};
    struct modules mods = {0};
    struct kmod_ctx *ctx = NULL;
    struct utsname uts;
    size_t i;

    uname( &uts );
    put( &o, "G release %s\n", uts.release );
    put_cpu( &o );
    put_dma( &o );
    walk( &o, "/sys/devices", "", 0, &mods );

    if (load_kmod()) ctx = p_kmod_new( NULL, NULL );
    if (ctx)
    {
        struct kmod_list *loaded = NULL, *l;
        if (p_kmod_module_new_from_loaded( ctx, &loaded ) >= 0)
        {
            for (l = loaded; l; l = p_kmod_list_next( loaded, l ))
            {
                struct kmod_module *mod = p_kmod_module_get_module( l );
                put( &o, "G loaded %s\n", p_kmod_module_get_name( mod ) );
                add_module( &mods, p_kmod_module_get_name( mod ) );
                p_kmod_module_unref( mod );
            }
            p_kmod_module_unref_list( loaded );
        }
    }
    for (i = 0; i < mods.count; i++)
    {
        put_module( &o, ctx, mods.names[i], uts.release );
        free( mods.names[i] );
    }
    free( mods.names );
    if (ctx) p_kmod_unref( ctx );
    return o;
}

/* The report is made on the first call and kept for the second one that brings a buffer large enough. */
NTSTATUS hw_list( void *args )
{
    static struct out last;
    struct hw_list_params *params = args;
    char *buffer = (char *)(ULONG_PTR)params->buffer;

    if (!buffer || !last.data)
    {
        free( last.data );
        last = report_text();
        if (last.failed || !last.data) { free( last.data ); memset( &last, 0, sizeof(last) ); params->err = ERROR_OUTOFMEMORY; return STATUS_SUCCESS; }
    }
    params->needed = last.len + 1;
    if (!buffer || params->size < params->needed) { params->err = ERROR_MORE_DATA; return STATUS_SUCCESS; }
    memcpy( buffer, last.data, last.len + 1 );
    free( last.data );
    memset( &last, 0, sizeof(last) );
    params->err = ERROR_SUCCESS;
    return STATUS_SUCCESS;
}
