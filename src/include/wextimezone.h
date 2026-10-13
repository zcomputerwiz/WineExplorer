/*
 * WineExplorer Desktop Environment - wextimezone.h
 * Windows XP time zone definitions and CLDR Windows<->IANA mapping
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
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
 *
 * --------------------------------------------------------------------------
 * Unicode License - v3.0
 * Copyright © 1991-2024 Unicode, Inc. All rights reserved.
 *
 * CLDR data derived from windowsZones.xml (Unicode CLDR release 45 / typeVersion 2021a).
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of the Unicode data files and any associated documentation (the "Data Files")
 * or Unicode software and any associated documentation (the "Software") to deal
 * in the Data Files or Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, and/or sell
 * copies of the Data Files or Software, and to permit persons to whom the Data
 * Files or Software are furnished to do so.
 * --------------------------------------------------------------------------
 */

#ifndef WEX_TIMEZONE_H
#define WEX_TIMEZONE_H

#include <windows.h>
#include <string.h>
#include <wchar.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct {
    const WCHAR *key;
    const WCHAR *display;
    const WCHAR *std;
    const WCHAR *dlt;
    LONG bias;          /* minutes from UTC (positive = west of UTC, e.g. EST = 300) */
    BOOL has_dst;
} WEX_XP_TIMEZONE;

typedef struct {
    const WCHAR *win_name;
    const char *iana_name;
} WEX_TZ_IANA_MAP;

/* Windows XP fallback time zones (54 entries matching timedate.cpl) */
static const WEX_XP_TIMEZONE g_xp_timezones[] = {
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
    { L"Central Pacific Standard Time", L"(GMT+11:00) Magadan, Solomon Is., New Caledonia", L"Central Pacific Standard Time", L"Central Daylight Time", -660, FALSE },
    { L"New Zealand Standard Time", L"(GMT+12:00) Auckland, Wellington", L"New Zealand Standard Time", L"New Zealand Daylight Time", -720, TRUE },
    { L"Fiji Standard Time", L"(GMT+12:00) Fiji, Kamchatka, Marshall Is.", L"Fiji Standard Time", L"Fiji Daylight Time", -720, FALSE },
    { L"Tonga Standard Time", L"(GMT+13:00) Nuku'alofa", L"Tonga Standard Time", L"Tonga Daylight Time", -780, FALSE }
};

/* Windows <-> IANA mapping table based on CLDR windowsZones.xml (typeVersion 2021a) */
static const WEX_TZ_IANA_MAP g_wex_tz_iana_map[] = {
    { L"Dateline Standard Time", "Etc/GMT+12" },
    { L"Samoa Standard Time", "Pacific/Apia" },
    { L"Samoa Standard Time", "Pacific/Midway" },
    { L"Samoa Standard Time", "Pacific/Pago_Pago" },
    { L"Hawaiian Standard Time", "Pacific/Honolulu" },
    { L"Alaskan Standard Time", "America/Anchorage" },
    { L"Pacific Standard Time", "America/Los_Angeles" },
    { L"Pacific Standard Time", "America/Vancouver" },
    { L"Pacific Standard Time", "America/Tijuana" },
    { L"Mountain Standard Time", "America/Denver" },
    { L"Mountain Standard Time", "America/Edmonton" },
    { L"US Mountain Standard Time", "America/Phoenix" },
    { L"Central Standard Time", "America/Chicago" },
    { L"Central Standard Time", "America/Winnipeg" },
    { L"Central Standard Time", "America/Mexico_City" },
    { L"Canada Central Standard Time", "America/Regina" },
    { L"Eastern Standard Time", "America/New_York" },
    { L"Eastern Standard Time", "America/Toronto" },
    { L"US Eastern Standard Time", "America/Indiana/Indianapolis" },
    { L"US Eastern Standard Time", "America/Indianapolis" },
    { L"Atlantic Standard Time", "America/Halifax" },
    { L"SA Western Standard Time", "America/La_Paz" },
    { L"SA Western Standard Time", "America/Caracas" },
    { L"Newfoundland Standard Time", "America/St_Johns" },
    { L"E. South America Standard Time", "America/Sao_Paulo" },
    { L"Greenland Standard Time", "America/Godthab" },
    { L"Greenland Standard Time", "America/Nuuk" },
    { L"Mid-Atlantic Standard Time", "Etc/GMT+2" },
    { L"Azores Standard Time", "Atlantic/Azores" },
    { L"Cape Verde Standard Time", "Atlantic/Cape_Verde" },
    { L"GMT Standard Time", "Europe/London" },
    { L"GMT Standard Time", "Europe/Dublin" },
    { L"Greenwich Standard Time", "Atlantic/Reykjavik" },
    { L"Greenwich Standard Time", "Africa/Monrovia" },
    { L"W. Europe Standard Time", "Europe/Berlin" },
    { L"W. Europe Standard Time", "Europe/Amsterdam" },
    { L"W. Europe Standard Time", "Europe/Rome" },
    { L"W. Europe Standard Time", "Europe/Stockholm" },
    { L"W. Europe Standard Time", "Europe/Vienna" },
    { L"Romance Standard Time", "Europe/Paris" },
    { L"Romance Standard Time", "Europe/Brussels" },
    { L"Romance Standard Time", "Europe/Madrid" },
    { L"Romance Standard Time", "Europe/Copenhagen" },
    { L"GTB Standard Time", "Europe/Bucharest" },
    { L"GTB Standard Time", "Europe/Athens" },
    { L"GTB Standard Time", "Europe/Istanbul" },
    { L"Egypt Standard Time", "Africa/Cairo" },
    { L"Israel Standard Time", "Asia/Jerusalem" },
    { L"Russian Standard Time", "Europe/Moscow" },
    { L"Russian Standard Time", "Europe/Simferopol" },
    { L"Arab Standard Time", "Asia/Riyadh" },
    { L"Arab Standard Time", "Asia/Kuwait" },
    { L"Iran Standard Time", "Asia/Tehran" },
    { L"Arabian Standard Time", "Asia/Dubai" },
    { L"Arabian Standard Time", "Asia/Muscat" },
    { L"Caucasus Standard Time", "Asia/Yerevan" },
    { L"Caucasus Standard Time", "Asia/Baku" },
    { L"Caucasus Standard Time", "Asia/Tbilisi" },
    { L"Afghanistan Standard Time", "Asia/Kabul" },
    { L"Ekaterinburg Standard Time", "Asia/Yekaterinburg" },
    { L"West Asia Standard Time", "Asia/Tashkent" },
    { L"West Asia Standard Time", "Asia/Karachi" },
    { L"India Standard Time", "Asia/Kolkata" },
    { L"India Standard Time", "Asia/Calcutta" },
    { L"Nepal Standard Time", "Asia/Kathmandu" },
    { L"Nepal Standard Time", "Asia/Katmandu" },
    { L"Central Asia Standard Time", "Asia/Almaty" },
    { L"Central Asia Standard Time", "Asia/Dhaka" },
    { L"Central Asia Standard Time", "Asia/Bishkek" },
    { L"Myanmar Standard Time", "Asia/Yangon" },
    { L"Myanmar Standard Time", "Asia/Rangoon" },
    { L"SE Asia Standard Time", "Asia/Bangkok" },
    { L"SE Asia Standard Time", "Asia/Jakarta" },
    { L"SE Asia Standard Time", "Asia/Saigon" },
    { L"SE Asia Standard Time", "Asia/Ho_Chi_Minh" },
    { L"China Standard Time", "Asia/Shanghai" },
    { L"China Standard Time", "Asia/Hong_Kong" },
    { L"China Standard Time", "Asia/Beijing" },
    { L"Singapore Standard Time", "Asia/Singapore" },
    { L"Singapore Standard Time", "Asia/Kuala_Lumpur" },
    { L"Taipei Standard Time", "Asia/Taipei" },
    { L"Tokyo Standard Time", "Asia/Tokyo" },
    { L"Korea Standard Time", "Asia/Seoul" },
    { L"Yakutsk Standard Time", "Asia/Yakutsk" },
    { L"Cen. Australia Standard Time", "Australia/Adelaide" },
    { L"AUS Central Standard Time", "Australia/Darwin" },
    { L"E. Australia Standard Time", "Australia/Brisbane" },
    { L"AUS Eastern Standard Time", "Australia/Sydney" },
    { L"AUS Eastern Standard Time", "Australia/Melbourne" },
    { L"Vladivostok Standard Time", "Asia/Vladivostok" },
    { L"Central Pacific Standard Time", "Pacific/Guadalcanal" },
    { L"New Zealand Standard Time", "Pacific/Auckland" },
    { L"Fiji Standard Time", "Pacific/Fiji" },
    { L"Tonga Standard Time", "Pacific/Tongatapu" },
    { L"UTC", "UTC" },
    { L"UTC", "Etc/UTC" },
    { L"GMT Standard Time", "Etc/GMT" }
};

/* Host zone name for a Windows zone key; returns NULL if unknown */
static inline const char *wex_iana_for_key(const WCHAR *tz_key_name)
{
    DWORD i;
    if (!tz_key_name || !tz_key_name[0]) return NULL;
    for (i = 0; i < ARRAY_SIZE(g_wex_tz_iana_map); i++) {
        if (!_wcsicmp(g_wex_tz_iana_map[i].win_name, tz_key_name))
            return g_wex_tz_iana_map[i].iana_name;
    }
    return NULL;
}

/* Windows zone key for a host/IANA zone name; returns NULL if unknown */
static inline const WCHAR *wex_key_for_iana(const char *iana_name)
{
    DWORD i;
    if (!iana_name || !iana_name[0]) return NULL;
    for (i = 0; i < ARRAY_SIZE(g_wex_tz_iana_map); i++) {
        if (!_stricmp(g_wex_tz_iana_map[i].iana_name, iana_name))
            return g_wex_tz_iana_map[i].win_name;
    }
    return NULL;
}

/*
 * Resolve any user-supplied timezone string (IANA name, Windows key, or XP display name)
 * to its corresponding IANA name and Windows key name.
 * Returns pointer to IANA string on success, or NULL on failure.
 */
static inline const char *wex_resolve_timezone(const char *input, char *iana_out, size_t iana_size,
                                               WCHAR *win_key_out, size_t win_key_size)
{
    WCHAR wbuf[128];
    const char *iana_match = NULL;
    const WCHAR *win_match = NULL;
    DWORD i;

    if (!input || !input[0]) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, input, -1, wbuf, ARRAY_SIZE(wbuf));

    /* 1. Try matching input directly as an IANA name in g_wex_tz_iana_map */
    win_match = wex_key_for_iana(input);
    if (win_match) {
        iana_match = input;
    }

    /* 2. Try matching input as a Windows key name */
    if (!iana_match) {
        iana_match = wex_iana_for_key(wbuf);
        if (iana_match) {
            win_match = wbuf;
        }
    }

    /* 3. Try matching input against XP display names or std names */
    if (!iana_match) {
        for (i = 0; i < ARRAY_SIZE(g_xp_timezones); i++) {
            char disp_a[128];
            WideCharToMultiByte(CP_UTF8, 0, g_xp_timezones[i].display, -1, disp_a, sizeof(disp_a), NULL, NULL);
            if (!_stricmp(input, disp_a) || !_wcsicmp(wbuf, g_xp_timezones[i].display) ||
                !_wcsicmp(wbuf, g_xp_timezones[i].std)) {
                win_match = g_xp_timezones[i].key;
                iana_match = wex_iana_for_key(win_match);
                break;
            }
        }
    }

    /* 4. If input looks like an IANA zone (contains '/' or is "UTC"), accept it directly */
    if (!iana_match && (strchr(input, '/') || !_stricmp(input, "UTC"))) {
        iana_match = input;
        win_match = L"UTC";
    }

    if (iana_match) {
        if (iana_out && iana_size > 0) {
            lstrcpynA(iana_out, iana_match, (int)iana_size);
        }
        if (win_key_out && win_key_size > 0 && win_match) {
            lstrcpynW(win_key_out, win_match, (int)win_key_size);
        }
        return iana_match;
    }

    return NULL;
}

#endif /* WEX_TIMEZONE_H */
