/*
 * WineExplorer Desktop Environment - Audio Control Backend Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Master volume / mute of the host's default output, through IAudioEndpointVolume (mmdevapi -> winepulse -> PulseAudio/PipeWire).
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

#ifndef __WINE_EXPLORER_AUDIO_BACKEND_H
#define __WINE_EXPLORER_AUDIO_BACKEND_H

#include <windows.h>

typedef struct
{
    BOOL available;        /* FALSE when there is no usable audio device: callers must hide their UI */
    int volume_percent;    /* 0 to 100 */
    BOOL is_muted;
    WCHAR device_name[128];
} AUDIO_STATUS_INFO;

void audio_backend_init(void);
void audio_backend_shutdown(void);
BOOL audio_backend_get_status(AUDIO_STATUS_INFO *info);
BOOL audio_backend_set_volume(int percent);
BOOL audio_backend_set_mute(BOOL mute);
BOOL audio_backend_toggle_mute(void);
int audio_backend_step_volume(int delta_percent);

#endif /* __WINE_EXPLORER_AUDIO_BACKEND_H */
