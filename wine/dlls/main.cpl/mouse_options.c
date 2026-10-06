/*
 * WineExplorer Desktop Environment - Mouse Properties: Pointer Options Page
 */

#include "main_private.h"

typedef struct _MOTION_STATE
{
    UINT mouse_speed;       /* 1 to 20 */
    BOOL enhance_precision;
    BOOL snap_to_def;
    UINT mouse_trails;      /* 0 (disabled), 2 to 8 */
    BOOL mouse_vanish;
    BOOL mouse_sonar;
} MOTION_STATE;

static MOTION_STATE g_state;

static void read_motion_settings(MOTION_STATE *st)
{
    int accel[3] = { 6, 10, 1 };
    HKEY hkey;

    st->mouse_speed = 10;
    st->enhance_precision = TRUE;
    st->snap_to_def = FALSE;
    st->mouse_trails = 0;
    st->mouse_vanish = TRUE;
    st->mouse_sonar = FALSE;

    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &st->mouse_speed, 0);
    SystemParametersInfoW(SPI_GETMOUSE, 0, accel, 0);
    st->enhance_precision = (accel[2] != 0);

    SystemParametersInfoW(SPI_GETSNAPTODEFBUTTON, 0, &st->snap_to_def, 0);
    SystemParametersInfoW(SPI_GETMOUSETRAILS, 0, &st->mouse_trails, 0);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
    {
        WCHAR buf[32];
        DWORD len = sizeof(buf);
        if (RegQueryValueExW(hkey, L"MouseVanish", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS)
            st->mouse_vanish = (_wtoi(buf) != 0);

        len = sizeof(buf);
        if (RegQueryValueExW(hkey, L"MouseSonar", NULL, NULL, (LPBYTE)buf, &len) == ERROR_SUCCESS)
            st->mouse_sonar = (_wtoi(buf) != 0);

        RegCloseKey(hkey);
    }
}

static void save_motion_settings(const MOTION_STATE *st)
{
    HKEY hkey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Mouse", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hkey, NULL) == ERROR_SUCCESS)
    {
        WCHAR buf[32];
        swprintf(buf, 32, L"%d", st->mouse_vanish ? 1 : 0);
        RegSetValueExW(hkey, L"MouseVanish", 0, REG_SZ, (const BYTE *)buf, (lstrlenW(buf) + 1) * sizeof(WCHAR));

        swprintf(buf, 32, L"%d", st->mouse_sonar ? 1 : 0);
        RegSetValueExW(hkey, L"MouseSonar", 0, REG_SZ, (const BYTE *)buf, (lstrlenW(buf) + 1) * sizeof(WCHAR));

        RegCloseKey(hkey);
    }
}

INT_PTR CALLBACK motion_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hSpeed = GetDlgItem(hwnd, MOUSE_SPEEDSCROLL);
        HWND hTrails = GetDlgItem(hwnd, MOUSE_TRAILSCROLL);

        read_motion_settings(&g_state);

        /* Speed slider: 1 to 20 */
        SendMessageW(hSpeed, TBM_SETRANGE, TRUE, MAKELONG(1, 20));
        SendMessageW(hSpeed, TBM_SETPOS, TRUE, g_state.mouse_speed);

        CheckDlgButton(hwnd, MOUSE_ENHANCED_MOTION, g_state.enhance_precision ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, MOUSE_SNAPDEF, g_state.snap_to_def ? BST_CHECKED : BST_UNCHECKED);

        /* Trails slider: 2 (Short) to 8 (Long) */
        SendMessageW(hTrails, TBM_SETRANGE, TRUE, MAKELONG(2, 8));
        SendMessageW(hTrails, TBM_SETPOS, TRUE, g_state.mouse_trails >= 2 ? g_state.mouse_trails : 4);
        CheckDlgButton(hwnd, MOUSE_TRAILS, g_state.mouse_trails > 0 ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(hTrails, g_state.mouse_trails > 0);

        CheckDlgButton(hwnd, MOUSE_VANISH, g_state.mouse_vanish ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, MOUSE_SONAR, g_state.mouse_sonar ? BST_CHECKED : BST_UNCHECKED);

        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSpeed = GetDlgItem(hwnd, MOUSE_SPEEDSCROLL);
        HWND hTrails = GetDlgItem(hwnd, MOUSE_TRAILSCROLL);
        if ((HWND)lparam == hSpeed)
        {
            g_state.mouse_speed = (UINT)SendMessageW(hSpeed, TBM_GETPOS, 0, 0);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        else if ((HWND)lparam == hTrails)
        {
            g_state.mouse_trails = (UINT)SendMessageW(hTrails, TBM_GETPOS, 0, 0);
            PropSheet_Changed(GetParent(hwnd), hwnd);
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case MOUSE_ENHANCED_MOTION:
            g_state.enhance_precision = (IsDlgButtonChecked(hwnd, MOUSE_ENHANCED_MOTION) == BST_CHECKED);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;

        case MOUSE_SNAPDEF:
            g_state.snap_to_def = (IsDlgButtonChecked(hwnd, MOUSE_SNAPDEF) == BST_CHECKED);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;

        case MOUSE_TRAILS:
        {
            BOOL checked = (IsDlgButtonChecked(hwnd, MOUSE_TRAILS) == BST_CHECKED);
            EnableWindow(GetDlgItem(hwnd, MOUSE_TRAILSCROLL), checked);
            if (checked)
            {
                g_state.mouse_trails = (UINT)SendDlgItemMessageW(hwnd, MOUSE_TRAILSCROLL, TBM_GETPOS, 0, 0);
                if (g_state.mouse_trails < 2) g_state.mouse_trails = 4;
            }
            else
            {
                g_state.mouse_trails = 0;
            }
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;
        }

        case MOUSE_VANISH:
            g_state.mouse_vanish = (IsDlgButtonChecked(hwnd, MOUSE_VANISH) == BST_CHECKED);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;

        case MOUSE_SONAR:
            g_state.mouse_sonar = (IsDlgButtonChecked(hwnd, MOUSE_SONAR) == BST_CHECKED);
            PropSheet_Changed(GetParent(hwnd), hwnd);
            break;
        }
        break;

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            int accel[3];
            accel[0] = 6;
            accel[1] = 10;
            accel[2] = g_state.enhance_precision ? 1 : 0;

            SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (void *)(ULONG_PTR)g_state.mouse_speed,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETMOUSE, 0, accel, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETSNAPTODEFBUTTON, g_state.snap_to_def, 0,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            SystemParametersInfoW(SPI_SETMOUSETRAILS, g_state.mouse_trails, 0,
                                  SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
            save_motion_settings(&g_state);
            return TRUE;
        }
        break;
    }
    }

    return FALSE;
}
