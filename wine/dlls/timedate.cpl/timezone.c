/*
 * WineExplorer Desktop Environment - Time Zone Property Page (timedate.cpl)
 */

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "timedate.h"

typedef struct
{
    LONG Bias;
    LONG StandardBias;
    LONG DaylightBias;
    SYSTEMTIME StandardDate;
    SYSTEMTIME DaylightDate;
} REG_TZI_FORMAT;

typedef struct _TZ_ENTRY
{
    WCHAR key_name[64];
    WCHAR display_name[128];
    WCHAR std_name[33];
    WCHAR dlt_name[33];
    REG_TZI_FORMAT tzi;
} TZ_ENTRY;

static TZ_ENTRY *g_tz_entries = NULL;
static int g_tz_count = 0;
static int g_selected_tz_idx = -1;
static WNDPROC g_orig_map_proc = NULL;
static HBITMAP g_hWorldBmp = NULL;
static int g_bm_width = 0;
static int g_bm_height = 0;

static const struct {
    const WCHAR *key;
    const WCHAR *display;
    const WCHAR *std;
    const WCHAR *dlt;
    LONG bias;
    BOOL has_dst;
} g_fallback_tzs[] = {
    { L"Dateline Standard Time", L"(GMT-12:00) International Date Line West", L"Dateline Standard Time", L"Dateline Daylight Time", 720, FALSE },
    { L"Samoa Standard Time", L"(GMT-11:00) Midway Island, Samoa", L"Samoa Standard Time", L"Samoa Daylight Time", 660, FALSE },
    { L"Hawaiian Standard Time", L"(GMT-10:00) Hawaii", L"Hawaiian Standard Time", L"Hawaiian Daylight Time", 600, FALSE },
    { L"Alaskan Standard Time", L"(GMT-09:00) Alaska", L"Alaskan Standard Time", L"Alaskan Daylight Time", 540, TRUE },
    { L"Pacific Standard Time", L"(GMT-08:00) Pacific Time (US & Canada); Tijuana", L"Pacific Standard Time", L"Pacific Daylight Time", 480, TRUE },
    { L"Mountain Standard Time", L"(GMT-07:00) Mountain Time (US & Canada)", L"Mountain Standard Time", L"Mountain Daylight Time", 420, TRUE },
    { L"US Mountain Standard Time", L"(GMT-07:00) Arizona", L"US Mountain Standard Time", L"US Mountain Daylight Time", 420, FALSE },
    { L"Central Standard Time", L"(GMT-06:00) Central Time (US & Canada)", L"Central Standard Time", L"Central Daylight Time", 360, TRUE },
    { L"Canada Central Standard Time", L"(GMT-06:00) Saskatchewan", L"Canada Central Standard Time", L"Canada Central Daylight Time", 360, FALSE },
    { L"Eastern Standard Time", L"(GMT-05:00) Eastern Time (US & Canada)", L"Eastern Standard Time", L"Eastern Daylight Time", 300, TRUE },
    { L"US Eastern Standard Time", L"(GMT-05:00) Indiana (East)", L"US Eastern Standard Time", L"US Eastern Daylight Time", 300, FALSE },
    { L"Atlantic Standard Time", L"(GMT-04:00) Atlantic Time (Canada)", L"Atlantic Standard Time", L"Atlantic Daylight Time", 240, TRUE },
    { L"SA Western Standard Time", L"(GMT-04:00) Caracas, La Paz", L"SA Western Standard Time", L"SA Western Daylight Time", 240, FALSE },
    { L"Newfoundland Standard Time", L"(GMT-03:30) Newfoundland", L"Newfoundland Standard Time", L"Newfoundland Daylight Time", 210, TRUE },
    { L"E. South America Standard Time", L"(GMT-03:00) Brasilia", L"E. South America Standard Time", L"E. South America Daylight Time", 180, TRUE },
    { L"Greenland Standard Time", L"(GMT-03:00) Greenland", L"Greenland Standard Time", L"Greenland Daylight Time", 180, TRUE },
    { L"Mid-Atlantic Standard Time", L"(GMT-02:00) Mid-Atlantic", L"Mid-Atlantic Standard Time", L"Mid-Atlantic Daylight Time", 120, TRUE },
    { L"Azores Standard Time", L"(GMT-01:00) Azores", L"Azores Standard Time", L"Azores Daylight Time", 60, TRUE },
    { L"Cape Verde Standard Time", L"(GMT-01:00) Cape Verde Is.", L"Cape Verde Standard Time", L"Cape Verde Daylight Time", 60, FALSE },
    { L"GMT Standard Time", L"(GMT) Greenwich Mean Time : Dublin, Edinburgh, Lisbon, London", L"GMT Standard Time", L"GMT Daylight Time", 0, TRUE },
    { L"Greenwich Standard Time", L"(GMT) Monrovia, Reykjavik", L"Greenwich Standard Time", L"Greenwich Daylight Time", 0, FALSE },
    { L"W. Europe Standard Time", L"(GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna", L"W. Europe Standard Time", L"W. Europe Daylight Time", -60, TRUE },
    { L"Romance Standard Time", L"(GMT+01:00) Brussels, Copenhagen, Madrid, Paris", L"Romance Standard Time", L"Romance Daylight Time", -60, TRUE },
    { L"GTB Standard Time", L"(GMT+02:00) Athens, Bucharest, Istanbul", L"GTB Standard Time", L"GTB Daylight Time", -120, TRUE },
    { L"Egypt Standard Time", L"(GMT+02:00) Cairo", L"Egypt Standard Time", L"Egypt Daylight Time", -120, TRUE },
    { L"Israel Standard Time", L"(GMT+02:00) Jerusalem", L"Israel Standard Time", L"Israel Daylight Time", -120, TRUE },
    { L"Russian Standard Time", L"(GMT+03:00) Moscow, St. Petersburg, Volgograd", L"Russian Standard Time", L"Russian Daylight Time", -180, TRUE },
    { L"Arab Standard Time", L"(GMT+03:00) Kuwait, Riyadh", L"Arab Standard Time", L"Arab Daylight Time", -180, FALSE },
    { L"Iran Standard Time", L"(GMT+03:30) Tehran", L"Iran Standard Time", L"Iran Daylight Time", -210, TRUE },
    { L"Arabian Standard Time", L"(GMT+04:00) Abu Dhabi, Muscat", L"Arabian Standard Time", L"Arabian Daylight Time", -240, FALSE },
    { L"Caucasus Standard Time", L"(GMT+04:00) Baku, Tbilisi, Yerevan", L"Caucasus Standard Time", L"Caucasus Daylight Time", -240, TRUE },
    { L"Afghanistan Standard Time", L"(GMT+04:30) Kabul", L"Afghanistan Standard Time", L"Afghanistan Daylight Time", -270, FALSE },
    { L"Ekaterinburg Standard Time", L"(GMT+05:00) Ekaterinburg", L"Ekaterinburg Standard Time", L"Ekaterinburg Daylight Time", -300, TRUE },
    { L"West Asia Standard Time", L"(GMT+05:00) Islamabad, Karachi, Tashkent", L"West Asia Standard Time", L"West Asia Daylight Time", -300, FALSE },
    { L"India Standard Time", L"(GMT+05:30) Chennai, Kolkata, Mumbai, New Delhi", L"India Standard Time", L"India Daylight Time", -330, FALSE },
    { L"Nepal Standard Time", L"(GMT+05:45) Kathmandu", L"Nepal Standard Time", L"Nepal Daylight Time", -345, FALSE },
    { L"Central Asia Standard Time", L"(GMT+06:00) Astana, Almaty, Dhaka", L"Central Asia Standard Time", L"Central Daylight Time", -360, FALSE },
    { L"Myanmar Standard Time", L"(GMT+06:30) Yangon (Rangoon)", L"Myanmar Standard Time", L"Myanmar Daylight Time", -390, FALSE },
    { L"SE Asia Standard Time", L"(GMT+07:00) Bangkok, Hanoi, Jakarta", L"SE Asia Standard Time", L"SE Asia Daylight Time", -420, FALSE },
    { L"China Standard Time", L"(GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi", L"China Standard Time", L"China Daylight Time", -480, FALSE },
    { L"Singapore Standard Time", L"(GMT+08:00) Kuala Lumpur, Singapore", L"Singapore Standard Time", L"Singapore Daylight Time", -480, FALSE },
    { L"Taipei Standard Time", L"(GMT+08:00) Taipei", L"Taipei Standard Time", L"Taipei Daylight Time", -480, FALSE },
    { L"Tokyo Standard Time", L"(GMT+09:00) Osaka, Sapporo, Tokyo", L"Tokyo Standard Time", L"Tokyo Daylight Time", -540, FALSE },
    { L"Korea Standard Time", L"(GMT+09:00) Seoul", L"Korea Standard Time", L"Korea Daylight Time", -540, FALSE },
    { L"Yakutsk Standard Time", L"(GMT+09:00) Yakutsk", L"Yakutsk Standard Time", L"Yakutsk Daylight Time", -540, TRUE },
    { L"Cen. Australia Standard Time", L"(GMT+09:30) Adelaide", L"Cen. Australia Standard Time", L"Cen. Australia Daylight Time", -570, TRUE },
    { L"AUS Central Standard Time", L"(GMT+09:30) Darwin", L"AUS Central Standard Time", L"AUS Central Daylight Time", -570, FALSE },
    { L"E. Australia Standard Time", L"(GMT+10:00) Brisbane", L"E. Australia Standard Time", L"E. Australia Daylight Time", -600, FALSE },
    { L"AUS Eastern Standard Time", L"(GMT+10:00) Canberra, Melbourne, Sydney", L"AUS Eastern Standard Time", L"AUS Eastern Daylight Time", -600, TRUE },
    { L"Vladivostok Standard Time", L"(GMT+10:00) Vladivostok", L"Vladivostok Standard Time", L"Vladivostok Daylight Time", -600, TRUE },
    { L"Central Pacific Standard Time", L"(GMT+11:00) Magadan, Solomon Is., New Caledonia", L"Central Pacific Standard Time", L"Central Pacific Daylight Time", -660, FALSE },
    { L"New Zealand Standard Time", L"(GMT+12:00) Auckland, Wellington", L"New Zealand Standard Time", L"New Zealand Daylight Time", -720, TRUE },
    { L"Fiji Standard Time", L"(GMT+12:00) Fiji, Kamchatka, Marshall Is.", L"Fiji Standard Time", L"Fiji Daylight Time", -720, FALSE },
    { L"Tonga Standard Time", L"(GMT+13:00) Nuku'alofa", L"Tonga Standard Time", L"Tonga Daylight Time", -780, FALSE }
};

/* Sort by Bias descending (so UTC-12 is first, up to UTC+13), then by display_name */
static int tz_compare(const void *a, const void *b)
{
    const TZ_ENTRY *ea = (const TZ_ENTRY *)a;
    const TZ_ENTRY *eb = (const TZ_ENTRY *)b;

    if (ea->tzi.Bias != eb->tzi.Bias)
        return (ea->tzi.Bias > eb->tzi.Bias) ? -1 : 1;

    return _wcsicmp(ea->display_name, eb->display_name);
}

static void init_timezones(void)
{
    HKEY hZonesKey;
    DWORD index = 0;
    DWORD subkey_count = 0;
    WCHAR subkey_name[64];
    DWORD subkey_len;
    int i;

    if (g_tz_entries) return;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones",
                      0, KEY_READ, &hZonesKey) == ERROR_SUCCESS)
    {
        RegQueryInfoKeyW(hZonesKey, NULL, NULL, NULL, &subkey_count, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        if (subkey_count > 0)
        {
            g_tz_entries = malloc(subkey_count * sizeof(TZ_ENTRY));
            g_tz_count = 0;

            for (index = 0; ; index++)
            {
                HKEY hSub;
                DWORD type, size;
                subkey_len = ARRAY_SIZE(subkey_name);
                if (RegEnumKeyExW(hZonesKey, index, subkey_name, &subkey_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                    break;

                if (RegOpenKeyExW(hZonesKey, subkey_name, 0, KEY_READ, &hSub) == ERROR_SUCCESS)
                {
                    TZ_ENTRY *item = &g_tz_entries[g_tz_count];
                    wcsncpy(item->key_name, subkey_name, ARRAY_SIZE(item->key_name) - 1);

                    size = sizeof(item->display_name);
                    if (RegQueryValueExW(hSub, L"Display", NULL, &type, (BYTE*)item->display_name, &size) != ERROR_SUCCESS)
                        wcsncpy(item->display_name, subkey_name, ARRAY_SIZE(item->display_name) - 1);

                    size = sizeof(item->std_name);
                    if (RegQueryValueExW(hSub, L"Std", NULL, &type, (BYTE*)item->std_name, &size) != ERROR_SUCCESS)
                        item->std_name[0] = 0;

                    size = sizeof(item->dlt_name);
                    if (RegQueryValueExW(hSub, L"Dlt", NULL, &type, (BYTE*)item->dlt_name, &size) != ERROR_SUCCESS)
                        item->dlt_name[0] = 0;

                    size = sizeof(item->tzi);
                    if (RegQueryValueExW(hSub, L"TZI", NULL, &type, (BYTE*)&item->tzi, &size) == ERROR_SUCCESS)
                    {
                        g_tz_count++;
                    }
                    RegCloseKey(hSub);
                }
            }
        }
        RegCloseKey(hZonesKey);
    }

    if (g_tz_count == 0)
    {
        /* Fallback table */
        int count = ARRAY_SIZE(g_fallback_tzs);
        g_tz_entries = malloc(count * sizeof(TZ_ENTRY));
        g_tz_count = count;

        for (i = 0; i < count; i++)
        {
            wcsncpy(g_tz_entries[i].key_name, g_fallback_tzs[i].key, ARRAY_SIZE(g_tz_entries[i].key_name) - 1);
            wcsncpy(g_tz_entries[i].display_name, g_fallback_tzs[i].display, ARRAY_SIZE(g_tz_entries[i].display_name) - 1);
            wcsncpy(g_tz_entries[i].std_name, g_fallback_tzs[i].std, ARRAY_SIZE(g_tz_entries[i].std_name) - 1);
            wcsncpy(g_tz_entries[i].dlt_name, g_fallback_tzs[i].dlt, ARRAY_SIZE(g_tz_entries[i].dlt_name) - 1);
            memset(&g_tz_entries[i].tzi, 0, sizeof(g_tz_entries[i].tzi));
            g_tz_entries[i].tzi.Bias = g_fallback_tzs[i].bias;
            if (g_fallback_tzs[i].has_dst)
            {
                g_tz_entries[i].tzi.DaylightBias = -60;
                g_tz_entries[i].tzi.StandardDate.wMonth = 10;
                g_tz_entries[i].tzi.StandardDate.wDay = 5;
                g_tz_entries[i].tzi.DaylightDate.wMonth = 3;
                g_tz_entries[i].tzi.DaylightDate.wDay = 5;
            }
        }
    }

    /* Sort time zones by UTC offset (Bias descending) */
    if (g_tz_count > 1)
    {
        qsort(g_tz_entries, g_tz_count, sizeof(TZ_ENTRY), tz_compare);
    }
}

static void draw_world_map(HDC hdc, const RECT *rc, int selected_idx)
{
    int width = rc->right - rc->left;
    int height = rc->bottom - rc->top;
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bm = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP old_mem_bm = SelectObject(mem_dc, mem_bm);

    if (g_hWorldBmp && g_bm_width > 0 && g_bm_height > 0)
    {
        HDC bmp_dc = CreateCompatibleDC(hdc);
        HBITMAP old_bmp = SelectObject(bmp_dc, g_hWorldBmp);
        StretchBlt(mem_dc, 0, 0, width, height, bmp_dc, 0, 0, g_bm_width, g_bm_height, SRCCOPY);
        SelectObject(bmp_dc, old_bmp);
        DeleteDC(bmp_dc);
    }
    else
    {
        HBRUSH bg_br = CreateSolidBrush(RGB(180, 205, 230));
        FillRect(mem_dc, rc, bg_br);
        DeleteObject(bg_br);
    }

    /* Highlight band for selected time zone */
    if (selected_idx >= 0 && selected_idx < g_tz_count)
    {
        LONG bias = g_tz_entries[selected_idx].tzi.Bias;
        /* Longitude in degrees: -Bias / 4.0 */
        double lon = -bias / 4.0;
        /* Map lon [-180, 180] to [0, width] */
        int band_center = (int)(((lon + 180.0) / 360.0) * width);
        HBRUSH highlight_brush;
        HPEN highlight_pen, old_pen;

        /* Semi-translucent yellow stripe: Invert / blend or draw tinted frame */
        highlight_brush = CreateSolidBrush(RGB(255, 230, 80));
        highlight_pen = CreatePen(PS_SOLID, 2, RGB(220, 160, 20));
        old_pen = SelectObject(mem_dc, highlight_pen);

        /* Draw vertical highlighted meridional meridian strip */
        MoveToEx(mem_dc, band_center, 0, NULL);
        LineTo(mem_dc, band_center, height);

        SelectObject(mem_dc, old_pen);
        DeleteObject(highlight_brush);
        DeleteObject(highlight_pen);

        /* UTC indicator text badge in lower-left */
        {
            WCHAR tz_text[32];
            int hours = -bias / 60;
            int mins = abs(bias % 60);
            HFONT font = GetStockObject(DEFAULT_GUI_FONT);
            HFONT old_font = SelectObject(mem_dc, font);
            RECT badge_rc = { 4, height - 20, 120, height - 3 };
            RECT txt_rc = { 8, height - 19, 116, height - 4 };
            HBRUSH badge_br = CreateSolidBrush(RGB(255, 255, 255));
            HPEN badge_pen = CreatePen(PS_SOLID, 1, RGB(120, 140, 170));

            old_pen = SelectObject(mem_dc, badge_pen);
            RoundRect(mem_dc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, 4, 4);

            if (bias == 0)
                swprintf(tz_text, 32, L"UTC / GMT");
            else if (mins == 0)
                swprintf(tz_text, 32, L"UTC %+d:00", hours);
            else
                swprintf(tz_text, 32, L"UTC %+d:%02d", hours, mins);

            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, RGB(20, 40, 80));
            DrawTextW(mem_dc, tz_text, -1, &txt_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(mem_dc, old_pen);
            SelectObject(mem_dc, old_font);
            DeleteObject(badge_pen);
            DeleteObject(badge_br);
        }
    }

    BitBlt(hdc, rc->left, rc->top, width, height, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_mem_bm);
    DeleteObject(mem_bm);
    DeleteDC(mem_dc);
}

static LRESULT CALLBACK map_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        draw_world_map(hdc, &rc, g_selected_tz_idx);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProcW(g_orig_map_proc, hwnd, msg, wparam, lparam);
}

/* Retrieve the system's configured TimeZoneKeyName from registry */
static BOOL get_active_timezone_key(WCHAR *key_buf, DWORD max_len)
{
    HKEY hKey;
    DWORD type, size;
    BOOL ret = FALSE;

    /* First check user's persisted setting in HKCU\Software\WineExplorer\Time */
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Time",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        size = max_len * sizeof(WCHAR);
        if (RegQueryValueExW(hKey, L"SelectedTimeZone", NULL, &type, (BYTE *)key_buf, &size) == ERROR_SUCCESS && key_buf[0] != 0)
        {
            ret = TRUE;
        }
        RegCloseKey(hKey);
        if (ret) return TRUE;
    }

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\TimeZoneInformation",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        size = max_len * sizeof(WCHAR);
        if (RegQueryValueExW(hKey, L"TimeZoneKeyName", NULL, &type, (BYTE *)key_buf, &size) == ERROR_SUCCESS && key_buf[0] != 0)
        {
            ret = TRUE;
        }
        else
        {
            size = max_len * sizeof(WCHAR);
            if (RegQueryValueExW(hKey, L"StandardName", NULL, &type, (BYTE *)key_buf, &size) == ERROR_SUCCESS && key_buf[0] != 0)
            {
                ret = TRUE;
            }
        }
        RegCloseKey(hKey);
    }
    return ret;
}

INT_PTR CALLBACK timezone_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        HWND hCombo = GetDlgItem(hwnd, IDC_TIMEZONE_LIST);
        HWND hMap = GetDlgItem(hwnd, IDC_TIMEZONE_MAP);
        TIME_ZONE_INFORMATION cur_tzi;
        WCHAR active_key[64];
        BOOL has_key;
        int i, sel = -1;
        BITMAP bm;

        init_timezones();

        if (!g_hWorldBmp)
        {
            g_hWorldBmp = LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDB_WORLD), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
            if (g_hWorldBmp)
            {
                GetObjectW(g_hWorldBmp, sizeof(bm), &bm);
                g_bm_width = bm.bmWidth;
                g_bm_height = bm.bmHeight;
            }
        }

        /* Subclass map control */
        if (hMap && !g_orig_map_proc)
        {
            g_orig_map_proc = (WNDPROC)SetWindowLongPtrW(hMap, GWLP_WNDPROC, (LONG_PTR)map_subclass_proc);
        }

        /* Populate combobox */
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < g_tz_count; i++)
        {
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)g_tz_entries[i].display_name);
        }

        /* Match current timezone:
         * 1. Try matching TimeZoneKeyName from HKLM\System\CurrentControlSet\Control\TimeZoneInformation
         * 2. Try matching StandardName / DaylightName from GetTimeZoneInformation
         * 3. Fallback to matching Bias
         */
        has_key = get_active_timezone_key(active_key, ARRAY_SIZE(active_key));
        GetTimeZoneInformation(&cur_tzi);

        if (has_key)
        {
            for (i = 0; i < g_tz_count; i++)
            {
                if (_wcsicmp(g_tz_entries[i].key_name, active_key) == 0 ||
                    _wcsicmp(g_tz_entries[i].std_name, active_key) == 0)
                {
                    sel = i;
                    break;
                }
            }
        }

        if (sel == -1 && cur_tzi.StandardName[0] != 0)
        {
            for (i = 0; i < g_tz_count; i++)
            {
                if (wcscmp(g_tz_entries[i].std_name, cur_tzi.StandardName) == 0 ||
                    wcscmp(g_tz_entries[i].dlt_name, cur_tzi.StandardName) == 0)
                {
                    sel = i;
                    break;
                }
            }
        }

        if (sel == -1)
        {
            for (i = 0; i < g_tz_count; i++)
            {
                if (g_tz_entries[i].tzi.Bias == cur_tzi.Bias)
                {
                    sel = i;
                    break;
                }
            }
        }

        if (sel < 0 || sel >= g_tz_count) sel = 0;

        g_selected_tz_idx = sel;
        SendMessageW(hCombo, CB_SETCURSEL, sel, 0);

        /* Auto Daylight saving checkbox */
        if (g_tz_entries[sel].tzi.StandardDate.wMonth != 0)
        {
            EnableWindow(GetDlgItem(hwnd, IDC_TIMEZONE_AUTO), TRUE);
            CheckDlgButton(hwnd, IDC_TIMEZONE_AUTO, (cur_tzi.StandardDate.wMonth != 0) ? BST_CHECKED : BST_UNCHECKED);
        }
        else
        {
            EnableWindow(GetDlgItem(hwnd, IDC_TIMEZONE_AUTO), FALSE);
            CheckDlgButton(hwnd, IDC_TIMEZONE_AUTO, BST_UNCHECKED);
        }

        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wparam) == IDC_TIMEZONE_LIST && HIWORD(wparam) == CBN_SELCHANGE)
        {
            int sel = (int)SendDlgItemMessageW(hwnd, IDC_TIMEZONE_LIST, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < g_tz_count)
            {
                HWND hMap = GetDlgItem(hwnd, IDC_TIMEZONE_MAP);
                g_selected_tz_idx = sel;

                if (g_tz_entries[sel].tzi.StandardDate.wMonth != 0)
                {
                    EnableWindow(GetDlgItem(hwnd, IDC_TIMEZONE_AUTO), TRUE);
                    CheckDlgButton(hwnd, IDC_TIMEZONE_AUTO, BST_CHECKED);
                }
                else
                {
                    EnableWindow(GetDlgItem(hwnd, IDC_TIMEZONE_AUTO), FALSE);
                    CheckDlgButton(hwnd, IDC_TIMEZONE_AUTO, BST_UNCHECKED);
                }

                if (hMap) InvalidateRect(hMap, NULL, TRUE);
                PropSheet_Changed(GetParent(hwnd), hwnd);
            }
            return TRUE;
        }
        else if (LOWORD(wparam) == IDC_TIMEZONE_AUTO)
        {
            PropSheet_Changed(GetParent(hwnd), hwnd);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *nmhdr = (NMHDR *)lparam;
        if (nmhdr->code == PSN_APPLY)
        {
            if (g_selected_tz_idx >= 0 && g_selected_tz_idx < g_tz_count)
            {
                TIME_ZONE_INFORMATION tzi;
                BOOL auto_dst = (IsDlgButtonChecked(hwnd, IDC_TIMEZONE_AUTO) == BST_CHECKED);
                HKEY hKey;

                memset(&tzi, 0, sizeof(tzi));
                tzi.Bias = g_tz_entries[g_selected_tz_idx].tzi.Bias;
                tzi.StandardBias = g_tz_entries[g_selected_tz_idx].tzi.StandardBias;
                tzi.DaylightBias = g_tz_entries[g_selected_tz_idx].tzi.DaylightBias;
                wcsncpy(tzi.StandardName, g_tz_entries[g_selected_tz_idx].std_name, 32);
                wcsncpy(tzi.DaylightName, g_tz_entries[g_selected_tz_idx].dlt_name, 32);

                if (auto_dst && g_tz_entries[g_selected_tz_idx].tzi.StandardDate.wMonth != 0)
                {
                    tzi.StandardDate = g_tz_entries[g_selected_tz_idx].tzi.StandardDate;
                    tzi.DaylightDate = g_tz_entries[g_selected_tz_idx].tzi.DaylightDate;
                }

                SetTimeZoneInformation(&tzi);

                /* Write TimeZoneKeyName to HKLM\System\CurrentControlSet\Control\TimeZoneInformation */
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\TimeZoneInformation",
                                  0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hKey, L"TimeZoneKeyName", 0, REG_SZ,
                                   (BYTE *)g_tz_entries[g_selected_tz_idx].key_name,
                                   (lstrlenW(g_tz_entries[g_selected_tz_idx].key_name) + 1) * sizeof(WCHAR));
                    RegCloseKey(hKey);
                }

                /* Also persist to HKCU\Software\WineExplorer\Time to survive prefix restarts and kernelbase resets */
                if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WineExplorer\\Time",
                                    0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL) == ERROR_SUCCESS)
                {
                    RegSetValueExW(hKey, L"SelectedTimeZone", 0, REG_SZ,
                                   (BYTE *)g_tz_entries[g_selected_tz_idx].key_name,
                                   (lstrlenW(g_tz_entries[g_selected_tz_idx].key_name) + 1) * sizeof(WCHAR));
                    RegCloseKey(hKey);
                }

                SendMessageW(HWND_BROADCAST, WM_TIMECHANGE, 0, 0);
            }
            SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
    {
        HWND hMap = GetDlgItem(hwnd, IDC_TIMEZONE_MAP);
        if (hMap && g_orig_map_proc)
        {
            SetWindowLongPtrW(hMap, GWLP_WNDPROC, (LONG_PTR)g_orig_map_proc);
            g_orig_map_proc = NULL;
        }
        if (g_hWorldBmp)
        {
            DeleteObject(g_hWorldBmp);
            g_hWorldBmp = NULL;
        }
        break;
    }
    }
    return FALSE;
}
