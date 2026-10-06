/*
 * Wine Sounds and Audio Devices Control Panel Applet (mmsys.cpl)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * Implements the authentic Windows XP / Server 2003 Sounds and Audio Devices
 * Control Panel applet, providing Volume, Sounds, and Audio device property pages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <cpl.h>
#include <shellapi.h>
#include <mmsystem.h>

#include "resource.h"

static HINSTANCE g_hInstance = NULL;

#define STSERVICE_VOLUME 0x00000001

/* Helper to execute host command asynchronously */
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

/* Helper to execute host command and capture output */
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

static int get_audio_volume(BOOL *out_muted)
{
    char buf[256];
    int vol = 75;
    BOOL muted = FALSE;

    /* Check PipeWire */
    if (run_cmd_capture("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", buf, sizeof(buf)))
    {
        char *p = strstr(buf, "Volume:");
        if (p)
        {
            float val = 0.0f;
            if (sscanf(p + 7, "%f", &val) == 1)
                vol = (int)(val * 100.0f + 0.5f);
        }
        muted = (strstr(buf, "[MUTED]") != NULL);
    }
    else if (run_cmd_capture("pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
    {
        char *pct = strchr(buf, '%');
        if (pct)
        {
            char *p = pct - 1;
            while (p > buf && (*p >= '0' && *p <= '9')) p--;
            vol = atoi(p + 1);
        }
        if (run_cmd_capture("pactl get-sink-mute @DEFAULT_SINK@ 2>/dev/null", buf, sizeof(buf)))
            muted = (strstr(buf, "yes") != NULL);
    }
    else
    {
        DWORD dwVol = 0;
        if (waveOutGetVolume(0, &dwVol) == MMSYSERR_NOERROR)
            vol = (int)((LOWORD(dwVol) * 100) / 0xFFFF);
    }

    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    if (out_muted) *out_muted = muted;
    return vol;
}

static void set_audio_volume(int vol)
{
    char cmd[256];
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    snprintf(cmd, sizeof(cmd), "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f 2>/dev/null || "
                               "pactl set-sink-volume @DEFAULT_SINK@ %d%% 2>/dev/null || "
                               "amixer sset Master %d%% 2>/dev/null",
             (float)vol / 100.0f, vol, vol);
    run_cmd_async(cmd);

    {
        DWORD raw = (vol * 0xFFFF) / 100;
        waveOutSetVolume(0, MAKELONG(raw, raw));
    }
}

static void set_audio_mute(BOOL mute)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "wpctl set-mute @DEFAULT_AUDIO_SINK@ %d 2>/dev/null || "
                               "pactl set-sink-mute @DEFAULT_SINK@ %d 2>/dev/null || "
                               "amixer sset Master %s 2>/dev/null",
             mute ? 1 : 0, mute ? 1 : 0, mute ? "mute" : "unmute");
    run_cmd_async(cmd);

    if (mute)
        waveOutSetVolume(0, 0);
}

/* Page 1: Volume */
static INT_PTR CALLBACK VolumePageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        BOOL muted = FALSE;
        int vol = get_audio_volume(&muted);
        HWND hSlider = GetDlgItem(hwnd, IDC_VOL_SLIDER);
        HWND hSetup = GetDlgItem(hwnd, IDC_VOL_SPKR_SETUP);

        if (hSlider)
        {
            SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(hSlider, TBM_SETPAGESIZE, 0, 10);
            SendMessageW(hSlider, TBM_SETPOS, TRUE, vol);
        }

        CheckDlgButton(hwnd, IDC_VOL_MUTE, muted ? BST_CHECKED : BST_UNCHECKED);

        /* Read tray service flag */
        {
            HKEY hKey;
            DWORD services = STSERVICE_VOLUME;
            DWORD size = sizeof(services);
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\SysTray",
                              0, KEY_READ, &hKey) == ERROR_SUCCESS)
            {
                RegQueryValueExW(hKey, L"Services", NULL, NULL, (LPBYTE)&services, &size);
                RegCloseKey(hKey);
            }
            CheckDlgButton(hwnd, IDC_VOL_SYSTRAY, (services & STSERVICE_VOLUME) ? BST_CHECKED : BST_UNCHECKED);
        }

        /* Populate speaker setup list */
        if (hSetup)
        {
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"Desktop stereo speakers");
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"Laptop stereo speakers");
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"Stereo headphones");
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"Quadraphonic speakers");
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"5.1 surround sound speakers");
            SendMessageW(hSetup, CB_ADDSTRING, 0, (LPARAM)L"7.1 home theater speakers");
            SendMessageW(hSetup, CB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hwnd, IDC_VOL_SLIDER);
        if (hSlider && (HWND)lparam == hSlider)
        {
            int vol = (int)SendMessageW(hSlider, TBM_GETPOS, 0, 0);
            set_audio_volume(vol);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_VOL_MUTE:
            if (HIWORD(wparam) == BN_CLICKED)
            {
                BOOL mute = (IsDlgButtonChecked(hwnd, IDC_VOL_MUTE) == BST_CHECKED);
                set_audio_mute(mute);
            }
            return TRUE;

        case IDC_VOL_SYSTRAY:
            if (HIWORD(wparam) == BN_CLICKED)
            {
                BOOL show_tray = (IsDlgButtonChecked(hwnd, IDC_VOL_SYSTRAY) == BST_CHECKED);
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\SysTray",
                                   0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
                {
                    DWORD services = show_tray ? STSERVICE_VOLUME : 0;
                    RegSetValueExW(hKey, L"Services", 0, REG_DWORD, (const BYTE *)&services, sizeof(services));
                    RegCloseKey(hKey);
                }
            }
            return TRUE;

        case IDC_VOL_SPKR_VOL:
        case IDC_VOL_SPKR_ADV:
            ShellExecuteW(NULL, L"open", L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Page 2: Sounds */
static const struct
{
    const WCHAR *name;
    const WCHAR *sound_file;
} g_sound_events[] = {
    { L"Asterisk", L"Windows XP Error.wav" },
    { L"Close Program", L"" },
    { L"Critical Stop", L"Windows XP Critical Stop.wav" },
    { L"Default Beep", L"Windows XP Ding.wav" },
    { L"Device Connect", L"Windows XP Hardware Insert.wav" },
    { L"Device Disconnect", L"Windows XP Hardware Remove.wav" },
    { L"Exclamation", L"Windows XP Exclamation.wav" },
    { L"Exit Windows", L"Windows XP Shutdown.wav" },
    { L"Maximize", L"" },
    { L"Menu Command", L"Windows XP Menu Command.wav" },
    { L"Menu Popup", L"" },
    { L"Minimize", L"" },
    { L"New Mail Notification", L"Windows XP Notify.wav" },
    { L"Open Program", L"" },
    { L"Program Error", L"Windows XP Error.wav" },
    { L"Question", L"" },
    { L"Restore Down", L"" },
    { L"Restore Up", L"" },
    { L"Start Windows", L"Windows XP Startup.wav" },
    { L"Windows Logoff", L"Windows XP Logoff.wav" },
    { L"Windows Logon", L"Windows XP Logon.wav" },
};

static INT_PTR CALLBACK SoundsPageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hScheme = GetDlgItem(hwnd, IDC_SND_SCHEME);
        HWND hEvents = GetDlgItem(hwnd, IDC_SND_EVENTS);
        LVCOLUMNW lvc = {0};

        if (hScheme)
        {
            SendMessageW(hScheme, CB_ADDSTRING, 0, (LPARAM)L"Windows Default");
            SendMessageW(hScheme, CB_ADDSTRING, 0, (LPARAM)L"No Sounds");
            SendMessageW(hScheme, CB_SETCURSEL, 0, 0);
        }

        if (hEvents)
        {
            SendMessageW(hEvents, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);

            lvc.mask = LVCF_TEXT | LVCF_WIDTH;
            lvc.cx = 120;
            lvc.pszText = (LPWSTR)L"Event";
            SendMessageW(hEvents, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

            lvc.cx = 110;
            lvc.pszText = (LPWSTR)L"Sound";
            SendMessageW(hEvents, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

            for (size_t i = 0; i < sizeof(g_sound_events)/sizeof(g_sound_events[0]); i++)
            {
                LVITEMW item = {0};
                item.mask = LVIF_TEXT;
                item.iItem = (int)i;
                item.iSubItem = 0;
                item.pszText = (LPWSTR)g_sound_events[i].name;
                SendMessageW(hEvents, LVM_INSERTITEMW, 0, (LPARAM)&item);

                item.iSubItem = 1;
                item.pszText = (LPWSTR)g_sound_events[i].sound_file;
                SendMessageW(hEvents, LVM_SETITEMW, 0, (LPARAM)&item);
            }
            ListView_SetItemState(hEvents, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            SetDlgItemTextW(hwnd, IDC_SND_FILE, g_sound_events[0].sound_file);
        }
        return TRUE;
    }

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lparam;
        if (pnm && pnm->idFrom == IDC_SND_EVENTS && pnm->code == LVN_ITEMCHANGED)
        {
            LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lparam;
            if (pnmv->uNewState & LVIS_SELECTED)
            {
                if (pnmv->iItem >= 0 && (size_t)pnmv->iItem < sizeof(g_sound_events)/sizeof(g_sound_events[0]))
                {
                    SetDlgItemTextW(hwnd, IDC_SND_FILE, g_sound_events[pnmv->iItem].sound_file);
                }
            }
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDC_SND_PLAY:
        {
            WCHAR path[MAX_PATH];
            GetDlgItemTextW(hwnd, IDC_SND_FILE, path, MAX_PATH);
            if (path[0])
            {
                WCHAR fullpath[MAX_PATH];
                GetWindowsDirectoryW(fullpath, MAX_PATH);
                lstrcatW(fullpath, L"\\Media\\");
                lstrcatW(fullpath, path);
                PlaySoundW(fullpath, NULL, SND_FILENAME | SND_ASYNC);
            }
            return TRUE;
        }

        case IDC_SND_STOP:
            PlaySoundW(NULL, 0, 0);
            return TRUE;

        case IDC_SND_BROWSE:
        {
            OPENFILENAMEW ofn = {0};
            WCHAR file[MAX_PATH] = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"Wave Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn))
            {
                SetDlgItemTextW(hwnd, IDC_SND_FILE, file);
            }
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

/* Page 3: Audio */
static INT_PTR CALLBACK AudioPageProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hPlay = GetDlgItem(hwnd, IDC_AUD_PLAYBACK);
        HWND hRec = GetDlgItem(hwnd, IDC_AUD_RECORD);
        HWND hMidi = GetDlgItem(hwnd, IDC_AUD_MIDI);

        if (hPlay)
        {
            SendMessageW(hPlay, CB_ADDSTRING, 0, (LPARAM)L"Default Audio Device (PipeWire / PulseAudio)");
            SendMessageW(hPlay, CB_SETCURSEL, 0, 0);
        }
        if (hRec)
        {
            SendMessageW(hRec, CB_ADDSTRING, 0, (LPARAM)L"Default Audio Input (Microphone)");
            SendMessageW(hRec, CB_SETCURSEL, 0, 0);
        }
        if (hMidi)
        {
            SendMessageW(hMidi, CB_ADDSTRING, 0, (LPARAM)L"Microsoft GS Wavetable SW Synth");
            SendMessageW(hMidi, CB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_AUD_VOL_PLAY || LOWORD(wparam) == IDC_AUD_VOL_REC)
        {
            ShellExecuteW(NULL, L"open", L"sndvol32.exe", NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void ShowAudioPropertySheet(HWND hwndParent)
{
    PROPSHEETPAGEW psp[3] = {0};
    PROPSHEETHEADERW psh = {0};

    psp[0].dwSize = sizeof(PROPSHEETPAGEW);
    psp[0].hInstance = g_hInstance;
    psp[0].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_VOLUME);
    psp[0].pfnDlgProc = VolumePageProc;

    psp[1].dwSize = sizeof(PROPSHEETPAGEW);
    psp[1].hInstance = g_hInstance;
    psp[1].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_SOUNDS);
    psp[1].pfnDlgProc = SoundsPageProc;

    psp[2].dwSize = sizeof(PROPSHEETPAGEW);
    psp[2].hInstance = g_hInstance;
    psp[2].pszTemplate = MAKEINTRESOURCEW(IDD_PAGE_AUDIO);
    psp[2].pfnDlgProc = AudioPageProc;

    psh.dwSize = sizeof(PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID;
    psh.hwndParent = hwndParent;
    psh.hInstance = g_hInstance;
    psh.pszIcon = MAKEINTRESOURCEW(IDI_CPL_MMSYS);
    psh.pszCaption = L"Sounds and Audio Devices Properties";
    psh.nPages = 3;
    psh.ppsp = psp;

    PropertySheetW(&psh);
}

LONG WINAPI CPlApplet(HWND hwnd, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
    switch (uMsg)
    {
    case CPL_INIT:
        return TRUE;

    case CPL_GETCOUNT:
        return 1;

    case CPL_INQUIRE:
    {
        CPLINFO *info = (CPLINFO *)lParam2;
        if (info)
        {
            info->idIcon = IDI_CPL_MMSYS;
            info->idName = IDS_CPL_NAME;
            info->idInfo = IDS_CPL_INFO;
            info->lData = 0;
        }
        return 0;
    }

    case CPL_NEWINQUIRE:
    {
        NEWCPLINFOW *info = (NEWCPLINFOW *)lParam2;
        if (info)
        {
            info->dwSize = sizeof(NEWCPLINFOW);
            info->dwFlags = 0;
            info->dwHelpContext = 0;
            info->lData = 0;
            info->hIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(IDI_CPL_MMSYS));
            LoadStringW(g_hInstance, IDS_CPL_NAME, info->szName, ARRAYSIZE(info->szName));
            LoadStringW(g_hInstance, IDS_CPL_INFO, info->szInfo, ARRAYSIZE(info->szInfo));
            info->szHelpFile[0] = '\0';
        }
        return 0;
    }

    case CPL_DBLCLK:
    case CPL_STARTWPARMSW:
        ShowAudioPropertySheet(hwnd);
        return 0;

    case CPL_STOP:
    case CPL_EXIT:
        return 0;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
