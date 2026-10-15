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
 * CLDR data derived from windowsZones.xml (Unicode CLDR release 45 / typeVersion 2021a),
 * used under the Unicode License v3:
 *
 * UNICODE LICENSE V3
 *
 * COPYRIGHT AND PERMISSION NOTICE
 *
 * Copyright © 1991-2026 Unicode, Inc.
 *
 * NOTICE TO USER: Carefully read the following legal agreement. BY
 * DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
 * SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
 * TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
 * DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of data files and any associated documentation (the "Data Files") or
 * software and any associated documentation (the "Software") to deal in the
 * Data Files or Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, and/or sell
 * copies of the Data Files or Software, and to permit persons to whom the
 * Data Files or Software are furnished to do so, provided that either (a)
 * this copyright and permission notice appear with all copies of the Data
 * Files or Software, or (b) this copyright and permission notice appear in
 * associated Documentation.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
 * BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
 * FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in these Data Files or Software without prior written
 * authorization of the copyright holder.
 * --------------------------------------------------------------------------
 */


#ifndef WEX_TIMEZONE_H
#define WEX_TIMEZONE_H

#include <windows.h>
#include <string.h>
#include <wchar.h>
#include <stdlib.h>

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
    DWORD index;        /* legacy unattend.sif numeric index (e.g. 85 for GMT, 4 for PST) */
} WEX_XP_TIMEZONE;

typedef struct {
    const WCHAR *win_name;
    const char *iana_name;
} WEX_TZ_IANA_MAP;

/* Windows XP time zones (75 entries in canonical GMT bias order) */
static const WEX_XP_TIMEZONE g_xp_timezones[] = {
    { L"Dateline Standard Time", L"(GMT-12:00) International Date Line West", L"Dateline Standard Time", L"Dateline Daylight Time", 720, FALSE, 0 },
    { L"Samoa Standard Time", L"(GMT-11:00) Midway Island, Samoa", L"Samoa Standard Time", L"Samoa Daylight Time", 660, FALSE, 1 },
    { L"Hawaiian Standard Time", L"(GMT-10:00) Hawaii", L"Hawaiian Standard Time", L"Hawaiian Daylight Time", 600, FALSE, 2 },
    { L"Alaskan Standard Time", L"(GMT-09:00) Alaska", L"Alaskan Standard Time", L"Alaskan Daylight Time", 540, TRUE, 3 },
    { L"Pacific Standard Time", L"(GMT-08:00) Pacific Time (US & Canada); Tijuana", L"Pacific Standard Time", L"Pacific Daylight Time", 480, TRUE, 4 },
    { L"Mountain Standard Time", L"(GMT-07:00) Mountain Time (US & Canada)", L"Mountain Standard Time", L"Mountain Daylight Time", 420, TRUE, 10 },
    { L"Mexico Standard Time 2", L"(GMT-07:00) Chihuahua, La Paz, Mazatlan", L"Mexico Standard Time 2", L"Mexico Daylight Time 2", 420, TRUE, 13 },
    { L"US Mountain Standard Time", L"(GMT-07:00) Arizona", L"US Mountain Standard Time", L"US Mountain Daylight Time", 420, FALSE, 15 },
    { L"Central Standard Time", L"(GMT-06:00) Central Time (US & Canada)", L"Central Standard Time", L"Central Daylight Time", 360, TRUE, 20 },
    { L"Canada Central Standard Time", L"(GMT-06:00) Saskatchewan", L"Canada Central Standard Time", L"Canada Central Daylight Time", 360, FALSE, 25 },
    { L"Mexico Standard Time", L"(GMT-06:00) Guadalajara, Mexico City, Monterrey", L"Mexico Standard Time", L"Mexico Daylight Time", 360, TRUE, 30 },
    { L"Central America Standard Time", L"(GMT-06:00) Central America", L"Central America Standard Time", L"Central America Daylight Time", 360, FALSE, 33 },
    { L"Eastern Standard Time", L"(GMT-05:00) Eastern Time (US & Canada)", L"Eastern Standard Time", L"Eastern Daylight Time", 300, TRUE, 35 },
    { L"US Eastern Standard Time", L"(GMT-05:00) Indiana (East)", L"US Eastern Standard Time", L"US Eastern Daylight Time", 300, FALSE, 40 },
    { L"SA Pacific Standard Time", L"(GMT-05:00) Bogota, Lima, Quito", L"SA Pacific Standard Time", L"SA Pacific Daylight Time", 300, FALSE, 45 },
    { L"Atlantic Standard Time", L"(GMT-04:00) Atlantic Time (Canada)", L"Atlantic Standard Time", L"Atlantic Daylight Time", 240, TRUE, 50 },
    { L"SA Western Standard Time", L"(GMT-04:00) Caracas, La Paz", L"SA Western Standard Time", L"SA Western Daylight Time", 240, FALSE, 55 },
    { L"Pacific SA Standard Time", L"(GMT-04:00) Santiago", L"Pacific SA Standard Time", L"Pacific SA Daylight Time", 240, TRUE, 56 },
    { L"Newfoundland Standard Time", L"(GMT-03:30) Newfoundland", L"Newfoundland Standard Time", L"Newfoundland Daylight Time", 210, TRUE, 60 },
    { L"E. South America Standard Time", L"(GMT-03:00) Brasilia", L"E. South America Standard Time", L"E. South America Daylight Time", 180, TRUE, 65 },
    { L"SA Eastern Standard Time", L"(GMT-03:00) Buenos Aires, Georgetown", L"SA Eastern Standard Time", L"SA Eastern Daylight Time", 180, FALSE, 70 },
    { L"Greenland Standard Time", L"(GMT-03:00) Greenland", L"Greenland Standard Time", L"Greenland Daylight Time", 180, TRUE, 73 },
    { L"Mid-Atlantic Standard Time", L"(GMT-02:00) Mid-Atlantic", L"Mid-Atlantic Standard Time", L"Mid-Atlantic Daylight Time", 120, TRUE, 75 },
    { L"Azores Standard Time", L"(GMT-01:00) Azores", L"Azores Standard Time", L"Azores Daylight Time", 60, TRUE, 80 },
    { L"Cape Verde Standard Time", L"(GMT-01:00) Cape Verde Is.", L"Cape Verde Standard Time", L"Cape Verde Daylight Time", 60, FALSE, 83 },
    { L"GMT Standard Time", L"(GMT) Greenwich Mean Time : Dublin, Edinburgh, Lisbon, London", L"GMT Standard Time", L"GMT Daylight Time", 0, TRUE, 85 },
    { L"Greenwich Standard Time", L"(GMT) Casablanca, Monrovia", L"Greenwich Standard Time", L"Greenwich Daylight Time", 0, FALSE, 90 },
    { L"Central Europe Standard Time", L"(GMT+01:00) Belgrade, Bratislava, Budapest, Ljubljana, Prague", L"Central Europe Standard Time", L"Central Europe Daylight Time", -60, TRUE, 95 },
    { L"Central European Standard Time", L"(GMT+01:00) Sarajevo, Skopje, Warsaw, Zagreb", L"Central European Standard Time", L"Central European Daylight Time", -60, TRUE, 100 },
    { L"Romance Standard Time", L"(GMT+01:00) Brussels, Copenhagen, Madrid, Paris", L"Romance Standard Time", L"Romance Daylight Time", -60, TRUE, 105 },
    { L"W. Europe Standard Time", L"(GMT+01:00) Amsterdam, Berlin, Bern, Rome, Stockholm, Vienna", L"W. Europe Standard Time", L"W. Europe Daylight Time", -60, TRUE, 110 },
    { L"W. Central Africa Standard Time", L"(GMT+01:00) West Central Africa", L"W. Central Africa Standard Time", L"W. Central Africa Daylight Time", -60, FALSE, 113 },
    { L"E. Europe Standard Time", L"(GMT+02:00) Bucharest", L"E. Europe Standard Time", L"E. Europe Daylight Time", -120, TRUE, 115 },
    { L"Egypt Standard Time", L"(GMT+02:00) Cairo", L"Egypt Standard Time", L"Egypt Daylight Time", -120, TRUE, 120 },
    { L"FLE Standard Time", L"(GMT+02:00) Helsinki, Kyiv, Riga, Sofia, Tallinn, Vilnius", L"FLE Standard Time", L"FLE Daylight Time", -120, TRUE, 125 },
    { L"GTB Standard Time", L"(GMT+02:00) Athens, Istanbul, Minsk", L"GTB Standard Time", L"GTB Daylight Time", -120, TRUE, 130 },
    { L"Israel Standard Time", L"(GMT+02:00) Jerusalem", L"Israel Standard Time", L"Israel Daylight Time", -120, TRUE, 135 },
    { L"South Africa Standard Time", L"(GMT+02:00) Harare, Pretoria", L"South Africa Standard Time", L"South Africa Daylight Time", -120, FALSE, 140 },
    { L"Russian Standard Time", L"(GMT+03:00) Moscow, St. Petersburg, Volgograd", L"Russian Standard Time", L"Russian Daylight Time", -180, TRUE, 145 },
    { L"Arab Standard Time", L"(GMT+03:00) Kuwait, Riyadh", L"Arab Standard Time", L"Arab Daylight Time", -180, FALSE, 150 },
    { L"E. Africa Standard Time", L"(GMT+03:00) Nairobi", L"E. Africa Standard Time", L"E. Africa Daylight Time", -180, FALSE, 155 },
    { L"Arabic Standard Time", L"(GMT+03:00) Baghdad", L"Arabic Standard Time", L"Arabic Daylight Time", -180, FALSE, 158 },
    { L"Iran Standard Time", L"(GMT+03:30) Tehran", L"Iran Standard Time", L"Iran Daylight Time", -210, TRUE, 160 },
    { L"Arabian Standard Time", L"(GMT+04:00) Abu Dhabi, Muscat", L"Arabian Standard Time", L"Arabian Daylight Time", -240, FALSE, 165 },
    { L"Caucasus Standard Time", L"(GMT+04:00) Baku, Tbilisi, Yerevan", L"Caucasus Standard Time", L"Caucasus Daylight Time", -240, TRUE, 170 },
    { L"Afghanistan Standard Time", L"(GMT+04:30) Kabul", L"Afghanistan Standard Time", L"Afghanistan Daylight Time", -270, FALSE, 175 },
    { L"Ekaterinburg Standard Time", L"(GMT+05:00) Ekaterinburg", L"Ekaterinburg Standard Time", L"Ekaterinburg Daylight Time", -300, TRUE, 180 },
    { L"West Asia Standard Time", L"(GMT+05:00) Islamabad, Karachi, Tashkent", L"West Asia Standard Time", L"West Asia Daylight Time", -300, FALSE, 185 },
    { L"India Standard Time", L"(GMT+05:30) Chennai, Kolkata, Mumbai, New Delhi", L"India Standard Time", L"India Daylight Time", -330, FALSE, 190 },
    { L"Nepal Standard Time", L"(GMT+05:45) Kathmandu", L"Nepal Standard Time", L"Nepal Daylight Time", -345, FALSE, 193 },
    { L"Central Asia Standard Time", L"(GMT+06:00) Astana, Dhaka", L"Central Asia Standard Time", L"Central Asia Daylight Time", -360, FALSE, 195 },
    { L"Sri Lanka Standard Time", L"(GMT+06:00) Sri Jayawardenepura", L"Sri Lanka Standard Time", L"Sri Lanka Daylight Time", -360, FALSE, 200 },
    { L"N. Central Asia Standard Time", L"(GMT+06:00) Almaty, Novosibirsk", L"N. Central Asia Standard Time", L"N. Central Asia Daylight Time", -360, TRUE, 201 },
    { L"Myanmar Standard Time", L"(GMT+06:30) Yangon (Rangoon)", L"Myanmar Standard Time", L"Myanmar Daylight Time", -390, FALSE, 203 },
    { L"SE Asia Standard Time", L"(GMT+07:00) Bangkok, Hanoi, Jakarta", L"SE Asia Standard Time", L"SE Asia Daylight Time", -420, FALSE, 205 },
    { L"North Asia Standard Time", L"(GMT+07:00) Krasnoyarsk", L"North Asia Standard Time", L"North Asia Daylight Time", -420, TRUE, 207 },
    { L"China Standard Time", L"(GMT+08:00) Beijing, Chongqing, Hong Kong, Urumqi", L"China Standard Time", L"China Daylight Time", -480, FALSE, 210 },
    { L"Singapore Standard Time", L"(GMT+08:00) Kuala Lumpur, Singapore", L"Singapore Standard Time", L"Singapore Daylight Time", -480, FALSE, 215 },
    { L"Taipei Standard Time", L"(GMT+08:00) Taipei", L"Taipei Standard Time", L"Taipei Daylight Time", -480, FALSE, 220 },
    { L"W. Australia Standard Time", L"(GMT+08:00) Perth", L"W. Australia Standard Time", L"W. Australia Daylight Time", -480, FALSE, 225 },
    { L"North Asia East Standard Time", L"(GMT+08:00) Irkutsk, Ulaan Bataar", L"North Asia East Standard Time", L"North Asia East Daylight Time", -480, TRUE, 227 },
    { L"Korea Standard Time", L"(GMT+09:00) Seoul", L"Korea Standard Time", L"Korea Daylight Time", -540, FALSE, 230 },
    { L"Tokyo Standard Time", L"(GMT+09:00) Osaka, Sapporo, Tokyo", L"Tokyo Standard Time", L"Tokyo Daylight Time", -540, FALSE, 235 },
    { L"Yakutsk Standard Time", L"(GMT+09:00) Yakutsk", L"Yakutsk Standard Time", L"Yakutsk Daylight Time", -540, TRUE, 240 },
    { L"AUS Central Standard Time", L"(GMT+09:30) Darwin", L"AUS Central Standard Time", L"AUS Central Daylight Time", -570, FALSE, 245 },
    { L"Cen. Australia Standard Time", L"(GMT+09:30) Adelaide", L"Cen. Australia Standard Time", L"Cen. Australia Daylight Time", -570, TRUE, 250 },
    { L"AUS Eastern Standard Time", L"(GMT+10:00) Canberra, Melbourne, Sydney", L"AUS Eastern Standard Time", L"AUS Eastern Daylight Time", -600, TRUE, 255 },
    { L"E. Australia Standard Time", L"(GMT+10:00) Brisbane", L"E. Australia Standard Time", L"E. Australia Daylight Time", -600, FALSE, 260 },
    { L"Tasmania Standard Time", L"(GMT+10:00) Hobart", L"Tasmania Standard Time", L"Tasmania Daylight Time", -600, TRUE, 265 },
    { L"Vladivostok Standard Time", L"(GMT+10:00) Vladivostok", L"Vladivostok Standard Time", L"Vladivostok Daylight Time", -600, TRUE, 270 },
    { L"West Pacific Standard Time", L"(GMT+10:00) Guam, Port Moresby", L"West Pacific Standard Time", L"West Pacific Daylight Time", -600, FALSE, 275 },
    { L"Central Pacific Standard Time", L"(GMT+11:00) Magadan, Solomon Is., New Caledonia", L"Central Pacific Standard Time", L"Central Pacific Daylight Time", -660, FALSE, 280 },
    { L"Fiji Standard Time", L"(GMT+12:00) Fiji, Kamchatka, Marshall Is.", L"Fiji Standard Time", L"Fiji Daylight Time", -720, FALSE, 285 },
    { L"New Zealand Standard Time", L"(GMT+12:00) Auckland, Wellington", L"New Zealand Standard Time", L"New Zealand Daylight Time", -720, TRUE, 290 },
    { L"Tonga Standard Time", L"(GMT+13:00) Nuku'alofa", L"Tonga Standard Time", L"Tonga Daylight Time", -780, FALSE, 300 },
};

/* Windows <-> IANA mapping table based on CLDR windowsZones.xml */
static const WEX_TZ_IANA_MAP g_wex_tz_iana_map[] = {
    { L"Dateline Standard Time", "Etc/GMT+12" },
    { L"Samoa Standard Time", "Pacific/Apia" },
    { L"Hawaiian Standard Time", "Pacific/Honolulu" },
    { L"Alaskan Standard Time", "America/Anchorage" },
    { L"Pacific Standard Time", "America/Los_Angeles" },
    { L"Mountain Standard Time", "America/Denver" },
    { L"Mexico Standard Time 2", "America/Mazatlan" },
    { L"US Mountain Standard Time", "America/Phoenix" },
    { L"Central Standard Time", "America/Chicago" },
    { L"Canada Central Standard Time", "America/Regina" },
    { L"Mexico Standard Time", "America/Mexico_City" },
    { L"Central America Standard Time", "America/Guatemala" },
    { L"Eastern Standard Time", "America/New_York" },
    { L"US Eastern Standard Time", "America/Indianapolis" },
    { L"SA Pacific Standard Time", "America/Bogota" },
    { L"Atlantic Standard Time", "America/Halifax" },
    { L"SA Western Standard Time", "America/La_Paz" },
    { L"Pacific SA Standard Time", "America/Santiago" },
    { L"Newfoundland Standard Time", "America/St_Johns" },
    { L"E. South America Standard Time", "America/Sao_Paulo" },
    { L"SA Eastern Standard Time", "America/Cayenne" },
    { L"Greenland Standard Time", "America/Godthab" },
    { L"Mid-Atlantic Standard Time", "Etc/GMT+2" },
    { L"Azores Standard Time", "Atlantic/Azores" },
    { L"Cape Verde Standard Time", "Atlantic/Cape_Verde" },
    { L"GMT Standard Time", "Europe/London" },
    { L"Greenwich Standard Time", "Atlantic/Reykjavik" },
    { L"Central Europe Standard Time", "Europe/Budapest" },
    { L"Central European Standard Time", "Europe/Warsaw" },
    { L"Romance Standard Time", "Europe/Paris" },
    { L"W. Europe Standard Time", "Europe/Berlin" },
    { L"W. Central Africa Standard Time", "Africa/Lagos" },
    { L"E. Europe Standard Time", "Europe/Chisinau" },
    { L"Egypt Standard Time", "Africa/Cairo" },
    { L"FLE Standard Time", "Europe/Kiev" },
    { L"GTB Standard Time", "Europe/Bucharest" },
    { L"Israel Standard Time", "Asia/Jerusalem" },
    { L"South Africa Standard Time", "Africa/Johannesburg" },
    { L"Russian Standard Time", "Europe/Moscow" },
    { L"Arab Standard Time", "Asia/Riyadh" },
    { L"E. Africa Standard Time", "Africa/Nairobi" },
    { L"Arabic Standard Time", "Asia/Baghdad" },
    { L"Iran Standard Time", "Asia/Tehran" },
    { L"Arabian Standard Time", "Asia/Dubai" },
    { L"Caucasus Standard Time", "Asia/Yerevan" },
    { L"Afghanistan Standard Time", "Asia/Kabul" },
    { L"Ekaterinburg Standard Time", "Asia/Yekaterinburg" },
    { L"West Asia Standard Time", "Asia/Tashkent" },
    { L"India Standard Time", "Asia/Calcutta" },
    { L"Nepal Standard Time", "Asia/Katmandu" },
    { L"Central Asia Standard Time", "Asia/Bishkek" },
    { L"Sri Lanka Standard Time", "Asia/Colombo" },
    { L"N. Central Asia Standard Time", "Asia/Novosibirsk" },
    { L"Myanmar Standard Time", "Asia/Rangoon" },
    { L"SE Asia Standard Time", "Asia/Bangkok" },
    { L"North Asia Standard Time", "Asia/Krasnoyarsk" },
    { L"China Standard Time", "Asia/Shanghai" },
    { L"Singapore Standard Time", "Asia/Singapore" },
    { L"Taipei Standard Time", "Asia/Taipei" },
    { L"W. Australia Standard Time", "Australia/Perth" },
    { L"North Asia East Standard Time", "Asia/Irkutsk" },
    { L"Korea Standard Time", "Asia/Seoul" },
    { L"Tokyo Standard Time", "Asia/Tokyo" },
    { L"Yakutsk Standard Time", "Asia/Yakutsk" },
    { L"AUS Central Standard Time", "Australia/Darwin" },
    { L"Cen. Australia Standard Time", "Australia/Adelaide" },
    { L"AUS Eastern Standard Time", "Australia/Sydney" },
    { L"E. Australia Standard Time", "Australia/Brisbane" },
    { L"Tasmania Standard Time", "Australia/Hobart" },
    { L"Vladivostok Standard Time", "Asia/Vladivostok" },
    { L"West Pacific Standard Time", "Pacific/Port_Moresby" },
    { L"Central Pacific Standard Time", "Pacific/Guadalcanal" },
    { L"Fiji Standard Time", "Pacific/Fiji" },
    { L"New Zealand Standard Time", "Pacific/Auckland" },
    { L"Tonga Standard Time", "Pacific/Tongatapu" },
    { L"Samoa Standard Time", "Pacific/Midway" },
    { L"Samoa Standard Time", "Pacific/Pago_Pago" },
    { L"Hawaiian Standard Time", "Pacific/Tahiti" },
    { L"Pacific Standard Time", "America/Vancouver" },
    { L"Pacific Standard Time", "America/Tijuana" },
    { L"Mountain Standard Time", "America/Edmonton" },
    { L"Mountain Standard Time (Mexico)", "America/Mazatlan" },
    { L"Central Standard Time", "America/Winnipeg" },
    { L"Central Standard Time", "America/Mexico_City" },
    { L"Central Standard Time (Mexico)", "America/Mexico_City" },
    { L"Eastern Standard Time", "America/Toronto" },
    { L"US Eastern Standard Time", "America/Indiana/Indianapolis" },
    { L"SA Pacific Standard Time", "America/Lima" },
    { L"SA Pacific Standard Time", "America/Quito" },
    { L"SA Western Standard Time", "America/Caracas" },
    { L"SA Eastern Standard Time", "America/Buenos_Aires" },
    { L"Greenland Standard Time", "America/Nuuk" },
    { L"GMT Standard Time", "Europe/Dublin" },
    { L"Greenwich Standard Time", "Africa/Monrovia" },
    { L"Greenwich Standard Time", "Africa/Casablanca" },
    { L"Central Europe Standard Time", "Europe/Prague" },
    { L"Central Europe Standard Time", "Europe/Belgrade" },
    { L"Central European Standard Time", "Europe/Sarajevo" },
    { L"Central European Standard Time", "Europe/Zagreb" },
    { L"Romance Standard Time", "Europe/Brussels" },
    { L"Romance Standard Time", "Europe/Madrid" },
    { L"Romance Standard Time", "Europe/Copenhagen" },
    { L"W. Europe Standard Time", "Europe/Amsterdam" },
    { L"W. Europe Standard Time", "Europe/Rome" },
    { L"W. Europe Standard Time", "Europe/Stockholm" },
    { L"W. Europe Standard Time", "Europe/Vienna" },
    { L"E. Europe Standard Time", "Europe/Bucharest" },
    { L"FLE Standard Time", "Europe/Helsinki" },
    { L"FLE Standard Time", "Europe/Riga" },
    { L"FLE Standard Time", "Europe/Tallinn" },
    { L"FLE Standard Time", "Europe/Vilnius" },
    { L"GTB Standard Time", "Europe/Athens" },
    { L"GTB Standard Time", "Europe/Istanbul" },
    { L"GTB Standard Time", "Europe/Minsk" },
    { L"Russian Standard Time", "Europe/Simferopol" },
    { L"Arab Standard Time", "Asia/Kuwait" },
    { L"Arabian Standard Time", "Asia/Muscat" },
    { L"Caucasus Standard Time", "Asia/Baku" },
    { L"Caucasus Standard Time", "Asia/Tbilisi" },
    { L"West Asia Standard Time", "Asia/Karachi" },
    { L"India Standard Time", "Asia/Kolkata" },
    { L"Nepal Standard Time", "Asia/Kathmandu" },
    { L"Central Asia Standard Time", "Asia/Almaty" },
    { L"Central Asia Standard Time", "Asia/Dhaka" },
    { L"Myanmar Standard Time", "Asia/Yangon" },
    { L"SE Asia Standard Time", "Asia/Jakarta" },
    { L"SE Asia Standard Time", "Asia/Saigon" },
    { L"SE Asia Standard Time", "Asia/Ho_Chi_Minh" },
    { L"China Standard Time", "Asia/Hong_Kong" },
    { L"China Standard Time", "Asia/Beijing" },
    { L"Singapore Standard Time", "Asia/Kuala_Lumpur" },
    { L"North Asia East Standard Time", "Asia/Ulaanbaatar" },
    { L"AUS Eastern Standard Time", "Australia/Melbourne" },
    { L"West Pacific Standard Time", "Pacific/Guam" },
    { L"Fiji Islands Standard Time", "Pacific/Fiji" },
    { L"UTC", "UTC" },
    { L"UTC", "Etc/UTC" },
    { L"GMT Standard Time", "Etc/GMT" },
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
 * Resolve any user-supplied timezone string (numeric index, IANA name, Windows key, or XP display name)
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

    /* 1. Try matching input as a numeric index (e.g. "085", "85", "4", "004") */
    if (input[0] >= '0' && input[0] <= '9') {
        char *endptr = NULL;
        unsigned long idx = strtoul(input, &endptr, 10);
        if (endptr && *endptr == '\0') {
            for (i = 0; i < ARRAY_SIZE(g_xp_timezones); i++) {
                if (g_xp_timezones[i].index == (DWORD)idx) {
                    win_match = g_xp_timezones[i].key;
                    iana_match = wex_iana_for_key(win_match);
                    break;
                }
            }
        }
    }

    /* 2. Try matching input directly as an IANA name in g_wex_tz_iana_map */
    if (!iana_match) {
        win_match = wex_key_for_iana(input);
        if (win_match) {
            iana_match = input;
        }
    }

    MultiByteToWideChar(CP_UTF8, 0, input, -1, wbuf, ARRAY_SIZE(wbuf));

    /* 3. Try matching input as a Windows key name */
    if (!iana_match) {
        iana_match = wex_iana_for_key(wbuf);
        if (iana_match) {
            win_match = wbuf;
        }
    }

    /* 4. Try matching input against XP display names or std names */
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

    /* 5. If input looks like an IANA zone (contains '/' or is "UTC"), accept it directly */
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
