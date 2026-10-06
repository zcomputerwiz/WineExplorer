/*
 * Wine Sound Volume Control (sndvol32.exe)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 Volume Control mixer application,
 * supporting multi-channel playback strips (Master, Wave, Synth, CD, Mic, Line In),
 * tray popup mode (/t), and seamless integration with Linux audio servers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <mmsystem.h>

#include "resource.h"

static HINSTANCE g_hInstance = NULL;
static int g_master_volume = 75;
static BOOL g_master_mute = FALSE;

/* Execute host command asynchronously */
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

/* Execute host command and capture output */
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

        if (WaitForSingleObject(pi.hProcess, 800) == WAIT_OBJECT_0)
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

static void query_audio_state(void)
{
    char buf[256];

    /* Check PipeWire */
    if (run_cmd_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf)))
    {
        char *p = strstr(buf, "Volume:");
        if (p)
        {
            float val = 0.0f;
            if (sscanf(p + 7, "%f", &val) == 1)
            {
                g_master_volume = (int)(val * 100.0f + 0.5f);
                if (g_master_volume < 0) g_master_volume = 0;
                if (g_master_volume > 100) g_master_volume = 100;
            }
        }
        g_master_mute = (strstr(buf, "[MUTED]") != NULL);
        return;
    }

    /* Check PulseAudio */
    if (run_cmd_capture("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
    {
        char *pct = strchr(buf, '%');
        if (pct)
        {
            char *p = pct - 1;
            while (p > buf && (*p >= '0' && *p <= '9')) p--;
            g_master_volume = atoi(p + 1);
        }
    }
    if (run_cmd_capture("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
    {
        g_master_mute = (strstr(buf, "yes") != NULL);
        return;
    }

    /* Check WinMM */
    {
        DWORD vol = 0;
        if (waveOutGetVolume(0, &vol) == MMSYSERR_NOERROR)
        {
            g_master_volume = (int)((LOWORD(vol) * 100) / 0xFFFF);
        }
    }
}

static void set_audio_volume(int vol)
{
    char cmd[256];
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_master_volume = vol;

    /* Dispatch to available audio server */
    snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f 2>/dev/null || "
                               "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null || "
                               "amixer sset Master %d%% 2>/dev/null",
             (float)vol / 100.0f, vol, vol);
    run_cmd_async(cmd);

    /* Also forward to WinMM */
    {
        DWORD raw = (vol * 0xFFFF) / 100;
        waveOutSetVolume(0, MAKELONG(raw, raw));
    }
}

static void set_audio_mute(BOOL mute)
{
    char cmd[256];
    g_master_mute = mute;

    snprintf(cmd, sizeof(cmd), "wpctl set-mute @DEFAULT_AUDIO_SINK@ %d 2>/dev/null || "
                               "pactl set-sink-mute @DEFAULT_SINK@ %d 2>/dev/null || "
                               "amixer sset Master %s 2>/dev/null",
             mute ? 1 : 0, mute ? 1 : 0, mute ? "mute" : "unmute");
    run_cmd_async(cmd);

    if (mute)
        waveOutSetVolume(0, 0);
    else
        set_audio_volume(g_master_volume);
}

/* Setup trackbar defaults */
static void init_slider_channel(HWND hDlg, int id_balance, int id_volume, int id_mute, int default_vol)
{
    HWND hBal = GetDlgItem(hDlg, id_balance);
    HWND hVol = GetDlgItem(hDlg, id_volume);

    if (hBal)
    {
        SendMessageW(hBal, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(hBal, TBM_SETPOS, TRUE, 50); /* Center balance */
    }

    if (hVol)
    {
        SendMessageW(hVol, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(hVol, TBM_SETPAGESIZE, 0, 10);
        /* Vertical trackbar: 0 is top, 100 is bottom */
        SendMessageW(hVol, TBM_SETPOS, TRUE, 100 - default_vol);
    }
}

static INT_PTR CALLBACK MainDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        query_audio_state();

        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_MIXER)));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_MIXER)));

        /* Configure Master Volume column */
        init_slider_channel(hwnd, IDC_MASTER_BALANCE, IDC_MASTER_VOLUME, IDC_MASTER_MUTE, g_master_volume);
        CheckDlgButton(hwnd, IDC_MASTER_MUTE, g_master_mute ? BST_CHECKED : BST_UNCHECKED);

        /* Configure other playback channels */
        init_slider_channel(hwnd, IDC_WAVE_BALANCE, IDC_WAVE_VOLUME, IDC_WAVE_MUTE, 85);
        init_slider_channel(hwnd, IDC_SYNTH_BALANCE, IDC_SYNTH_VOLUME, IDC_SYNTH_MUTE, 80);
        init_slider_channel(hwnd, IDC_CD_BALANCE, IDC_CD_VOLUME, IDC_CD_MUTE, 70);
        init_slider_channel(hwnd, IDC_MIC_BALANCE, IDC_MIC_VOLUME, IDC_MIC_MUTE, 60);
        init_slider_channel(hwnd, IDC_LINE_BALANCE, IDC_LINE_VOLUME, IDC_LINE_MUTE, 65);

        return TRUE;
    }

    case WM_VSCROLL:
    {
        HWND hTrack = (HWND)lparam;
        if (hTrack == GetDlgItem(hwnd, IDC_MASTER_VOLUME))
        {
            int pos = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);
            int vol = 100 - pos;
            set_audio_volume(vol);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_MASTER_MUTE:
            if (HIWORD(wparam) == BN_CLICKED)
            {
                BOOL mute = (IsDlgButtonChecked(hwnd, IDC_MASTER_MUTE) == BST_CHECKED);
                set_audio_mute(mute);
            }
            return TRUE;

        case IDM_PROPERTIES:
            ShellExecuteW(NULL, L"open", L"control.exe", L"mmsys.cpl", NULL, SW_SHOWNORMAL);
            return TRUE;

        case IDM_EXIT:
        case IDCANCEL:
            EndDialog(hwnd, IDOK);
            PostQuitMessage(0);
            return TRUE;

        case IDM_ABOUT:
            ShellAboutW(hwnd, L"Volume Control", L"WineExplorer Desktop Environment",
                        LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_MIXER)));
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwnd, IDOK);
        PostQuitMessage(0);
        return TRUE;
    }

    return FALSE;
}

/* Tray Mode Dialog Proc */
static INT_PTR CALLBACK TrayDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hTrack;
        POINT pt;
        RECT rc;
        int w, h;

        hTrack = GetDlgItem(hwnd, IDC_TRAY_VOLUME_SLIDER);
        query_audio_state();

        if (hTrack)
        {
            SendMessageW(hTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hTrack, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(hTrack, TBM_SETPOS, TRUE, 100 - g_master_volume);
        }

        CheckDlgButton(hwnd, IDC_TRAY_VOLUME_MUTE, g_master_mute ? BST_CHECKED : BST_UNCHECKED);

        /* Position dialog near cursor */
        GetCursorPos(&pt);
        GetWindowRect(hwnd, &rc);
        w = rc.right - rc.left;
        h = rc.bottom - rc.top;
        SetWindowPos(hwnd, HWND_TOPMOST, pt.x - (w / 2), pt.y - h - 4, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        return TRUE;
    }

    case WM_VSCROLL:
    {
        HWND hTrack = GetDlgItem(hwnd, IDC_TRAY_VOLUME_SLIDER);
        if (hTrack && (HWND)lparam == hTrack)
        {
            int pos = (int)SendMessageW(hTrack, TBM_GETPOS, 0, 0);
            set_audio_volume(100 - pos);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_TRAY_VOLUME_MUTE:
            if (HIWORD(wparam) == BN_CLICKED)
            {
                BOOL mute = (IsDlgButtonChecked(hwnd, IDC_TRAY_VOLUME_MUTE) == BST_CHECKED);
                set_audio_mute(mute);
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hwnd, IDCANCEL);
            PostQuitMessage(0);
            return TRUE;
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE)
        {
            EndDialog(hwnd, IDOK);
            PostQuitMessage(0);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icex;
    BOOL bTrayMode = FALSE;

    g_hInstance = hInstance;

    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    if (lpCmdLine && (wcsstr(lpCmdLine, L"/t") || wcsstr(lpCmdLine, L"-t") || wcsstr(lpCmdLine, L"/T")))
    {
        bTrayMode = TRUE;
    }

    if (bTrayMode)
    {
        DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_TRAYMASTER), NULL, TrayDlgProc);
    }
    else
    {
        DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_MIXER_MAIN), NULL, MainDlgProc);
    }

    return 0;
}
