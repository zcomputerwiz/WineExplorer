/*
 * Device Manager
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

#ifndef __DEVMGR_PRIVATE_H
#define __DEVMGR_PRIVATE_H

#include <windef.h>
#include <winbase.h>

/* Device setup classes, in the order of their names in the resources (IDS_CLASS_BASE) */
enum
{
    CLASS_BATTERY, CLASS_BLUETOOTH, CLASS_CDROM, CLASS_COMPUTER, CLASS_DISKDRIVE, CLASS_DISPLAY, CLASS_FDC,
    CLASS_FLOPPY, CLASS_HDC, CLASS_HID, CLASS_1394, CLASS_IMAGE, CLASS_INFRARED, CLASS_KEYBOARD, CLASS_MODEM,
    CLASS_MONITOR, CLASS_MOUSE, CLASS_NET, CLASS_LEGACY, CLASS_UNKNOWN, CLASS_PCMCIA, CLASS_PORTS, CLASS_PRINTER,
    CLASS_PROCESSOR, CLASS_SCSI, CLASS_SDHOST, CLASS_SMARTCARD, CLASS_MEDIA, CLASS_VOLUME, CLASS_SYSTEM, CLASS_TAPE,
    CLASS_USB, CLASS_COUNT
};

/* Names Windows gives devices whose driver names them generically (IDS_DEV_BASE) */
enum
{
    DEVNAME_ACPI_MULTI, DEVNAME_ACPI_UNI, DEVNAME_ACPI_SYSTEM, DEVNAME_FIXED_BUTTON, DEVNAME_POWER_BUTTON,
    DEVNAME_SLEEP_BUTTON, DEVNAME_LID, DEVNAME_PCI_BUS, DEVNAME_PIC, DEVNAME_TIMER, DEVNAME_HPET, DEVNAME_DMA,
    DEVNAME_RTC, DEVNAME_SPEAKER, DEVNAME_SYSTEM_BOARD, DEVNAME_MOTHERBOARD, DEVNAME_FPU, DEVNAME_EC, DEVNAME_FAN,
    DEVNAME_WMI, DEVNAME_THERMAL, DEVNAME_PS2_KEYBOARD, DEVNAME_PS2_MOUSE, DEVNAME_LPT, DEVNAME_COM, DEVNAME_FDC,
    DEVNAME_BATTERY, DEVNAME_AC, DEVNAME_USB_ROOT_HUB, DEVNAME_USB_HUB, DEVNAME_USB_COMPOSITE, DEVNAME_USB_HID,
    DEVNAME_USB_STORAGE, DEVNAME_USB_AUDIO, DEVNAME_USB_VIDEO, DEVNAME_USB_BLUETOOTH, DEVNAME_USB_PRINTING,
    DEVNAME_USB_DEVICE, DEVNAME_HID_KEYBOARD, DEVNAME_HID_MOUSE, DEVNAME_HID_CONSUMER, DEVNAME_HID_DEVICE,
    DEVNAME_HID_GAME, DEVNAME_PNP_MONITOR, DEVNAME_DEFAULT_MONITOR, DEVNAME_VOLUME, DEVNAME_LOOPBACK,
    DEVNAME_PCI_DEVICE, DEVNAME_UNKNOWN, DEVNAME_USB_DISK, DEVNAME_I2C_HID, DEVNAME_COUNT
};

enum { RES_MEMORY, RES_IO, RES_IRQ, RES_DMA, RES_COUNT };

struct kv { char tag; const char *key; const char *value; };
struct resource { int type; ULONGLONG start, end; };

struct module
{
    const char *name, *path, *author, *description, *license, *version, *signer, *vermagic, *depends;
    LONGLONG mtime;
    BOOL builtin;
    BOOL bound;        /* the driver of a device */
};

struct device
{
    const char *path;          /* below /sys/devices; "" for the computer */
    const char *subsystem, *driver, *module, *firmware;
    struct kv *kv;
    unsigned int kv_count, kv_size;
    struct resource *res;
    unsigned int res_count, res_size;
    struct device *acpi;       /* its ACPI node */
    struct device *owner;      /* not shown itself: the device that stands for it (or NULL) */
    struct device *parent;     /* by connection */
    struct module *mod;
    int cls;                   /* CLASS_*, -1 not a device of its own */
    BOOL hidden;               /* only with View > Show hidden devices */
    DWORD problem;             /* CM_PROB_*, 0 working properly */
    WCHAR name[256];
};

struct hardware
{
    char *text;
    struct device *devs;
    unsigned int count, size;
    struct module *mods;
    unsigned int mod_count, mod_size;
    struct device *computer;   /* "ACPI Multiprocessor PC" */
    const char *release, *cpu, *vendor;
    int dma[8];                /* channel in use by: -1 none, else 0 */
    unsigned int cpus;
};

extern HINSTANCE devmgr_instance;

/* hardware.c */
struct hardware *hardware_load( void );
void hardware_free( struct hardware *hw );
const char *dev_get( const struct device *dev, char tag, const char *key );
void dev_instance_id( const struct device *dev, WCHAR *buffer, DWORD size );
void dev_hardware_ids( const struct device *dev, BOOL compatible, WCHAR *buffer, DWORD size );   /* \n separated */
void dev_manufacturer( const struct device *dev, WCHAR *buffer, DWORD size );
void dev_location( const struct device *dev, WCHAR *buffer, DWORD size );
const WCHAR *dev_enumerator( const struct device *dev );
struct module *find_module( struct hardware *hw, const char *name );
HICON class_icon( int cls, int size );
const WCHAR *class_guid( int cls );
const WCHAR *class_key( int cls );
WCHAR *load_string( UINT id );
WCHAR *utf8_to_wide( const char *str, WCHAR *buffer, DWORD size );
HMODULE xp_resources( const WCHAR *module );

/* properties.c */
void device_properties( HWND owner, struct hardware *hw, struct device *dev );

#endif
