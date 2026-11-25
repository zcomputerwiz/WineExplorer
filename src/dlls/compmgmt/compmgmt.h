/*
 * Computer Management - what the console and the snap-in nodes share
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

#ifndef __COMPMGMT_PRIVATE_H
#define __COMPMGMT_PRIVATE_H

#include <windef.h>
#include <winbase.h>

extern HINSTANCE compmgmt_module;

/* the text of a string resource, in one of a ring of buffers */
const WCHAR *cm_string( UINT id );

/* a change was refused for want of rights: offers to open the console again as an administrator, and does so
 * (the console then closes). Returns TRUE when the new console was started. */
BOOL cm_offer_elevation( HWND owner );

/* the Services node (services.c) */
enum
{
    SVC_CAN_START = 1, SVC_CAN_STOP = 2, SVC_CAN_PAUSE = 4, SVC_CAN_RESUME = 8, SVC_CAN_RESTART = 16, SVC_SELECTED = 32
};

HWND svc_create_view( HWND parent );
void svc_refresh( HWND list );
void svc_free( HWND list );
DWORD svc_commands( HWND list );                          /* the SVC_ flags the selected service allows */
void svc_command( HWND list, HWND owner, UINT command );   /* IDM_SVC_START ... IDM_SVC_PROPERTIES */
void svc_context_menu( HWND list, HWND owner, int x, int y );

#endif
