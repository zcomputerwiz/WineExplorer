/*
 * WineExplorer Desktop Environment - Audio Control Backend Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Provides abstraction for Linux audio servers (PipeWire / WirePlumber wpctl,
 * PulseAudio pactl, ALSA amixer) with Win32 waveOut / mmsystem fallback.
 */

#ifndef __WINE_EXPLORER_AUDIO_BACKEND_H
#define __WINE_EXPLORER_AUDIO_BACKEND_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_BACKEND_UNKNOWN = 0,
    AUDIO_BACKEND_PIPEWIRE,
    AUDIO_BACKEND_PULSEAUDIO,
    AUDIO_BACKEND_ALSA,
    AUDIO_BACKEND_WINMM,
    AUDIO_BACKEND_MOCK
} AUDIO_BACKEND_TYPE;

typedef struct
{
    int volume_percent;    /* 0 to 100 */
    BOOL is_muted;
    AUDIO_BACKEND_TYPE backend_type;
    WCHAR device_name[128];
} AUDIO_STATUS_INFO;

/* Initialize audio subsystem and detect native Linux / Win32 backend */
void audio_backend_init(void);

/* Get current volume level (0-100) and mute status */
BOOL audio_backend_get_status(AUDIO_STATUS_INFO *info);

/* Set master volume level (0-100) */
BOOL audio_backend_set_volume(int percent);

/* Set master mute state */
BOOL audio_backend_set_mute(BOOL mute);

/* Toggle mute state */
BOOL audio_backend_toggle_mute(void);

/* Step volume up or down by delta (e.g. +5 or -5) */
int audio_backend_step_volume(int delta_percent);

#ifdef __cplusplus
}
#endif

#endif /* __WINE_EXPLORER_AUDIO_BACKEND_H */
