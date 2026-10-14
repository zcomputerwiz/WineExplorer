/* SPDX-License-Identifier: GPL-2.0-or-later */
DWORD wayland_host_bind(UINT64 window, HWND host);
DWORD wayland_host_configure(UINT64 window);
DWORD wayland_host_release(UINT64 window);
void wayland_host_stop(void);
