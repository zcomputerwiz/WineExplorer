/*
 * Device Manager - the computer's devices, as Windows XP would show them
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

/* The devices are the Linux kernel's (wexhost reads sysfs); Wine's own virtual devices are not hardware.  A sysfs
 * node becomes a device when Windows would have one for it: the PCI functions, the legacy devices ACPI describes,
 * USB devices and their interfaces, HID top-level collections, disks, monitors ...  Nodes Windows has no device for
 * (SCSI targets, sound cards, network interfaces of an adapter) stand for nothing or for the device above them. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winnls.h>
#include <winreg.h>
#include <cfgmgr32.h>

#include "wexhost.h"
#include "devmgr.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(devmgr);

static const struct
{
    const WCHAR *key;          /* the class key's "Class" value */
    const WCHAR *guid;
    const WCHAR *module;       /* the icon on Windows XP: module (xpres pack) and id */
    int xp_icon;
    int own_icon;              /* ours */
}
classes[CLASS_COUNT] =
{
    [CLASS_BATTERY]   = { L"Battery",         L"{72631e54-78a4-11d0-bcf7-00aa00b7b32a}", L"batt", 2, IDI_BATTERY },
    [CLASS_BLUETOOTH] = { L"Bluetooth",       L"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}", L"setupapi", 5, 5 },
    [CLASS_CDROM]     = { L"CDROM",           L"{4d36e965-e325-11ce-bfc1-08002be10318}", L"setupapi", 51, 51 },
    [CLASS_COMPUTER]  = { L"Computer",        L"{4d36e966-e325-11ce-bfc1-08002be10318}", L"setupapi", 27, 27 },
    [CLASS_DISKDRIVE] = { L"DiskDrive",       L"{4d36e967-e325-11ce-bfc1-08002be10318}", L"setupapi", 53, 53 },
    [CLASS_DISPLAY]   = { L"Display",         L"{4d36e968-e325-11ce-bfc1-08002be10318}", L"setupapi", 1, 1 },
    [CLASS_FDC]       = { L"fdc",             L"{4d36e969-e325-11ce-bfc1-08002be10318}", L"setupapi", 9, 9 },
    [CLASS_FLOPPY]    = { L"FloppyDisk",      L"{4d36e980-e325-11ce-bfc1-08002be10318}", L"setupapi", 50, 50 },
    [CLASS_HDC]       = { L"hdc",             L"{4d36e96a-e325-11ce-bfc1-08002be10318}", L"setupapi", 9, 9 },
    [CLASS_HID]       = { L"HIDClass",        L"{745a17a0-74d3-11d0-b6fe-00a0c90f57da}", L"setupapi", 24, 24 },
    [CLASS_1394]      = { L"1394",            L"{6bdd1fc1-810f-11d0-bec7-08002be2092f}", L"setupapi", 21, 21 },
    [CLASS_IMAGE]     = { L"Image",           L"{6bdd1fc6-810f-11d0-bec7-08002be2092f}", L"mmsys", 3004, IDI_MEDIA },
    [CLASS_INFRARED]  = { L"Infrared",        L"{6bdd1fc5-810f-11d0-bec7-08002be2092f}", L"setupapi", 22, 22 },
    [CLASS_KEYBOARD]  = { L"Keyboard",        L"{4d36e96b-e325-11ce-bfc1-08002be10318}", L"setupapi", 3, 3 },
    [CLASS_MODEM]     = { L"Modem",           L"{4d36e96d-e325-11ce-bfc1-08002be10318}", L"setupapi", 22, 22 },
    [CLASS_MONITOR]   = { L"Monitor",         L"{4d36e96e-e325-11ce-bfc1-08002be10318}", L"setupapi", 1, 1 },
    [CLASS_MOUSE]     = { L"Mouse",           L"{4d36e96f-e325-11ce-bfc1-08002be10318}", L"setupapi", 2, 2 },
    [CLASS_NET]       = { L"Net",             L"{4d36e972-e325-11ce-bfc1-08002be10318}", L"setupapi", 5, 5 },
    [CLASS_LEGACY]    = { L"LegacyDriver",    L"{8ecc055d-047f-11d1-a537-0000f8753ed1}", L"setupapi", 19, 19 },
    [CLASS_UNKNOWN]   = { L"Unknown",         L"{4d36e97e-e325-11ce-bfc1-08002be10318}", L"setupapi", 18, 18 },
    [CLASS_PCMCIA]    = { L"PCMCIA",          L"{4d36e977-e325-11ce-bfc1-08002be10318}", L"setupapi", 11, 11 },
    [CLASS_PORTS]     = { L"Ports",           L"{4d36e978-e325-11ce-bfc1-08002be10318}", L"setupapi", 23, 23 },
    [CLASS_PRINTER]   = { L"Printer",         L"{4d36e979-e325-11ce-bfc1-08002be10318}", L"setupapi", 4, 4 },
    [CLASS_PROCESSOR] = { L"Processor",       L"{50127dc3-0f36-415e-a6cc-4cb3be910b65}", L"setupapi", 28, 28 },
    [CLASS_SCSI]      = { L"SCSIAdapter",     L"{4d36e97b-e325-11ce-bfc1-08002be10318}", L"setupapi", 10, 10 },
    [CLASS_SDHOST]    = { L"SDHost",          L"{a0a588a4-c46f-4b37-b7ea-c82fe89870c6}", L"setupapi", 11, 11 },
    [CLASS_SMARTCARD] = { L"SmartCardReader", L"{50dd5230-ba8a-11d1-bf5d-0000f805f530}", L"setupapi", 25, 25 },
    [CLASS_MEDIA]     = { L"MEDIA",           L"{4d36e96c-e325-11ce-bfc1-08002be10318}", L"mmsys", 3004, IDI_MEDIA },
    [CLASS_VOLUME]    = { L"Volume",          L"{71a27cdd-812a-11d0-bec7-08002be2092f}", L"setupapi", 53, 53 },
    [CLASS_SYSTEM]    = { L"System",          L"{4d36e97d-e325-11ce-bfc1-08002be10318}", L"setupapi", 27, 27 },
    [CLASS_TAPE]      = { L"TapeDrive",       L"{6d807884-7d21-11cf-801c-08002be10318}", L"setupapi", 52, 52 },
    [CLASS_USB]       = { L"USB",             L"{36fc9e60-c465-11cf-8056-444553540000}", L"setupapi", 20, 20 },
};

/* the devices ACPI describes, by ACPI or PnP id */
static const struct { const char *id; int cls; int name; } acpi_ids[] =
{
    { "LNXSYSTM", CLASS_SYSTEM, DEVNAME_ACPI_SYSTEM },
    { "LNXPWRBN", CLASS_SYSTEM, DEVNAME_FIXED_BUTTON },
    { "LNXSLPBN", CLASS_SYSTEM, DEVNAME_SLEEP_BUTTON },
    { "LNXTHERM", CLASS_SYSTEM, DEVNAME_THERMAL },
    { "PNP0C0C",  CLASS_SYSTEM, DEVNAME_POWER_BUTTON },
    { "PNP0C0E",  CLASS_SYSTEM, DEVNAME_SLEEP_BUTTON },
    { "PNP0C0D",  CLASS_SYSTEM, DEVNAME_LID },
    { "PNP0A03",  CLASS_SYSTEM, DEVNAME_PCI_BUS },
    { "PNP0A08",  CLASS_SYSTEM, DEVNAME_PCI_BUS },
    { "PNP0000",  CLASS_SYSTEM, DEVNAME_PIC },
    { "PNP0001",  CLASS_SYSTEM, DEVNAME_PIC },
    { "PNP0100",  CLASS_SYSTEM, DEVNAME_TIMER },
    { "PNP0103",  CLASS_SYSTEM, DEVNAME_HPET },
    { "PNP0200",  CLASS_SYSTEM, DEVNAME_DMA },
    { "PNP0B00",  CLASS_SYSTEM, DEVNAME_RTC },
    { "PNP0800",  CLASS_SYSTEM, DEVNAME_SPEAKER },
    { "PNP0C01",  CLASS_SYSTEM, DEVNAME_SYSTEM_BOARD },
    { "PNP0C02",  CLASS_SYSTEM, DEVNAME_MOTHERBOARD },
    { "PNP0C04",  CLASS_SYSTEM, DEVNAME_FPU },
    { "PNP0C09",  CLASS_SYSTEM, DEVNAME_EC },
    { "PNP0C0B",  CLASS_SYSTEM, DEVNAME_FAN },
    { "PNP0C14",  CLASS_SYSTEM, DEVNAME_WMI },
    { "PNP0303",  CLASS_KEYBOARD, DEVNAME_PS2_KEYBOARD },
    { "PNP030B",  CLASS_KEYBOARD, DEVNAME_PS2_KEYBOARD },
    { "PNP0F03",  CLASS_MOUSE, DEVNAME_PS2_MOUSE },
    { "PNP0F13",  CLASS_MOUSE, DEVNAME_PS2_MOUSE },
    { "PNP0400",  CLASS_PORTS, DEVNAME_LPT },
    { "PNP0401",  CLASS_PORTS, DEVNAME_LPT },
    { "PNP0500",  CLASS_PORTS, DEVNAME_COM },
    { "PNP0501",  CLASS_PORTS, DEVNAME_COM },
    { "PNP0700",  CLASS_FDC, DEVNAME_FDC },
    { "PNP0C0A",  CLASS_BATTERY, DEVNAME_BATTERY },
    { "ACPI0003", CLASS_BATTERY, DEVNAME_AC },
};

HINSTANCE devmgr_instance;
static const char *cpu_vendor;   /* "GenuineIntel" */

WCHAR *load_string( UINT id )
{
    static WCHAR buffer[8][512];
    static unsigned int next;
    WCHAR *s = buffer[next++ % ARRAY_SIZE(buffer)];

    if (!LoadStringW( devmgr_instance, id, s, ARRAY_SIZE(buffer[0]) )) s[0] = 0;
    return s;
}

WCHAR *utf8_to_wide( const char *str, WCHAR *buffer, DWORD size )
{
    if (!str || !MultiByteToWideChar( CP_UTF8, 0, str, -1, buffer, size )) buffer[0] = 0;
    buffer[size - 1] = 0;
    return buffer;
}

/* the genuine Windows XP resources, when a locally built resource pack is installed (docs/ASSETS.md) */
HMODULE xp_resources( const WCHAR *module )
{
    static struct { const WCHAR *name; HMODULE handle; BOOL tried; } packs[8];
    WCHAR path[MAX_PATH];
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(packs) && packs[i].name; i++)
        if (!wcscmp( packs[i].name, module )) return packs[i].handle;
    if (i == ARRAY_SIZE(packs)) return NULL;
    packs[i].name = module;
    GetWindowsDirectoryW( path, MAX_PATH );
    swprintf( path + wcslen( path ), MAX_PATH - wcslen( path ), L"\\Resources\\Icons\\xpres_%s.dll", module );
    packs[i].handle = LoadLibraryExW( path, NULL, LOAD_LIBRARY_AS_DATAFILE );
    return packs[i].handle;
}

HICON class_icon( int cls, int size )
{
    HMODULE pack = xp_resources( classes[cls].module );
    HICON icon = NULL;

    if (pack) icon = LoadImageW( pack, MAKEINTRESOURCEW(classes[cls].xp_icon), IMAGE_ICON, size, size, 0 );
    if (!icon) icon = LoadImageW( devmgr_instance, MAKEINTRESOURCEW(classes[cls].own_icon), IMAGE_ICON, size, size, 0 );
    return icon;
}

const WCHAR *class_guid( int cls ) { return classes[cls].guid; }
const WCHAR *class_key( int cls ) { return classes[cls].key; }

const char *dev_get( const struct device *dev, char tag, const char *key )
{
    unsigned int i;
    for (i = 0; i < dev->kv_count; i++)
        if (dev->kv[i].tag == tag && !strcmp( dev->kv[i].key, key )) return dev->kv[i].value;
    return NULL;
}

static unsigned int get_hex( const struct device *dev, const char *key )
{
    const char *value = dev_get( dev, 'A', key );
    return value ? strtoul( value, NULL, 16 ) : 0;
}

static const char *sysname( const struct device *dev )
{
    const char *p = strrchr( dev->path, '/' );
    return p ? p + 1 : dev->path;
}

static BOOL is_child_of( const struct device *child, const struct device *parent )
{
    size_t len = strlen( parent->path );
    return !strncmp( child->path, parent->path, len ) && child->path[len] == '/';
}

static struct device *find_device( struct hardware *hw, const char *path )
{
    unsigned int i;
    if (!path) return NULL;
    for (i = 0; i < hw->count; i++) if (!strcmp( hw->devs[i].path, path )) return &hw->devs[i];
    return NULL;
}

/* the nearest node above dev in sysfs that wexhost reported */
static struct device *sysfs_parent( struct hardware *hw, const struct device *dev )
{
    char path[1024], *p;

    lstrcpynA( path, dev->path, sizeof(path) );
    while ((p = strrchr( path, '/' )))
    {
        struct device *parent;
        *p = 0;
        if ((parent = find_device( hw, path ))) return parent;
    }
    return NULL;
}

struct module *find_module( struct hardware *hw, const char *name )
{
    char wanted[256], have[256], *p;
    unsigned int i;

    if (!name) return NULL;
    lstrcpynA( wanted, name, sizeof(wanted) );
    if ((p = strchr( wanted, ' ' ))) *p = 0;   /* "i8042 kbd" is the driver i8042 registered */
    for (p = wanted; *p; p++) if (*p == '-') *p = '_';
    for (i = 0; i < hw->mod_count; i++)
    {
        lstrcpynA( have, hw->mods[i].name, sizeof(have) );
        for (p = have; *p; p++) if (*p == '-') *p = '_';
        if (!strcmp( have, wanted ) && (hw->mods[i].path || !strcmp( hw->mods[i].name, name ))) return &hw->mods[i];
    }
    return NULL;
}

static void set_name( struct device *dev, const char *utf8 )
{
    char clean[256];
    int i;

    lstrcpynA( clean, utf8, sizeof(clean) );
    for (i = 0; clean[i]; i++) if (clean[i] == '_') clean[i] = ' ';
    while (i && clean[i - 1] == ' ') clean[--i] = 0;
    utf8_to_wide( clean, dev->name, ARRAY_SIZE(dev->name) );
}

static void set_generic_name( struct device *dev, int name )
{
    lstrcpynW( dev->name, load_string( IDS_DEV_BASE + name ), ARRAY_SIZE(dev->name) );
}

static void show( struct device *dev, int cls, int name )
{
    dev->cls = cls;
    if (name >= 0) set_generic_name( dev, name );
}

/* PCI: the class code tells the setup class; functions without a driver are "Other devices", except those Linux
 * does not need one for (bridges, system peripherals) */
static void classify_pci( struct device *dev )
{
    unsigned int code = get_hex( dev, "class" ) >> 8, base = code >> 8;
    const char *model = dev_get( dev, 'P', "ID_MODEL_FROM_DATABASE" );
    int cls;

    switch (base)
    {
    case 0x01: cls = code == 0x0101 || code == 0x0106 ? CLASS_HDC : CLASS_SCSI; break;
    case 0x02: cls = CLASS_NET; break;
    case 0x03: cls = CLASS_DISPLAY; break;
    case 0x04: cls = code == 0x0403 ? CLASS_SYSTEM : CLASS_MEDIA; break;   /* HD Audio controller: the bus */
    case 0x07: cls = code == 0x0700 ? CLASS_PORTS : code == 0x0703 ? CLASS_MODEM : CLASS_SYSTEM; break;
    case 0x0c:
        cls = code == 0x0c03 ? CLASS_USB : code == 0x0c00 ? CLASS_1394 : code == 0x0c04 ? CLASS_SCSI : CLASS_SYSTEM;
        break;
    case 0x0d: cls = code == 0x0d11 ? CLASS_BLUETOOTH : CLASS_NET; break;
    case 0x08: cls = code == 0x0805 ? CLASS_SDHOST : CLASS_SYSTEM; break;
    default: cls = CLASS_SYSTEM; break;
    }
    if (!dev->driver && base != 0x06 && base != 0x08 && base != 0x05 && base != 0x0b && base != 0x11 && code != 0x0c05)
    {
        cls = CLASS_UNKNOWN;
        dev->problem = CM_PROB_FAILED_INSTALL;
    }
    dev->cls = cls;
    if (model) set_name( dev, model );
    else set_generic_name( dev, DEVNAME_PCI_DEVICE );
}

static const struct device *find_child( struct hardware *hw, const struct device *dev, const char *subsystem )
{
    unsigned int i;
    for (i = 0; i < hw->count; i++)
        if (!strcmp( hw->devs[i].subsystem, subsystem ) && is_child_of( &hw->devs[i], dev )) return &hw->devs[i];
    return NULL;
}

/* what a USB function is, by its driver */
static void classify_usb_function( struct hardware *hw, struct device *dev, const char *driver )
{
    if (!driver) { dev->cls = CLASS_UNKNOWN; dev->problem = CM_PROB_FAILED_INSTALL; set_generic_name( dev, DEVNAME_UNKNOWN ); }
    else if (!strcmp( driver, "usbhid" )) show( dev, CLASS_HID, DEVNAME_USB_HID );
    else if (!strcmp( driver, "usb-storage" ) || !strcmp( driver, "uas" )) show( dev, CLASS_USB, DEVNAME_USB_STORAGE );
    else if (!strcmp( driver, "snd-usb-audio" )) show( dev, CLASS_MEDIA, DEVNAME_USB_AUDIO );
    else if (!strcmp( driver, "uvcvideo" )) show( dev, CLASS_IMAGE, DEVNAME_USB_VIDEO );
    else if (!strcmp( driver, "btusb" )) show( dev, CLASS_BLUETOOTH, DEVNAME_USB_BLUETOOTH );
    else if (!strcmp( driver, "usblp" )) show( dev, CLASS_USB, DEVNAME_USB_PRINTING );
    else if (find_child( hw, dev, "net" )) show( dev, CLASS_NET, -1 );
    else show( dev, CLASS_USB, DEVNAME_USB_DEVICE );
}

static void classify_usb( struct hardware *hw, struct device *dev )
{
    const char *type = dev_get( dev, 'E', "DEVTYPE" ), *product = dev_get( dev, 'A', "product" );
    unsigned int i, interfaces = 0;
    struct device *only = NULL;

    if (type && !strcmp( type, "usb_interface" ))
    {
        struct device *parent = sysfs_parent( hw, dev );
        unsigned int count = parent ? strtoul( dev_get( parent, 'A', "bNumInterfaces" ) ? dev_get( parent, 'A', "bNumInterfaces" ) : "1", NULL, 10 ) : 1;
        if (count > 1 && dev->driver && strcmp( dev->driver, "hub" ))
        {
            classify_usb_function( hw, dev, dev->driver );
            if (dev->cls == CLASS_NET && dev_get( parent, 'A', "product" )) set_name( dev, dev_get( parent, 'A', "product" ) );
        }
        else dev->owner = parent;
        return;
    }
    if (!type || strcmp( type, "usb_device" )) return;

    if (get_hex( dev, "bDeviceClass" ) == 9)
    {
        struct device *parent = sysfs_parent( hw, dev );
        show( dev, CLASS_USB, parent && !strcmp( parent->subsystem, "pci" ) ? DEVNAME_USB_ROOT_HUB : DEVNAME_USB_HUB );
        if ((only = (struct device *)find_child( hw, dev, "usb" )) && only->driver)
        {
            dev->driver = only->driver;   /* "hub", not the generic "usb" */
            dev->module = only->module;
        }
        return;
    }
    for (i = 0; i < hw->count; i++)
    {
        const char *t = dev_get( &hw->devs[i], 'E', "DEVTYPE" );
        if (t && !strcmp( t, "usb_interface" ) && is_child_of( &hw->devs[i], dev ) &&
            !strchr( hw->devs[i].path + strlen( dev->path ) + 1, '/' ))
        {
            interfaces++;
            only = &hw->devs[i];
        }
    }
    if (interfaces > 1) show( dev, CLASS_USB, DEVNAME_USB_COMPOSITE );
    else
    {
        classify_usb_function( hw, dev, only ? only->driver : NULL );
        if (only && only->driver)
        {
            dev->driver = only->driver;   /* the function's driver, not the generic "usb" */
            dev->module = only->module;
        }
        if (dev->cls == CLASS_NET || dev->cls == CLASS_UNKNOWN)
        {
            if (product) set_name( dev, product );
            else if (dev_get( dev, 'P', "ID_MODEL_FROM_DATABASE" )) set_name( dev, dev_get( dev, 'P', "ID_MODEL_FROM_DATABASE" ) );
        }
        if (only && dev->problem && only->driver) dev->problem = 0;
    }
}

/* input devices: the top-level collections of HID devices, and keyboards and mice of other buses */
static void classify_input( struct hardware *hw, struct device *dev )
{
    struct device *parent = sysfs_parent( hw, dev );
    BOOL keyboard = !!dev_get( dev, 'P', "ID_INPUT_KEYBOARD" ), mouse = dev_get( dev, 'P', "ID_INPUT_MOUSE" ) ||
         dev_get( dev, 'P', "ID_INPUT_TOUCHPAD" ), joystick = !!dev_get( dev, 'P', "ID_INPUT_JOYSTICK" );
    const char *name = dev_get( dev, 'A', "name" );

    if (!dev_get( dev, 'E', "PRODUCT" )) return;   /* event, mouse, js nodes: not devices */
    if (!parent || !strcmp( parent->subsystem, "serio" ) || !strcmp( parent->subsystem, "sound" ) ||
        !strcmp( parent->subsystem, "acpi" ) || !strcmp( parent->subsystem, "platform" )) return;
    if (!strcmp( parent->subsystem, "hid" ))
    {
        if (keyboard) show( dev, CLASS_KEYBOARD, DEVNAME_HID_KEYBOARD );
        else if (mouse) show( dev, CLASS_MOUSE, DEVNAME_HID_MOUSE );
        else if (joystick) show( dev, CLASS_HID, DEVNAME_HID_GAME );
        else if (name && strstr( name, "Consumer Control" )) show( dev, CLASS_HID, DEVNAME_HID_CONSUMER );
        else show( dev, CLASS_HID, DEVNAME_HID_DEVICE );
        dev->driver = parent->driver;
        dev->module = parent->module;
        return;
    }
    if (keyboard) dev->cls = CLASS_KEYBOARD;
    else if (mouse) dev->cls = CLASS_MOUSE;
    else if (joystick) dev->cls = CLASS_HID;
    else return;
    if (name) set_name( dev, name );
}

static void classify_block( struct hardware *hw, struct device *dev )
{
    const char *type = dev_get( dev, 'E', "DEVTYPE" ), *model = dev_get( dev, 'P', "ID_MODEL" ), *bus = dev_get( dev, 'P', "ID_BUS" );
    const char *name = sysname( dev );
    struct device *parent = sysfs_parent( hw, dev );
    char text[256];

    if (type && !strcmp( type, "partition" ))
    {
        if (!strncmp( dev->path, "virtual/", 8 )) return;
        show( dev, CLASS_VOLUME, DEVNAME_VOLUME );
        dev->hidden = TRUE;
        return;
    }
    if (!strncmp( dev->path, "virtual/", 8 ))
    {
        if (!strncmp( name, "loop", 4 ) && !strcmp( dev_get( dev, 'A', "size" ) ? dev_get( dev, 'A', "size" ) : "0", "0" )) return;
        dev->cls = CLASS_DISKDRIVE;
        dev->hidden = TRUE;
        set_name( dev, name );
        return;
    }
    dev->cls = !strncmp( name, "sr", 2 ) || (dev_get( dev, 'A', "device/type" ) && !strcmp( dev_get( dev, 'A', "device/type" ), "5" ))
               ? CLASS_CDROM : !strncmp( name, "fd", 2 ) ? CLASS_FLOPPY : CLASS_DISKDRIVE;
    if (!model)
    {
        snprintf( text, sizeof(text), "%s %s", dev_get( dev, 'A', "device/vendor" ) ? dev_get( dev, 'A', "device/vendor" ) : "",
                  dev_get( dev, 'A', "device/model" ) ? dev_get( dev, 'A', "device/model" ) : name );
        model = text[0] == ' ' ? text + 1 : text;
    }
    if (bus && !strcmp( bus, "usb" ))
    {
        WCHAR wide[200];
        snprintf( text, sizeof(text), "%s %s", dev_get( dev, 'P', "ID_VENDOR" ) ? dev_get( dev, 'P', "ID_VENDOR" ) : "", model );
        set_name( dev, text[0] == ' ' ? text + 1 : text );
        lstrcpynW( wide, dev->name, ARRAY_SIZE(wide) );
        swprintf( dev->name, ARRAY_SIZE(dev->name), load_string( IDS_DEV_BASE + DEVNAME_USB_DISK ), wide );
    }
    else set_name( dev, model );
    /* the disk driver is the SCSI device's (sd, sr) */
    if (parent && !strcmp( parent->subsystem, "scsi" ) && parent->driver)
    {
        dev->driver = parent->driver;
        dev->module = parent->module;
    }
}

static void classify_net( struct hardware *hw, struct device *dev )
{
    if (strncmp( dev->path, "virtual/", 8 ))
    {
        dev->owner = sysfs_parent( hw, dev );   /* the adapter */
        return;
    }
    dev->cls = CLASS_NET;
    dev->hidden = TRUE;
    if (!strcmp( sysname( dev ), "lo" )) set_generic_name( dev, DEVNAME_LOOPBACK );
    else set_name( dev, sysname( dev ) );
}

static void classify_drm( struct hardware *hw, struct device *dev )
{
    const char *type = dev_get( dev, 'E', "DEVTYPE" ), *status = dev_get( dev, 'A', "status" );
    const char *name = dev_get( dev, 'A', "edid_name" );

    if (!type || strcmp( type, "drm_connector" ) || !status || strcmp( status, "connected" )) return;
    dev->cls = CLASS_MONITOR;
    if (name) set_name( dev, name );
    else set_generic_name( dev, dev_get( dev, 'A', "edid_vendor" ) ? DEVNAME_PNP_MONITOR : DEVNAME_DEFAULT_MONITOR );
}

static int acpi_id( const char *id )
{
    unsigned int i;
    if (!id) return -1;
    for (i = 0; i < ARRAY_SIZE(acpi_ids); i++)
        if (!strncmp( id, acpi_ids[i].id, strlen( acpi_ids[i].id ) )) return i;
    return -1;
}

static void classify_acpi_name( struct device *dev, int index )
{
    show( dev, acpi_ids[index].cls, acpi_ids[index].name );
    if (acpi_ids[index].name == DEVNAME_COM || acpi_ids[index].name == DEVNAME_LPT)
    {
        WCHAR format[64];
        const char *uid = dev->acpi ? dev_get( dev->acpi, 'A', "uid" ) : dev_get( dev, 'A', "uid" );
        lstrcpynW( format, dev->name, ARRAY_SIZE(format) );
        swprintf( dev->name, ARRAY_SIZE(dev->name), format, (uid ? atoi( uid ) : 0) + 1 );
    }
}

static void classify( struct hardware *hw, struct device *dev )
{
    const char *s = dev->subsystem;
    int index;

    if (!strcmp( s, "pci" )) classify_pci( dev );
    else if (!strcmp( s, "usb" )) classify_usb( hw, dev );
    else if (!strcmp( s, "input" )) classify_input( hw, dev );
    else if (!strcmp( s, "block" )) classify_block( hw, dev );
    else if (!strcmp( s, "net" )) classify_net( hw, dev );
    else if (!strcmp( s, "drm" )) classify_drm( hw, dev );
    else if (!strcmp( s, "hdaudio" ))
    {
        char text[256];
        snprintf( text, sizeof(text), "%s %s", dev_get( dev, 'A', "vendor_name" ) ? dev_get( dev, 'A', "vendor_name" ) : "",
                  dev_get( dev, 'A', "chip_name" ) ? dev_get( dev, 'A', "chip_name" ) : "High Definition Audio" );
        dev->cls = CLASS_MEDIA;
        set_name( dev, text[0] == ' ' ? text + 1 : text );
    }
    else if (!strcmp( s, "cpu" ))
    {
        dev->cls = CLASS_PROCESSOR;
        set_name( dev, hw->cpu ? hw->cpu : sysname( dev ) );
    }
    else if (!strcmp( s, "pnp" ) && (index = acpi_id( dev_get( dev, 'A', "id" ) )) >= 0) classify_acpi_name( dev, index );
    else if (!strcmp( s, "platform" ) && dev->acpi && (index = acpi_id( dev_get( dev->acpi, 'A', "hid" ) )) >= 0)
        classify_acpi_name( dev, index );
    else if (!strcmp( s, "power_supply" ))
    {
        const char *type = dev_get( dev, 'A', "type" );
        struct device *parent = sysfs_parent( hw, dev );
        if (!type || (strcmp( type, "Battery" ) && strcmp( type, "Mains" ))) return;
        if (parent && parent->cls >= 0) { dev->owner = parent; return; }
        show( dev, CLASS_BATTERY, strcmp( type, "Mains" ) ? DEVNAME_BATTERY : DEVNAME_AC );
    }
    else if (!strcmp( s, "tty" ))
    {
        struct device *parent = sysfs_parent( hw, dev );
        const char *type = dev_get( dev, 'A', "type" );
        if (strncmp( sysname( dev ), "ttyS", 4 ) || !type || !strcmp( type, "0" )) return;
        if (parent && parent->cls >= 0) { dev->owner = parent; return; }
        dev->cls = CLASS_PORTS;
        swprintf( dev->name, ARRAY_SIZE(dev->name), load_string( IDS_DEV_BASE + DEVNAME_COM ), atoi( sysname( dev ) + 4 ) + 1 );
    }
    /* hid, sound, scsi ... stand for nothing: their children or parents are the devices */
}

/* ACPI nodes: merged into the device they describe, or devices of their own when Windows has one */
static void link_acpi( struct hardware *hw )
{
    unsigned int i;

    for (i = 0; i < hw->count; i++)
    {
        struct device *dev = &hw->devs[i], *acpi;
        const char *physical;

        if ((acpi = find_device( hw, dev->firmware )) && acpi != dev)
        {
            if (!dev->acpi) dev->acpi = acpi;
            if (!acpi->owner) acpi->owner = dev;
        }
        if (strcmp( dev->subsystem, "acpi" ) || !(physical = dev_get( dev, 'A', "physical_node" ))) continue;
        if ((acpi = find_device( hw, physical )) && !dev->owner)
        {
            dev->owner = acpi;
            if (!acpi->acpi) acpi->acpi = dev;
        }
    }
}

static void classify_acpi_nodes( struct hardware *hw )
{
    unsigned int i;
    int index;

    for (i = 0; i < hw->count; i++)
    {
        struct device *dev = &hw->devs[i];
        const char *status = dev_get( dev, 'A', "status" ), *physical = dev_get( dev, 'A', "physical_node" );

        if (strcmp( dev->subsystem, "acpi" ) || dev->owner) continue;
        if (status && !(atoi( status ) & 1)) continue;   /* not present */
        if ((index = acpi_id( dev_get( dev, 'A', "hid" ) )) < 0) continue;
        if (acpi_ids[index].cls == CLASS_BATTERY) continue;   /* the power supplies are */
        /* the PCI root bus is no sysfs device: its ACPI node stands for it */
        if (physical && find_device( hw, physical )) continue;
        classify_acpi_name( dev, index );
    }
}

/* the device above dev, as "Devices by connection" shows it */
static struct device *raw_parent( struct hardware *hw, const struct device *dev )
{
    struct device *parent = sysfs_parent( hw, dev );
    unsigned int i;

    if (parent) return parent;
    if (dev->acpi && (parent = sysfs_parent( hw, dev->acpi ))) return parent;
    /* the PCI root bus: the ACPI node whose device it is */
    for (i = 0; i < hw->count; i++)
    {
        const char *physical = dev_get( &hw->devs[i], 'A', "physical_node" );
        size_t len = physical ? strlen( physical ) : 0;
        if (len && !strncmp( dev->path, physical, len ) && dev->path[len] == '/' && &hw->devs[i] != dev)
            return &hw->devs[i];
    }
    return NULL;
}

static struct device *shown_parent( struct hardware *hw, struct device *dev )
{
    struct device *parent = raw_parent( hw, dev );
    unsigned int depth;

    for (depth = 0; parent && depth < 64; depth++)
    {
        struct device *shown = parent;
        while (shown->owner && shown->owner != shown && shown->owner != dev) shown = shown->owner;
        if (shown != dev && shown->cls >= 0 && !shown->owner) return shown;
        parent = raw_parent( hw, parent );
    }
    return hw->computer;
}

static struct device *add_device( struct hardware *hw )
{
    struct device *dev;

    if (hw->count == hw->size)
    {
        struct device *devs = realloc( hw->devs, (hw->size = hw->size * 2 + 64) * sizeof(*devs) );
        if (!devs) return NULL;
        hw->devs = devs;
    }
    dev = &hw->devs[hw->count++];
    memset( dev, 0, sizeof(*dev) );
    dev->subsystem = "";
    dev->cls = -1;
    return dev;
}

static void add_kv( struct device *dev, char tag, char *text )
{
    char *space = strchr( text, ' ' );

    if (dev->kv_count == dev->kv_size)
    {
        struct kv *kv = realloc( dev->kv, (dev->kv_size = dev->kv_size * 2 + 16) * sizeof(*kv) );
        if (!kv) return;
        dev->kv = kv;
    }
    if (space) *space = 0;
    dev->kv[dev->kv_count].tag = tag;
    dev->kv[dev->kv_count].key = text;
    dev->kv[dev->kv_count].value = space ? space + 1 : "";
    dev->kv_count++;
}

static void add_resource( struct device *dev, const char *text )
{
    struct resource res;
    char type[8];
    ULONGLONG a = 0, b = 0;

    if (sscanf( text, "%7s", type ) != 1) return;
    if (!strcmp( type, "io" ) || !strcmp( type, "mem" ))
    {
        if (sscanf( text + strlen( type ), "%llx %llx", &a, &b ) != 2) return;
        res.type = type[0] == 'i' ? RES_IO : RES_MEMORY;
    }
    else if (!strcmp( type, "irq" ) || !strcmp( type, "dma" ))
    {
        if (sscanf( text + strlen( type ), "%llu", &a ) != 1) return;
        b = a;
        res.type = type[0] == 'i' ? RES_IRQ : RES_DMA;
    }
    else return;
    res.start = a;
    res.end = b;
    if (dev->res_count == dev->res_size)
    {
        struct resource *r = realloc( dev->res, (dev->res_size = dev->res_size * 2 + 4) * sizeof(*r) );
        if (!r) return;
        dev->res = r;
    }
    dev->res[dev->res_count++] = res;
}

static struct module *add_module( struct hardware *hw, const char *name )
{
    struct module *mod;

    if (hw->mod_count == hw->mod_size)
    {
        struct module *mods = realloc( hw->mods, (hw->mod_size = hw->mod_size * 2 + 64) * sizeof(*mods) );
        if (!mods) return NULL;
        hw->mods = mods;
    }
    mod = &hw->mods[hw->mod_count++];
    memset( mod, 0, sizeof(*mod) );
    mod->name = name;
    return mod;
}

static void parse( struct hardware *hw, char *text, const char ***loaded, unsigned int *loaded_count )
{
    struct device *dev = NULL;
    struct module *mod = NULL;
    char *line, *next;
    unsigned int loaded_size = 0;

    for (line = text; line && *line; line = next)
    {
        char *rest;
        if ((next = strchr( line, '\n' ))) *next++ = 0;
        if (!line[0] || line[1] != ' ') continue;
        rest = line + 2;
        switch (line[0])
        {
        case 'G':
            if (!strncmp( rest, "release ", 8 )) hw->release = rest + 8;
            else if (!strncmp( rest, "cpu ", 4 )) hw->cpu = rest + 4;
            else if (!strncmp( rest, "vendor ", 7 )) hw->vendor = rest + 7;
            else if (!strncmp( rest, "dma ", 4 ) && atoi( rest + 4 ) < 8) hw->dma[atoi( rest + 4 )] = 0;
            else if (!strncmp( rest, "loaded ", 7 ))
            {
                if (*loaded_count == loaded_size)
                {
                    const char **l = realloc( (void *)*loaded, (loaded_size = loaded_size * 2 + 64) * sizeof(*l) );
                    if (!l) break;
                    *loaded = l;
                }
                (*loaded)[(*loaded_count)++] = rest + 7;
            }
            break;
        case 'D': if ((dev = add_device( hw ))) dev->path = rest; mod = NULL; break;
        case 'S': if (dev) dev->subsystem = rest; break;
        case 'R': if (dev) dev->driver = rest; break;
        case 'M': if (dev) dev->module = rest; break;
        case 'F': if (dev) dev->firmware = rest; break;
        case 'A': case 'E': case 'P': if (dev) add_kv( dev, line[0], rest ); break;
        case 'X': if (dev) add_resource( dev, rest ); break;
        case 'K': mod = add_module( hw, rest ); dev = NULL; break;
        case 'k':
            if (!mod) break;
            if (!strncmp( rest, "path ", 5 )) mod->path = rest + 5;
            else if (!strncmp( rest, "mtime ", 6 )) mod->mtime = _atoi64( rest + 6 );
            else if (!strncmp( rest, "builtin ", 8 )) mod->builtin = TRUE;
            else if (!strncmp( rest, "author ", 7 )) mod->author = rest + 7;
            else if (!strncmp( rest, "description ", 12 )) mod->description = rest + 12;
            else if (!strncmp( rest, "license ", 8 )) mod->license = rest + 8;
            else if (!strncmp( rest, "version ", 8 )) mod->version = rest + 8;
            else if (!strncmp( rest, "signer ", 7 )) mod->signer = rest + 7;
            else if (!strncmp( rest, "vermagic ", 9 )) mod->vermagic = rest + 9;
            else if (!strncmp( rest, "depends ", 8 )) mod->depends = rest + 8;
            break;
        }
    }
}

struct hardware *hardware_load( void )
{
    struct hardware *hw = calloc( 1, sizeof(*hw) );
    const char **loaded = NULL;
    unsigned int i, j, loaded_count = 0, count;
    DWORD needed = 0, err;
    struct device *dev;

    if (!hw) return NULL;
    memset( hw->dma, 0xff, sizeof(hw->dma) );
    err = WexHardwareList( NULL, 0, &needed );
    if (err == ERROR_MORE_DATA && (hw->text = malloc( needed )))
        err = WexHardwareList( hw->text, needed, &needed );
    if (err) WARN( "no hardware report: %lu\n", err );
    else parse( hw, hw->text, &loaded, &loaded_count );
    cpu_vendor = hw->vendor;

    /* synthetic devices: the computer, and the drivers that drive no device ("Non-Plug and Play Drivers") */
    if ((dev = add_device( hw )))
    {
        dev->path = "";
        dev->subsystem = "computer";
        dev->cls = CLASS_COMPUTER;
    }
    count = hw->count;
    for (i = 0; i < loaded_count; i++)
    {
        BOOL bound = FALSE;
        for (j = 0; j < count && !bound; j++)
            bound = hw->devs[j].module && !strcmp( hw->devs[j].module, loaded[i] );
        if (!bound && (dev = add_device( hw )))
        {
            dev->path = loaded[i];
            dev->subsystem = "module";
            dev->driver = dev->module = loaded[i];
            dev->cls = CLASS_LEGACY;
            dev->hidden = TRUE;
        }
    }
    free( loaded );
    hw->computer = &hw->devs[count - 1];

    /* from here on the array stays as it is: pointers into it are kept */
    for (i = 0; i < hw->count; i++) if (!strcmp( hw->devs[i].subsystem, "cpu" )) hw->cpus++;
    set_generic_name( hw->computer, hw->cpus > 1 ? DEVNAME_ACPI_MULTI : DEVNAME_ACPI_UNI );
    link_acpi( hw );
    /* parents before children: a USB interface asks its device, a power supply its platform device */
    for (i = 0; i < hw->count; i++)
        if (!strcmp( hw->devs[i].subsystem, "platform" ) || !strcmp( hw->devs[i].subsystem, "pnp" ) ||
            !strcmp( hw->devs[i].subsystem, "pci" ))
            classify( hw, &hw->devs[i] );
    for (i = 0; i < hw->count; i++)
        if (strcmp( hw->devs[i].subsystem, "platform" ) && strcmp( hw->devs[i].subsystem, "pnp" ) &&
            strcmp( hw->devs[i].subsystem, "pci" ) && strcmp( hw->devs[i].subsystem, "computer" ) &&
            strcmp( hw->devs[i].subsystem, "module" ))
            classify( hw, &hw->devs[i] );
    classify_acpi_nodes( hw );

    /* PS/2 keyboards and mice are ACPI's devices; without ACPI the serio ports are */
    for (i = 0; i < hw->count; i++)
    {
        dev = &hw->devs[i];
        if (strcmp( dev->subsystem, "serio" ) || !dev->driver) continue;
        for (j = 0; j < hw->count; j++)
            if (hw->devs[j].cls == CLASS_KEYBOARD || hw->devs[j].cls == CLASS_MOUSE)
                if (!strcmp( hw->devs[j].subsystem, "pnp" )) break;
        if (j < hw->count) continue;
        if (!strcmp( dev->driver, "atkbd" )) show( dev, CLASS_KEYBOARD, DEVNAME_PS2_KEYBOARD );
        else if (!strcmp( dev->driver, "psmouse" )) show( dev, CLASS_MOUSE, DEVNAME_PS2_MOUSE );
    }

    for (i = 0; i < hw->count; i++)
    {
        dev = &hw->devs[i];
        if (dev->cls < 0) continue;
        if (dev->owner == dev) dev->owner = NULL;
        if ((dev->mod = find_module( hw, dev->module ? dev->module : dev->driver ))) dev->mod->bound = TRUE;
    }
    for (i = 0; i < hw->count; i++)
    {
        const char *irq;
        int index;

        dev = &hw->devs[i];
        /* a PCI function's interrupt line (the resource file has the windows only) */
        if (!strcmp( dev->subsystem, "pci" ) && (irq = dev_get( dev, 'A', "irq" )) && atoi( irq ) > 0)
        {
            char text[16];
            snprintf( text, sizeof(text), "irq %u", atoi( irq ) );
            add_resource( dev, text );
        }
        /* the DMA channels the kernel reserved belong to the DMA controller */
        index = acpi_id( dev_get( dev->acpi ? dev->acpi : dev, 'A', "hid" ) );
        if (dev->cls != CLASS_SYSTEM || index < 0 || acpi_ids[index].name != DEVNAME_DMA) continue;
        for (j = 0; j < ARRAY_SIZE(hw->dma); j++)
        {
            char text[16];
            if (hw->dma[j]) continue;
            snprintf( text, sizeof(text), "dma %u", j );
            add_resource( dev, text );
        }
    }
    for (i = 0; i < hw->count; i++)
    {
        dev = &hw->devs[i];
        if (dev->cls < 0 || dev->owner || dev == hw->computer) continue;
        dev->parent = shown_parent( hw, dev );
        if (dev->parent == dev) dev->parent = hw->computer;
    }
    TRACE( "%u nodes, %u modules\n", hw->count, hw->mod_count );
    return hw;
}

void hardware_free( struct hardware *hw )
{
    unsigned int i;

    if (!hw) return;
    for (i = 0; i < hw->count; i++)
    {
        free( hw->devs[i].kv );
        free( hw->devs[i].res );
    }
    free( hw->devs );
    free( hw->mods );
    free( hw->text );
    free( hw );
}

static unsigned int hash( const char *s )
{
    unsigned int h = 2166136261u;
    while (s && *s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void upper_id( char *s )
{
    for (; *s; s++)
    {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
        else if (*s == ' ' || *s == '-') *s = '_';
    }
}

const WCHAR *dev_enumerator( const struct device *dev )
{
    const char *s = dev->subsystem, *bus = dev_get( dev, 'P', "ID_BUS" );

    if (!strcmp( s, "pci" )) return L"PCI";
    if (!strcmp( s, "usb" )) return L"USB";
    if (!strcmp( s, "input" )) return L"HID";
    if (!strcmp( s, "acpi" ) || !strcmp( s, "pnp" ) || !strcmp( s, "platform" ) || !strcmp( s, "cpu" ) ||
        !strcmp( s, "power_supply" ) || !strcmp( s, "computer" ) || !strcmp( s, "serio" )) return L"ACPI";
    if (!strcmp( s, "hdaudio" )) return L"HDAUDIO";
    if (!strcmp( s, "drm" )) return L"DISPLAY";
    if (!strcmp( s, "block" ))
    {
        if (dev->cls == CLASS_VOLUME) return L"STORAGE";
        if (!strncmp( dev->path, "virtual/", 8 )) return L"ROOT";
        if (bus && !strcmp( bus, "ata" )) return L"IDE";
        if (bus && !strcmp( bus, "usb" )) return L"USBSTOR";
        return L"SCSI";
    }
    if (!strcmp( s, "net" ) || !strcmp( s, "module" )) return L"ROOT";
    return L"LINUX";
}

/* the hardware ids without the instance part; buffer gets them \n separated, the most specific first */
static void ids( const struct device *dev, BOOL compatible, char *buffer, size_t size )
{
    const char *s = dev->subsystem;
    size_t len = 0;

#define ADD(...) do { if (len < size) len += snprintf( buffer + len, size - len, __VA_ARGS__ ); \
                      if (len < size) buffer[len++] = '\n'; } while (0)
    buffer[0] = 0;
    if (!strcmp( s, "pci" ))
    {
        unsigned int ven = get_hex( dev, "vendor" ), id = get_hex( dev, "device" ), rev = get_hex( dev, "revision" );
        unsigned int sub = get_hex( dev, "subsystem_device" ) << 16 | get_hex( dev, "subsystem_vendor" );
        unsigned int code = get_hex( dev, "class" );
        if (!compatible)
        {
            ADD( "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X&REV_%02X", ven, id, sub, rev );
            ADD( "PCI\\VEN_%04X&DEV_%04X&SUBSYS_%08X", ven, id, sub );
            ADD( "PCI\\VEN_%04X&DEV_%04X&CC_%06X", ven, id, code );
            ADD( "PCI\\VEN_%04X&DEV_%04X&CC_%04X", ven, id, code >> 8 );
        }
        else
        {
            ADD( "PCI\\VEN_%04X&DEV_%04X&REV_%02X", ven, id, rev );
            ADD( "PCI\\VEN_%04X&DEV_%04X", ven, id );
            ADD( "PCI\\VEN_%04X&CC_%06X", ven, code );
            ADD( "PCI\\VEN_%04X&CC_%04X", ven, code >> 8 );
            ADD( "PCI\\VEN_%04X", ven );
            ADD( "PCI\\CC_%06X", code );
            ADD( "PCI\\CC_%04X", code >> 8 );
        }
    }
    else if (!strcmp( s, "usb" ))
    {
        const char *type = dev_get( dev, 'E', "DEVTYPE" );
        const char *product = dev_get( dev, 'E', "PRODUCT" ), *iface = dev_get( dev, 'E', "INTERFACE" );
        unsigned int vid = 0, pid = 0, rev = 0, c = 0, sc = 0, p = 0;
        if (product) sscanf( product, "%x/%x/%x", &vid, &pid, &rev );
        if (iface) sscanf( iface, "%u/%u/%u", &c, &sc, &p );
        else c = get_hex( dev, "bDeviceClass" );
        if (!compatible)
        {
            if (type && !strcmp( type, "usb_interface" ))
                ADD( "USB\\VID_%04X&PID_%04X&MI_%02X", vid, pid, get_hex( dev, "bInterfaceNumber" ) );
            else
            {
                ADD( "USB\\VID_%04X&PID_%04X&REV_%04X", vid, pid, rev );
                ADD( "USB\\VID_%04X&PID_%04X", vid, pid );
            }
        }
        else if (iface)
        {
            ADD( "USB\\Class_%02X&SubClass_%02X&Prot_%02X", c, sc, p );
            ADD( "USB\\Class_%02X&SubClass_%02X", c, sc );
            ADD( "USB\\Class_%02X", c );
        }
        else if (dev_get( dev, 'A', "bNumInterfaces" ) && atoi( dev_get( dev, 'A', "bNumInterfaces" ) ) > 1)
        {
            ADD( "USB\\DevClass_00&SubClass_00&Prot_00" );
            ADD( "USB\\COMPOSITE" );
        }
        else ADD( "USB\\Class_%02X", c );
    }
    else if (!strcmp( s, "input" ))
    {
        unsigned int bus = 0, vid = 0, pid = 0;
        sscanf( dev_get( dev, 'E', "PRODUCT" ) ? dev_get( dev, 'E', "PRODUCT" ) : "", "%x/%x/%x", &bus, &vid, &pid );
        if (!compatible) ADD( "HID\\VID_%04X&PID_%04X", vid, pid );
        else
        {
            ADD( "HID_DEVICE_SYSTEM_%s", dev->cls == CLASS_KEYBOARD ? "KEYBOARD" : dev->cls == CLASS_MOUSE ? "MOUSE" : "CONSUMER" );
            ADD( "HID_DEVICE" );
        }
    }
    else if (!strcmp( s, "hdaudio" ))
    {
        const char *alias = dev_get( dev, 'E', "MODALIAS" );
        unsigned int id = 0, rev = 0;
        if (alias) sscanf( alias, "hdaudio:v%8xr%8x", &id, &rev );
        if (!compatible)
        {
            ADD( "HDAUDIO\\FUNC_01&VEN_%04X&DEV_%04X&REV_%04X", id >> 16, id & 0xffff, rev & 0xffff );
            ADD( "HDAUDIO\\FUNC_01&VEN_%04X&DEV_%04X", id >> 16, id & 0xffff );
        }
        else ADD( "HDAUDIO\\FUNC_01" );
    }
    else if (!strcmp( s, "block" ) && dev->cls != CLASS_VOLUME && strncmp( dev->path, "virtual/", 8 ))
    {
        char vendor[64] = "", model[64] = "", rev[16] = "";
        lstrcpynA( vendor, dev_get( dev, 'A', "device/vendor" ) ? dev_get( dev, 'A', "device/vendor" ) : "", sizeof(vendor) );
        lstrcpynA( model, dev_get( dev, 'A', "device/model" ) ? dev_get( dev, 'A', "device/model" ) : "", sizeof(model) );
        lstrcpynA( rev, dev_get( dev, 'A', "device/rev" ) ? dev_get( dev, 'A', "device/rev" ) : "", sizeof(rev) );
        upper_id( vendor );
        upper_id( model );
        if (!compatible)
        {
            const char *type = dev->cls == CLASS_CDROM ? "CdRom" : "Disk";
            ADD( "%S\\%s%s_%s_%s", dev_enumerator( dev ), type, vendor, model, rev );
            ADD( "%S\\%s%s_%s", dev_enumerator( dev ), type, vendor, model );
            ADD( "%S\\%s%s", dev_enumerator( dev ), type, vendor );
        }
        else ADD( "GenDisk" );
    }
    else if (!strcmp( s, "drm" ))
    {
        const char *vendor = dev_get( dev, 'A', "edid_vendor" ), *product = dev_get( dev, 'A', "edid_product" );
        if (!compatible) ADD( vendor ? "MONITOR\\%s%s" : "MONITOR\\Default_Monitor", vendor, product );
        else ADD( "*PNP09FF" );
    }
    else if (dev->acpi || !strcmp( s, "acpi" ) || !strcmp( s, "pnp" ))
    {
        const char *id = dev_get( dev, 'A', "hid" ), *alias = dev_get( dev, 'E', "MODALIAS" ), *p;
        unsigned int family = 0, model = 0;

        if (!id && dev->acpi) id = dev_get( dev->acpi, 'A', "hid" );
        if (!id) id = dev_get( dev, 'A', "id" );
        if (!strcmp( s, "cpu" ) && alias && (p = strstr( alias, "fam" )) && sscanf( p, "fam%4xmod%4x", &family, &model ) == 2)
        {
            if (!compatible) ADD( "ACPI\\%s_-_x86_Family_%u_Model_%u", cpu_vendor ? cpu_vendor : "", family, model );
            ADD( "*%s_-_x86_Family_%u_Model_%u", cpu_vendor ? cpu_vendor : "", family, model );
        }
        else if (id) { if (!compatible) ADD( "ACPI\\%s", id ); ADD( "*%s", id ); }
    }
    else if (!strcmp( s, "computer" ))
    {
        if (!compatible) ADD( "acpiapic_mp" );
    }
    if (len) buffer[len - 1] = 0;
#undef ADD
}

void dev_hardware_ids( const struct device *dev, BOOL compatible, WCHAR *buffer, DWORD size )
{
    char text[1024];
    ids( dev, compatible, text, sizeof(text) );
    utf8_to_wide( text, buffer, size );
}

void dev_instance_id( const struct device *dev, WCHAR *buffer, DWORD size )
{
    char text[1024], *nl;
    const char *s = dev->subsystem, *slot;
    unsigned int h = hash( dev->path );

    ids( dev, FALSE, text, sizeof(text) );
    if ((nl = strchr( text, '\n' ))) *nl = 0;
    if (!strcmp( s, "pci" ) && (slot = dev_get( dev, 'E', "PCI_SLOT_NAME" )))
    {
        unsigned int bus = 0, device = 0, function = 0;
        sscanf( slot, "%*x:%x:%x.%x", &bus, &device, &function );
        if ((nl = strstr( text, "&REV_" )) && strlen( nl ) > 7) nl[7] = 0;
        swprintf( buffer, size, L"%S\\3&%08X&0&%02X", text, hash( slot ) & 0xfffffff, device << 3 | function );
    }
    else if (!strcmp( s, "usb" ) && dev_get( dev, 'A', "serial" ) && !strstr( text, "&MI_" ))
    {
        char vidpid[64];
        lstrcpynA( vidpid, text, sizeof(vidpid) );
        if ((nl = strstr( vidpid, "&REV_" ))) *nl = 0;
        swprintf( buffer, size, L"%S\\%S", vidpid, dev_get( dev, 'A', "serial" ) );
    }
    else if (!strcmp( s, "computer" )) swprintf( buffer, size, L"ROOT\\ACPI_HAL\\0000" );
    else if (!strcmp( s, "module" ))
    {
        char name[64];
        lstrcpynA( name, dev->path, sizeof(name) );
        upper_id( name );
        swprintf( buffer, size, L"ROOT\\LEGACY_%S\\0000", name );
    }
    else if (!strcmp( s, "net" )) swprintf( buffer, size, L"ROOT\\NET\\%04u", h % 10000 );
    else if (dev->cls == CLASS_VOLUME) swprintf( buffer, size, L"STORAGE\\VOLUME\\1&%08X&0&%S", h, sysname( dev ) );
    else if (text[0]) swprintf( buffer, size, L"%S\\%u&%08X&0&%u", text, strchr( dev->path, '/' ) ? 4 : 1, h,
                                atoi( sysname( dev ) + strcspn( sysname( dev ), "0123456789" ) ) );
    else swprintf( buffer, size, L"%s\\%S", dev_enumerator( dev ), dev->path );
}

void dev_manufacturer( const struct device *dev, WCHAR *buffer, DWORD size )
{
    const char *s = dev->subsystem, *value = NULL;
    UINT id = IDS_STANDARD_DEVICES;

    if (!strcmp( s, "pci" ) || !strcmp( s, "usb" ))
    {
        value = dev_get( dev, 'P', "ID_VENDOR_FROM_DATABASE" );
        if (!value) value = dev_get( dev, 'A', "manufacturer" );
    }
    else if (!strcmp( s, "hdaudio" )) value = dev_get( dev, 'A', "vendor_name" );
    else if (!strcmp( s, "cpu" ) && cpu_vendor)
        value = !strcmp( cpu_vendor, "GenuineIntel" ) ? "Intel" : !strcmp( cpu_vendor, "AuthenticAMD" ) ? "AMD" : cpu_vendor;
    else if (dev->cls == CLASS_DISKDRIVE) id = IDS_STD_DISK;
    else if (dev->cls == CLASS_CDROM) id = IDS_STD_CDROM;
    else if (dev->cls == CLASS_MONITOR) id = IDS_STD_MONITOR;
    else if (dev->cls == CLASS_KEYBOARD) id = IDS_STD_KEYBOARD;
    else if (dev->cls == CLASS_COMPUTER) id = IDS_STD_COMPUTER;
    else if (dev->cls == CLASS_NET) id = IDS_STD_NET;
    else if (dev->cls == CLASS_MOUSE || dev->cls == CLASS_BATTERY || dev->cls == CLASS_VOLUME) id = IDS_MICROSOFT;
    if (value)
    {
        char clean[256];
        lstrcpynA( clean, value, sizeof(clean) );
        utf8_to_wide( clean, buffer, size );
    }
    else lstrcpynW( buffer, load_string( id ), size );
}

void dev_location( const struct device *dev, WCHAR *buffer, DWORD size )
{
    const char *slot = dev_get( dev, 'E', "PCI_SLOT_NAME" ), *product = dev_get( dev, 'A', "product" );
    WCHAR wide[256];

    if (!strcmp( dev->subsystem, "pci" ) && slot)
    {
        unsigned int bus = 0, device = 0, function = 0;
        sscanf( slot, "%*x:%x:%x.%x", &bus, &device, &function );
        swprintf( buffer, size, load_string( IDS_LOCATION_PCI ), bus, device, function );
    }
    else if (!strcmp( dev->subsystem, "usb" ) && product && wcscmp( utf8_to_wide( product, wide, ARRAY_SIZE(wide) ), dev->name ))
        lstrcpynW( buffer, wide, size );
    else if (dev->parent && dev->parent->cls != CLASS_COMPUTER)
        swprintf( buffer, size, load_string( IDS_LOCATION_ON ), dev->parent->name );
    else lstrcpynW( buffer, load_string( IDS_UNKNOWN ), size );
}
