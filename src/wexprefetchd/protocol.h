/*
 * wexprefetchd wire protocol, shared by the daemon, the test client and the Wine loader (ntdll/unix/virtual.c
 * carries a copy of the constants it needs; keep them in sync).
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

#ifndef WEXPREFETCHD_PROTOCOL_H
#define WEXPREFETCHD_PROTOCOL_H

#include <stdint.h>

/*
 * Transport: AF_UNIX, SOCK_SEQPACKET, socket path below (systemd socket activation passes it as fd 3; the
 * socket is mode 0666).  One request and one reply per connection; the client then closes.
 *
 * Request: struct wexpf_request in one datagram, with exactly ONE file descriptor in SCM_RIGHTS: an open,
 * readable regular file (the PE image).  The daemon never accepts a path.  Everything about the file (device,
 * inode, size, times, contents) is taken from that descriptor by the daemon, never from the client.
 *
 * Reply: struct wexpf_reply in one datagram.  On WEXPF_OK it carries ONE file descriptor in SCM_RIGHTS, opened
 * O_RDONLY: the "flat image" described below.  On any other status no descriptor is attached and the client must
 * fall back to its own mapping path.
 *
 * Flat image (layout version 1): a file of image_size bytes (sparse; image_size is OptionalHeader.SizeOfImage
 * rounded up to 4096).  For every section that is not (IMAGE_SCN_MEM_SHARED and IMAGE_SCN_MEM_WRITE) and has
 * PointerToRawData != 0 and SizeOfRawData != 0, the bytes of the source file starting at
 *     file_start = PointerToRawData & ~0x1ff
 * and of length
 *     file_size  = min( round_up( PointerToRawData + SizeOfRawData, 0x200 ) - file_start, map_size )
 * where map_size = round_up( VirtualSize ? VirtualSize : SizeOfRawData, max( SectionAlignment, 0x1000 ) ),
 * clipped to the end of the source file, are stored at file offset VirtualAddress.  This is exactly the data Wine
 * reads for that section.  Everything else is a hole (reads as zero).  Headers are not stored.  A client maps
 * a section with mmap( base + VirtualAddress, round_up( file_size, 4096 ), prot, MAP_FIXED | MAP_PRIVATE, fd,
 * VirtualAddress ).  The flat image is only usable for images with SectionAlignment >= 4096 and every
 * VirtualAddress a multiple of 4096; otherwise the daemon answers WEXPF_UNSUPPORTED.
 */

#define WEXPF_SOCKET_PATH     "/run/wineexplorer/prefetch.sock"
#define WEXPF_SOCKET_ENV      "WINE_PREFETCH_SOCKET"   /* client override, used by tests */
#define WEXPF_MAGIC           0x31465058u              /* "XPF1" little endian */
#define WEXPF_VERSION         1u
#define WEXPF_OP_GET_IMAGE    1u
#define WEXPF_LAYOUT_FLAT     1u
#define WEXPF_MAX_IMAGE_SIZE  (1u << 30)               /* source files and images above 1 GiB are refused */

enum wexpf_status
{
    WEXPF_OK            = 0,
    WEXPF_BAD_REQUEST   = 1,   /* malformed datagram, wrong version or descriptor count */
    WEXPF_NOT_PE        = 2,   /* not a PE image, or headers inconsistent with the file */
    WEXPF_TOO_LARGE     = 3,
    WEXPF_BUSY          = 4,   /* too many concurrent requests: try the fallback */
    WEXPF_IO_ERROR      = 5,   /* cache directory full or unwritable */
    WEXPF_DISABLED      = 6,   /* administrator disabled the service */
    WEXPF_UNSUPPORTED   = 7    /* valid PE the flat layout cannot represent */
};

struct wexpf_request
{
    uint32_t magic;     /* WEXPF_MAGIC */
    uint32_t version;   /* WEXPF_VERSION */
    uint32_t op;        /* WEXPF_OP_GET_IMAGE */
    uint32_t flags;     /* reserved, 0 */
};

struct wexpf_reply
{
    uint32_t magic;       /* WEXPF_MAGIC */
    uint32_t version;     /* WEXPF_VERSION */
    int32_t  status;      /* enum wexpf_status */
    uint32_t layout;      /* WEXPF_LAYOUT_FLAT on success */
    uint64_t image_size;  /* size of the flat image in bytes */
    char     sha256[65];  /* lowercase hex digest of the source file, NUL terminated; empty on failure */
    char     pad[7];
};

#endif /* WEXPREFETCHD_PROTOCOL_H */
