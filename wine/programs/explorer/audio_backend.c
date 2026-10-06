/*
 * WineExplorer Desktop Environment - Audio Control Backend Implementation
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements native audio control bridging Linux PipeWire / WirePlumber (wpctl),
 * PulseAudio (pactl), and ALSA (amixer) with Win32 waveOut fallback.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <mmsystem.h>

#include "wine/debug.h"
#include "audio_backend.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

static AUDIO_BACKEND_TYPE g_backend = AUDIO_BACKEND_UNKNOWN;
static int g_cached_volume = 75;
static BOOL g_cached_mute = FALSE;
static DWORD g_last_query_tick = 0;
static WCHAR g_cached_device[128] = L"Default Audio Device";

/* Execute command asynchronously without blocking UI */
static void run_cmd_async(const char *cmd)
{
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    char cmd_line[512];

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    snprintf(cmd_line, sizeof(cmd_line), "sh -c \"%s\" 2>/dev/null", cmd);
    if (CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

/* Execute command and capture standard output */
static BOOL run_cmd_capture(const char *cmd, char *out_buf, DWORD max_len)
{
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd_line[512];
    DWORD bytes_read = 0;
    BOOL success = FALSE;

    if (!out_buf || max_len == 0) return FALSE;
    out_buf[0] = '\0';

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return FALSE;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    memset(&pi, 0, sizeof(pi));
    snprintf(cmd_line, sizeof(cmd_line), "sh -c \"%s\"", cmd);

    if (CreateProcessA(NULL, cmd_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;

        if (WaitForSingleObject(pi.hProcess, 1000) == WAIT_OBJECT_0)
        {
            ReadFile(hReadPipe, out_buf, max_len - 1, &bytes_read, NULL);
            if (bytes_read > 0)
            {
                out_buf[bytes_read] = '\0';
                success = TRUE;
            }
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    if (hWritePipe) CloseHandle(hWritePipe);
    CloseHandle(hReadPipe);
    return success;
}

void audio_backend_init(void)
{
    char buf[256];

    /* 1. Test for PipeWire / WirePlumber (standard on modern Linux & Gamescope) */
    if (run_cmd_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf)) && strlen(buf) > 0)
    {
        g_backend = AUDIO_BACKEND_PIPEWIRE;
        lstrcpyW(g_cached_device, L"PipeWire Audio Sink");
        TRACE("Audio backend initialized: PipeWire (WirePlumber)\n");
        return;
    }

    /* 2. Test for PulseAudio */
    if (run_cmd_capture("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)) && strlen(buf) > 0)
    {
        g_backend = AUDIO_BACKEND_PULSEAUDIO;
        lstrcpyW(g_cached_device, L"PulseAudio Master Sink");
        TRACE("Audio backend initialized: PulseAudio\n");
        return;
    }

    /* 3. Test for ALSA */
    if (run_cmd_capture("amixer sget Master 2>/dev/null", buf, sizeof(buf)) && strlen(buf) > 0)
    {
        g_backend = AUDIO_BACKEND_ALSA;
        lstrcpyW(g_cached_device, L"ALSA Master Mixer");
        TRACE("Audio backend initialized: ALSA\n");
        return;
    }

    /* 4. Test for Win32 waveOut */
    {
        DWORD vol = 0;
        if (waveOutGetVolume(0, &vol) == MMSYSERR_NOERROR)
        {
            g_backend = AUDIO_BACKEND_WINMM;
            lstrcpyW(g_cached_device, L"Wave Audio Out");
            TRACE("Audio backend initialized: WinMM waveOut\n");
            return;
        }
    }

    /* 5. Fallback in-memory mock */
    g_backend = AUDIO_BACKEND_MOCK;
    lstrcpyW(g_cached_device, L"Simulated Audio Device");
    TRACE("Audio backend initialized: Mock / Fallback\n");
}

BOOL audio_backend_get_status(AUDIO_STATUS_INFO *info)
{
    char buf[512];
    DWORD now = GetTickCount();

    if (!info) return FALSE;

    if (g_backend == AUDIO_BACKEND_UNKNOWN)
        audio_backend_init();

    /* Rate-limit backend queries to once every 400ms to keep UI responsive */
    if (now - g_last_query_tick > 400)
    {
        g_last_query_tick = now;

        switch (g_backend)
        {
        case AUDIO_BACKEND_PIPEWIRE:
            if (run_cmd_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf)))
            {
                /* Typical output: "Volume: 0.65" or "Volume: 0.65 [MUTED]" */
                char *p = strstr(buf, "Volume:");
                if (p)
                {
                    float val = 0.0f;
                    if (sscanf(p + 7, "%f", &val) == 1)
                    {
                        g_cached_volume = (int)(val * 100.0f + 0.5f);
                        if (g_cached_volume < 0) g_cached_volume = 0;
                        if (g_cached_volume > 100) g_cached_volume = 100;
                    }
                }
                g_cached_mute = (strstr(buf, "[MUTED]") != NULL);
            }
            break;

        case AUDIO_BACKEND_PULSEAUDIO:
            if (run_cmd_capture("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
            {
                /* Look for first percentage "%" */
                char *pct = strchr(buf, '%');
                if (pct)
                {
                    char *p = pct - 1;
                    while (p > buf && (*p >= '0' && *p <= '9')) p--;
                    g_cached_volume = atoi(p + 1);
                }
            }
            if (run_cmd_capture("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
            {
                g_cached_mute = (strstr(buf, "yes") != NULL);
            }
            break;

        case AUDIO_BACKEND_ALSA:
            if (run_cmd_capture("amixer sget Master 2>/dev/null", buf, sizeof(buf)))
            {
                char *pct = strchr(buf, '%');
                if (pct)
                {
                    char *p = pct - 1;
                    while (p > buf && (*p >= '0' && *p <= '9')) p--;
                    g_cached_volume = atoi(p + 1);
                }
                g_cached_mute = (strstr(buf, "[off]") != NULL);
            }
            break;

        case AUDIO_BACKEND_WINMM:
        {
            DWORD vol = 0;
            if (waveOutGetVolume(0, &vol) == MMSYSERR_NOERROR)
            {
                WORD left = LOWORD(vol);
                g_cached_volume = (int)((left * 100) / 0xFFFF);
            }
            break;
        }

        case AUDIO_BACKEND_MOCK:
        default:
            /* Maintain simulated in-memory state */
            break;
        }
    }

    info->volume_percent = g_cached_volume;
    info->is_muted = g_cached_mute;
    info->backend_type = g_backend;
    lstrcpyW(info->device_name, g_cached_device);
    return TRUE;
}

BOOL audio_backend_set_volume(int percent)
{
    char cmd[256];

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    g_cached_volume = percent;
    g_last_query_tick = GetTickCount();

    switch (g_backend)
    {
    case AUDIO_BACKEND_PIPEWIRE:
        snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f", (float)percent / 100.0f);
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_PULSEAUDIO:
        snprintf(cmd, sizeof(cmd), "pactl set-sink-volume @DEFAULT_SINK@ %d%%", percent);
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_ALSA:
        snprintf(cmd, sizeof(cmd), "amixer sset Master %d%%", percent);
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_WINMM:
    {
        DWORD raw = (percent * 0xFFFF) / 100;
        waveOutSetVolume(0, MAKELONG(raw, raw));
        break;
    }

    case AUDIO_BACKEND_MOCK:
    default:
        break;
    }

    return TRUE;
}

BOOL audio_backend_set_mute(BOOL mute)
{
    char cmd[256];

    g_cached_mute = mute;
    g_last_query_tick = GetTickCount();

    switch (g_backend)
    {
    case AUDIO_BACKEND_PIPEWIRE:
        snprintf(cmd, sizeof(cmd), "wpctl set-mute @DEFAULT_AUDIO_SINK@ %d", mute ? 1 : 0);
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_PULSEAUDIO:
        snprintf(cmd, sizeof(cmd), "pactl set-sink-mute @DEFAULT_SINK@ %d", mute ? 1 : 0);
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_ALSA:
        snprintf(cmd, sizeof(cmd), "amixer sset Master %s", mute ? "mute" : "unmute");
        run_cmd_async(cmd);
        break;

    case AUDIO_BACKEND_WINMM:
        if (mute)
            waveOutSetVolume(0, 0);
        else
            audio_backend_set_volume(g_cached_volume);
        break;

    case AUDIO_BACKEND_MOCK:
    default:
        break;
    }

    return TRUE;
}

BOOL audio_backend_toggle_mute(void)
{
    return audio_backend_set_mute(!g_cached_mute);
}

int audio_backend_step_volume(int delta_percent)
{
    int new_vol = g_cached_volume + delta_percent;
    if (new_vol < 0) new_vol = 0;
    if (new_vol > 100) new_vol = 100;
    audio_backend_set_volume(new_vol);
    return new_vol;
}
