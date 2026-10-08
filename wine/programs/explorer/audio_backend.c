/*
 * WineExplorer Desktop Environment - Audio Control Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Master volume and mute of the default playback device through the standard Windows interface,
 * IAudioEndpointVolume (mmdevapi), which Wine forwards to the sound server (winepulse: PulseAudio/PipeWire).
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

#define COBJMACROS
#include <stdlib.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include "wine/debug.h"
#include "audio_backend.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static IMMDevice *g_device;
static IAudioEndpointVolume *g_volume;
static BOOL g_com_inited;
static DWORD g_last_probe_tick;
static WCHAR g_device_name[128];

/* Not in libuuid; defined here so initguid.h stays out of this file (it would redefine unknwn.h GUIDs). */
static const GUID clsid_mm_enumerator =
    { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
static const GUID iid_mm_enumerator =
    { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
static const GUID iid_endpoint_volume =
    { 0x5cdf2c82, 0x841e, 0x4546, { 0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a } };

/* PKEY_Device_FriendlyName {a45c254e-df1c-4efd-8020-67d146a850e0}, 14 */
static const PROPERTYKEY key_friendly_name =
    { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

static void release_endpoint(void)
{
    if (g_volume) { IAudioEndpointVolume_Release(g_volume); g_volume = NULL; }
    if (g_device) { IMMDevice_Release(g_device); g_device = NULL; }
    g_device_name[0] = 0;
}

static void read_device_name(void)
{
    IPropertyStore *props;
    PROPVARIANT pv;

    g_device_name[0] = 0;
    if (FAILED(IMMDevice_OpenPropertyStore(g_device, STGM_READ, &props))) return;
    PropVariantInit(&pv);
    if (SUCCEEDED(IPropertyStore_GetValue(props, &key_friendly_name, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
        lstrcpynW(g_device_name, pv.pwszVal, ARRAY_SIZE(g_device_name));
    PropVariantClear(&pv);
    IPropertyStore_Release(props);
}

/* Look for the default render endpoint; cheap enough to retry every few seconds when none exists. */
static BOOL acquire_endpoint(void)
{
    IMMDeviceEnumerator *enumerator;
    HRESULT hr;

    if (g_volume) return TRUE;
    if (!g_com_inited)
        g_com_inited = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));

    hr = CoCreateInstance(&clsid_mm_enumerator, NULL, CLSCTX_INPROC_SERVER, &iid_mm_enumerator, (void **)&enumerator);
    if (FAILED(hr)) return FALSE;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eMultimedia, &g_device);
    IMMDeviceEnumerator_Release(enumerator);
    if (FAILED(hr)) { g_device = NULL; return FALSE; }

    hr = IMMDevice_Activate(g_device, &iid_endpoint_volume, CLSCTX_INPROC_SERVER, NULL, (void **)&g_volume);
    if (FAILED(hr)) { release_endpoint(); return FALSE; }

    read_device_name();
    TRACE("audio endpoint %s\n", debugstr_w(g_device_name));
    return TRUE;
}

void audio_backend_init(void)
{
    g_last_probe_tick = GetTickCount();
    acquire_endpoint();
}

void audio_backend_shutdown(void)
{
    release_endpoint();
    if (g_com_inited) { CoUninitialize(); g_com_inited = FALSE; }
}

BOOL audio_backend_get_status(AUDIO_STATUS_INFO *info)
{
    float level = 0.0f;
    BOOL mute = FALSE;

    if (!info) return FALSE;
    memset(info, 0, sizeof(*info));

    if (!g_volume)
    {
        DWORD now = GetTickCount();
        if (now - g_last_probe_tick < 5000) return TRUE;
        g_last_probe_tick = now;
        if (!acquire_endpoint()) return TRUE;
    }

    /* E_NOTIMPL: the active driver cannot control the device volume, so there is nothing useful to show */
    if (FAILED(IAudioEndpointVolume_GetMasterVolumeLevelScalar(g_volume, &level)) ||
        FAILED(IAudioEndpointVolume_GetMute(g_volume, &mute)))
    {
        release_endpoint();  /* device went away; re-probe later */
        return TRUE;
    }

    info->available = TRUE;
    info->volume_percent = (int)(level * 100.0f + 0.5f);
    info->is_muted = mute;
    lstrcpynW(info->device_name, g_device_name, ARRAY_SIZE(info->device_name));
    return TRUE;
}

BOOL audio_backend_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (!g_volume) return FALSE;
    return SUCCEEDED(IAudioEndpointVolume_SetMasterVolumeLevelScalar(g_volume, percent / 100.0f, NULL));
}

BOOL audio_backend_set_mute(BOOL mute)
{
    if (!g_volume) return FALSE;
    return SUCCEEDED(IAudioEndpointVolume_SetMute(g_volume, mute, NULL));
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
