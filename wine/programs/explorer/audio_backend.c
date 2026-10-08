/*
 * WineExplorer Desktop Environment - Audio Control Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Wine's mmdevapi does not implement IAudioEndpointVolume (it reports a constant 100%), so master
 * volume/mute are read and written through the host sound server: PipeWire (wpctl), PulseAudio (pactl)
 * or ALSA (amixer).  A worker thread polls the server so the shell's UI thread never waits on a process.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "wine/debug.h"
#include "audio_backend.h"
#include "hostcmd.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define POLL_MS 1000

typedef enum { BACKEND_NONE, BACKEND_PIPEWIRE, BACKEND_PULSE, BACKEND_ALSA } BACKEND;

static CRITICAL_SECTION g_cs;
static BOOL g_cs_ready;
static BACKEND g_backend = BACKEND_NONE;
static HANDLE g_thread;
static volatile LONG g_stop;
static AUDIO_STATUS_INFO g_status;     /* guarded by g_cs */
static DWORD g_hold_until;             /* ignore polled values briefly after we changed something */

static const char *const wpctl_get[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
static const char *const pactl_vol[] = { "pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL };
static const char *const pactl_mute[] = { "pactl", "get-sink-mute", "@DEFAULT_SINK@", NULL };
static const char *const amixer_get[] = { "amixer", "sget", "Master", NULL };

/* Volume = first percentage found in text; digits immediately before the '%'. */
static int first_percent(const char *buf)
{
    const char *pct = strchr(buf, '%');
    if (!pct) return -1;
    while (pct > buf && pct[-1] >= '0' && pct[-1] <= '9') pct--;
    return atoi(pct);
}

/* Returns FALSE if the backend gave no usable answer. */
static BOOL query(BACKEND backend, int *volume, BOOL *mute)
{
    char buf[512];

    switch (backend)
    {
    case BACKEND_PIPEWIRE:
    {
        const char *p;
        float v = 0;
        if (!hostcmd_capture(wpctl_get, buf, sizeof(buf), 1500) || !(p = strstr(buf, "Volume:"))) return FALSE;
        if (sscanf(p + 7, "%f", &v) != 1) return FALSE;
        *volume = max(0, min(100, (int)(v * 100.0f + 0.5f)));
        *mute = strstr(buf, "[MUTED]") != NULL;
        return TRUE;
    }
    case BACKEND_PULSE:
        if (!hostcmd_capture(pactl_vol, buf, sizeof(buf), 1500) || (*volume = first_percent(buf)) < 0) return FALSE;
        *volume = min(100, *volume);
        *mute = hostcmd_capture(pactl_mute, buf, sizeof(buf), 1500) && strstr(buf, "yes") != NULL;
        return TRUE;
    case BACKEND_ALSA:
        if (!hostcmd_capture(amixer_get, buf, sizeof(buf), 1500) || (*volume = first_percent(buf)) < 0) return FALSE;
        *volume = min(100, *volume);
        *mute = strstr(buf, "[off]") != NULL;
        return TRUE;
    default:
        return FALSE;
    }
}

static const char *backend_name(BACKEND b)
{
    return b == BACKEND_PIPEWIRE ? "PipeWire" : b == BACKEND_PULSE ? "PulseAudio" : "ALSA";
}

static DWORD WINAPI poll_thread(void *arg)
{
    static const BACKEND order[] = { BACKEND_PIPEWIRE, BACKEND_PULSE, BACKEND_ALSA };

    while (!g_stop)
    {
        int vol = 0, i;
        BOOL mute = FALSE, ok = FALSE;
        BACKEND found = BACKEND_NONE;

        for (i = 0; i < 3 && !ok; i++)
        {
            BACKEND b = (g_backend != BACKEND_NONE) ? g_backend : order[i];
            ok = query(b, &vol, &mute);
            if (ok) found = b;
            if (g_backend != BACKEND_NONE) break;   /* once detected, only ask that one */
        }

        EnterCriticalSection(&g_cs);
        if (ok)
        {
            if (g_backend == BACKEND_NONE) TRACE("audio backend: %s\n", backend_name(found));
            g_backend = found;
            g_status.available = TRUE;
            if ((int)(GetTickCount() - g_hold_until) >= 0)
            {
                g_status.volume_percent = vol;
                g_status.is_muted = mute;
            }
            lstrcpyW(g_status.device_name, L"Default audio device");
        }
        else
        {
            g_backend = BACKEND_NONE;   /* server may have gone away or not started yet: probe again */
            g_status.available = FALSE;
        }
        LeaveCriticalSection(&g_cs);

        {
            DWORD waited;
            for (waited = 0; waited < POLL_MS && !g_stop; waited += 50) Sleep(50);
        }
    }
    return 0;
}

void audio_backend_init(void)
{
    if (g_thread) return;
    if (!g_cs_ready) { InitializeCriticalSection(&g_cs); g_cs_ready = TRUE; }
    g_stop = 0;
    g_thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
}

void audio_backend_shutdown(void)
{
    if (!g_thread) return;
    g_stop = 1;
    WaitForSingleObject(g_thread, 3000);
    CloseHandle(g_thread);
    g_thread = NULL;
}

BOOL audio_backend_get_status(AUDIO_STATUS_INFO *info)
{
    if (!info) return FALSE;
    memset(info, 0, sizeof(*info));
    if (!g_cs_ready) return TRUE;
    EnterCriticalSection(&g_cs);
    *info = g_status;
    LeaveCriticalSection(&g_cs);
    return TRUE;
}

/* Publish a locally-made change immediately; polled values are ignored for a moment so the UI does not
 * flicker back to the old value before the server reflects the change. */
static void publish(int volume, int mute)
{
    EnterCriticalSection(&g_cs);
    if (volume >= 0) g_status.volume_percent = volume;
    if (mute >= 0) g_status.is_muted = mute;
    g_hold_until = GetTickCount() + 1500;
    LeaveCriticalSection(&g_cs);
}

BOOL audio_backend_set_volume(int percent)
{
    char arg[32];
    BACKEND b;

    if (!g_cs_ready) return FALSE;
    percent = max(0, min(100, percent));
    EnterCriticalSection(&g_cs);
    b = g_backend;
    LeaveCriticalSection(&g_cs);

    switch (b)
    {
    case BACKEND_PIPEWIRE:
    {
        const char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", arg, NULL };
        snprintf(arg, sizeof(arg), "%d.%02d", percent / 100, percent % 100);
        hostcmd_run_async(argv);
        break;
    }
    case BACKEND_PULSE:
    {
        const char *argv[] = { "pactl", "set-sink-volume", "@DEFAULT_SINK@", arg, NULL };
        snprintf(arg, sizeof(arg), "%d%%", percent);
        hostcmd_run_async(argv);
        break;
    }
    case BACKEND_ALSA:
    {
        const char *argv[] = { "amixer", "sset", "Master", arg, NULL };
        snprintf(arg, sizeof(arg), "%d%%", percent);
        hostcmd_run_async(argv);
        break;
    }
    default:
        return FALSE;
    }
    publish(percent, -1);
    return TRUE;
}

BOOL audio_backend_set_mute(BOOL mute)
{
    BACKEND b;

    if (!g_cs_ready) return FALSE;
    EnterCriticalSection(&g_cs);
    b = g_backend;
    LeaveCriticalSection(&g_cs);

    switch (b)
    {
    case BACKEND_PIPEWIRE:
    {
        const char *argv[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", mute ? "1" : "0", NULL };
        hostcmd_run_async(argv);
        break;
    }
    case BACKEND_PULSE:
    {
        const char *argv[] = { "pactl", "set-sink-mute", "@DEFAULT_SINK@", mute ? "1" : "0", NULL };
        hostcmd_run_async(argv);
        break;
    }
    case BACKEND_ALSA:
    {
        const char *argv[] = { "amixer", "sset", "Master", mute ? "mute" : "unmute", NULL };
        hostcmd_run_async(argv);
        break;
    }
    default:
        return FALSE;
    }
    publish(-1, mute);
    return TRUE;
}

BOOL audio_backend_toggle_mute(void)
{
    AUDIO_STATUS_INFO st;
    audio_backend_get_status(&st);
    return st.available && audio_backend_set_mute(!st.is_muted);
}

int audio_backend_step_volume(int delta_percent)
{
    AUDIO_STATUS_INFO st;
    int v;

    audio_backend_get_status(&st);
    if (!st.available) return 0;
    v = max(0, min(100, st.volume_percent + delta_percent));
    audio_backend_set_volume(v);
    return v;
}
