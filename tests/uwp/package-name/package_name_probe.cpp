/*
 * WineExplorer U1-R0B PackageFullNameFromId native observation probe
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <windows.h>
#include <appmodel.h>
#include <bcrypt.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

#if defined(_M_X64)
static const wchar_t *const kArch = L"x64";
#elif defined(_M_IX86)
static const wchar_t *const kArch = L"x86";
#else
static const wchar_t *const kArch = L"unknown";
#endif

typedef LONG (WINAPI *pfnPackageFullNameFromId)(const PACKAGE_ID *packageId, UINT32 *packageFullNameLength, PWSTR packageFullName);

static pfnPackageFullNameFromId g_pfnPackageFullNameFromId = nullptr;
static std::wstring g_output;

static std::wstring jsonEscape(const std::wstring &s)
{
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s)
    {
        switch (c)
        {
        case L'"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\b': out += L"\\b"; break;
        case L'\f': out += L"\\f"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (static_cast<unsigned short>(c) < 0x20 || static_cast<unsigned short>(c) > 0x7E)
            {
                wchar_t buf[8];
                std::swprintf(buf, 8, L"\\u%04x", static_cast<unsigned short>(c));
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

static std::wstring derivePublisherIdRef(const wchar_t *publisher)
{
    if (!publisher)
        return L"";

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    BYTE hash[32];
    DWORD hashLen = sizeof(hash);
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status))
        return L"";

    status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    size_t byteCount = wcslen(publisher) * sizeof(wchar_t);
    status = BCryptHashData(hHash, (PUCHAR)publisher, (ULONG)byteCount, 0);
    if (!BCRYPT_SUCCESS(status))
    {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return L"";
    }

    status = BCryptFinishHash(hHash, hash, hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!BCRYPT_SUCCESS(status))
        return L"";

    static const wchar_t charset[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    wchar_t id[14];
    id[0]  = charset[(hash[0] >> 3) & 0x1f];
    id[1]  = charset[((hash[0] << 2) | (hash[1] >> 6)) & 0x1f];
    id[2]  = charset[(hash[1] >> 1) & 0x1f];
    id[3]  = charset[((hash[1] << 4) | (hash[2] >> 4)) & 0x1f];
    id[4]  = charset[((hash[2] << 1) | (hash[3] >> 7)) & 0x1f];
    id[5]  = charset[(hash[3] >> 2) & 0x1f];
    id[6]  = charset[((hash[3] << 3) | (hash[4] >> 5)) & 0x1f];
    id[7]  = charset[hash[4] & 0x1f];
    id[8]  = charset[(hash[5] >> 3) & 0x1f];
    id[9]  = charset[((hash[5] << 2) | (hash[6] >> 6)) & 0x1f];
    id[10] = charset[(hash[6] >> 1) & 0x1f];
    id[11] = charset[((hash[6] << 4) | (hash[7] >> 4)) & 0x1f];
    id[12] = charset[(hash[7] << 1) & 0x1f];
    id[13] = L'\0';

    return std::wstring(id);
}

struct Observation
{
    std::wstring category = {};
    std::wstring testName = {};
    LONG returnCode = 0;
    bool hasLengthPointer = false;
    UINT32 inLength = 0;
    UINT32 outLength = 0;
    std::wstring resultString = {};
    std::wstring expectedString = {};
    bool sentinelChecked = false;
    bool sentinelIntact = false;
    std::wstring notes = {};
};

static void recordObservation(const Observation &obs)
{
    std::wstring line = L"{\"schema\":\"wex-uwp-package-name/1\",\"arch\":\"";
    line += kArch;
    line += L"\",\"category\":\"";
    line += jsonEscape(obs.category);
    line += L"\",\"test\":\"";
    line += jsonEscape(obs.testName);
    line += L"\",\"ret\":";
    line += std::to_wstring(obs.returnCode);
    line += L",\"in_len\":";
    if (obs.hasLengthPointer)
        line += std::to_wstring(obs.inLength);
    else
        line += L"\"NA\"";
    line += L",\"out_len\":";
    if (obs.hasLengthPointer)
        line += std::to_wstring(obs.outLength);
    else
        line += L"\"NA\"";
    line += L",\"str\":\"";
    line += jsonEscape(obs.resultString);
    line += L"\",\"expected\":\"";
    line += jsonEscape(obs.expectedString);
    line += L"\",\"sentinel_checked\":";
    line += (obs.sentinelChecked ? L"true" : L"false");
    line += L",\"sentinel_intact\":";
    line += (obs.sentinelIntact ? L"true" : L"false");
    line += L",\"notes\":\"";
    line += jsonEscape(obs.notes);
    line += L"\"}\n";

    g_output += line;
}

static void testBufferCapacities()
{
    PACKAGE_ID id = {0};
    id.name = (PWSTR)L"TestPackage";
    id.version.Major = 1;
    id.version.Minor = 2;
    id.version.Build = 3;
    id.version.Revision = 4;
    id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    id.resourceId = (PWSTR)L"res";
    id.publisherId = (PWSTR)L"1234567890abc";

    // 1. Buffer NULL, length 0
    {
        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, nullptr);
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"null_buffer_len_0";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = L"Query required length with null buffer";
        recordObservation(obs);
    }

    // Determine required length
    UINT32 reqLen = 0;
    g_pfnPackageFullNameFromId(&id, &reqLen, nullptr);

    // 2. Non-null buffer filled with sentinels, capacity 0
    {
        const UINT32 kBufSize = 256;
        WCHAR buf[kBufSize];
        wmemset(buf, 0xAAAA, kBufSize);
        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf);
        bool sentinelOk = true;
        for (UINT32 i = 0; i < kBufSize; ++i)
        {
            if (buf[i] != 0xAAAA)
            {
                sentinelOk = false;
                break;
            }
        }
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"sentinel_len_0";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = true;
        obs.sentinelIntact = sentinelOk;
        obs.notes = sentinelOk ? L"Buffer untouched" : L"Buffer overwritten!";
        recordObservation(obs);
    }

    // 3. Non-null buffer, capacity 1
    {
        const UINT32 kBufSize = 256;
        WCHAR buf[kBufSize];
        wmemset(buf, 0xAAAA, kBufSize);
        UINT32 inLen = 1;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf);
        bool sentinelOk = true;
        for (UINT32 i = 0; i < kBufSize; ++i)
        {
            if (buf[i] != 0xAAAA)
            {
                sentinelOk = false;
                break;
            }
        }
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"sentinel_len_1";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = true;
        obs.sentinelIntact = sentinelOk;
        obs.notes = sentinelOk ? L"Buffer untouched on insufficient buffer" : L"Buffer overwritten on insufficient buffer!";
        recordObservation(obs);
    }

    // 4. Non-null buffer, capacity reqLen - 1
    if (reqLen > 1)
    {
        const UINT32 kBufSize = 256;
        WCHAR buf[kBufSize];
        wmemset(buf, 0xAAAA, kBufSize);
        UINT32 inLen = reqLen - 1;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf);
        bool sentinelOk = true;
        for (UINT32 i = 0; i < kBufSize; ++i)
        {
            if (buf[i] != 0xAAAA)
            {
                sentinelOk = false;
                break;
            }
        }
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"sentinel_len_req_minus_1";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = true;
        obs.sentinelIntact = sentinelOk;
        obs.notes = sentinelOk ? L"All sentinels untouched" : L"Sentinels overwritten!";
        recordObservation(obs);
    }

    // 5. Non-null buffer, capacity reqLen (exact)
    {
        const UINT32 kBufSize = 256;
        WCHAR buf[kBufSize];
        wmemset(buf, 0xAAAA, kBufSize);
        UINT32 inLen = reqLen;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf);
        bool sentinelOk = true;
        for (UINT32 i = reqLen; i < kBufSize; ++i)
        {
            if (buf[i] != 0xAAAA)
            {
                sentinelOk = false;
                break;
            }
        }
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"exact_capacity";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.resultString = (ret == ERROR_SUCCESS) ? std::wstring(buf) : L"";
        obs.expectedString = L"TestPackage_1.2.3.4_x86_res_1234567890abc";
        obs.sentinelChecked = true;
        obs.sentinelIntact = sentinelOk;
        obs.notes = sentinelOk ? L"Exact fit sentinel preserved beyond capacity" : L"Sentinel overwritten beyond capacity!";
        recordObservation(obs);
    }

    // 6. Non-null buffer, capacity reqLen + 10 (extra)
    {
        const UINT32 kBufSize = 256;
        WCHAR buf[kBufSize];
        wmemset(buf, 0xAAAA, kBufSize);
        UINT32 inLen = reqLen + 10;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf);
        bool sentinelOk = true;
        for (UINT32 i = len; i < kBufSize; ++i)
        {
            if (buf[i] != 0xAAAA)
            {
                sentinelOk = false;
                break;
            }
        }
        Observation obs = {};
        obs.category = L"buffer_capacity";
        obs.testName = L"extra_capacity";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.resultString = (ret == ERROR_SUCCESS) ? std::wstring(buf) : L"";
        obs.expectedString = L"TestPackage_1.2.3.4_x86_res_1234567890abc";
        obs.sentinelChecked = true;
        obs.sentinelIntact = sentinelOk;
        obs.notes = (len == reqLen) ? L"Output length updated to exact chars written" : L"Output length retains input";
        recordObservation(obs);
    }
}

static void testResourceIds()
{
    PACKAGE_ID id = {0};
    id.name = (PWSTR)L"TestPackage";
    id.version.Major = 1;
    id.version.Minor = 0;
    id.version.Build = 0;
    id.version.Revision = 0;
    id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    id.publisherId = (PWSTR)L"1234567890abc";

    struct ResourceCase
    {
        const wchar_t *resId;
        const wchar_t *name;
        const wchar_t *expected;
        const wchar_t *notes;
    };
    ResourceCase cases[] = {
        { nullptr, L"resource_id_null", L"TestPackage_1.0.0.0_x86__1234567890abc", L"Resource ID is null" },
        { L"", L"resource_id_empty", L"TestPackage_1.0.0.0_x86__1234567890abc", L"Resource ID is empty string" },
        { L"res", L"resource_id_nonempty_res", L"TestPackage_1.0.0.0_x86_res_1234567890abc", L"Resource ID is 'res'" },
        { L"resourceid", L"resource_id_nonempty_resourceid", L"TestPackage_1.0.0.0_x86_resourceid_1234567890abc", L"Resource ID is 'resourceid'" }
    };

    for (const auto &c : cases)
    {
        id.resourceId = (PWSTR)c.resId;
        UINT32 len = 0;
        g_pfnPackageFullNameFromId(&id, &len, nullptr);
        std::vector<WCHAR> buf(len + 1, 0);
        UINT32 inLen = len;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf.data());

        Observation obs = {};
        obs.category = L"resource_id";
        obs.testName = c.name;
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.resultString = (ret == ERROR_SUCCESS) ? buf.data() : L"";
        obs.expectedString = c.expected;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = c.notes;
        recordObservation(obs);
    }
}

static void testPublisherDerivation()
{
    const wchar_t *publishers[] = {
        L"publisher",
        L"CN=Test",
        L"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US",
        L"AppX Test Publisher",
        L"MyPublisher123"
    };

    for (const wchar_t *pub : publishers)
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        id.resourceId = (PWSTR)L"res";
        id.publisher = (PWSTR)pub;
        id.publisherId = nullptr; // Derive!

        UINT32 len = 0;
        g_pfnPackageFullNameFromId(&id, &len, nullptr);
        std::vector<WCHAR> buf(len + 1, 0);
        UINT32 inLen = len;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf.data());

        std::wstring refPubId = derivePublisherIdRef(pub);
        std::wstring expected = std::wstring(L"TestPackage_1.0.0.0_x86_res_") + refPubId;

        Observation obs = {};
        obs.category = L"publisher_derivation";
        obs.testName = std::wstring(L"derive_") + pub;
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.resultString = (ret == ERROR_SUCCESS) ? buf.data() : L"";
        obs.expectedString = expected;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = std::wstring(L"Derived pubId: ") + refPubId;
        recordObservation(obs);
    }

    // Explicit publisherId tests
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        id.resourceId = (PWSTR)L"res";
        id.publisherId = (PWSTR)L"1234567890abc"; // 13 chars

        UINT32 len = 0;
        g_pfnPackageFullNameFromId(&id, &len, nullptr);
        std::vector<WCHAR> buf(len + 1, 0);
        UINT32 inLen = len;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, buf.data());

        Observation obs = {};
        obs.category = L"publisher_id";
        obs.testName = L"explicit_13_chars";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.resultString = (ret == ERROR_SUCCESS) ? buf.data() : L"";
        obs.expectedString = L"TestPackage_1.0.0.0_x86_res_1234567890abc";
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = L"Explicit 13-character publisher ID";
        recordObservation(obs);
    }

    // Explicit publisherId wrong lengths: 12 chars
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        id.resourceId = (PWSTR)L"res";
        id.publisherId = (PWSTR)L"1234567890ab"; // 12 chars

        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, nullptr);
        Observation obs = {};
        obs.category = L"publisher_id";
        obs.testName = L"explicit_12_chars";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Rejected 12 chars" : L"Unexpected return code";
        recordObservation(obs);
    }

    // Explicit publisherId wrong lengths: 14 chars
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        id.resourceId = (PWSTR)L"res";
        id.publisherId = (PWSTR)L"1234567890abcd"; // 14 chars

        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, nullptr);
        Observation obs = {};
        obs.category = L"publisher_id";
        obs.testName = L"explicit_14_chars";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Rejected 14 chars" : L"Unexpected return code";
        recordObservation(obs);
    }

    // Both publisher and publisherId NULL
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
        id.resourceId = (PWSTR)L"res";
        id.publisher = nullptr;
        id.publisherId = nullptr;

        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, nullptr);
        Observation obs = {};
        obs.category = L"publisher_id";
        obs.testName = L"both_publisher_and_id_null";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Rejected missing publisher" : L"Unexpected return code";
        recordObservation(obs);
    }
}

static void testArchitectures()
{
    struct ArchTest
    {
        UINT32 arch;
        const wchar_t *name;
        const wchar_t *archStr;
    };
    ArchTest tests[] = {
        { PROCESSOR_ARCHITECTURE_INTEL, L"arch_intel", L"x86" },
        { PROCESSOR_ARCHITECTURE_AMD64, L"arch_amd64", L"x64" },
        { PROCESSOR_ARCHITECTURE_ARM, L"arch_arm", L"arm" },
        { PROCESSOR_ARCHITECTURE_ARM64, L"arch_arm64", L"arm64" },
        { PROCESSOR_ARCHITECTURE_NEUTRAL, L"arch_neutral", L"neutral" },
        { 9999, L"arch_invalid", L"" }
    };

    for (const auto &t : tests)
    {
        PACKAGE_ID id = {0};
        id.name = (PWSTR)L"TestPackage";
        id.version.Major = 1;
        id.version.Minor = 0;
        id.version.Build = 0;
        id.version.Revision = 0;
        id.processorArchitecture = t.arch;
        id.resourceId = (PWSTR)L"res";
        id.publisherId = (PWSTR)L"1234567890abc";

        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&id, &len, nullptr);
        if (ret == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<WCHAR> buf(len + 1, 0);
            UINT32 callInLen = len;
            ret = g_pfnPackageFullNameFromId(&id, &len, buf.data());
            std::wstring expected = std::wstring(L"TestPackage_1.0.0.0_") + t.archStr + L"_res_1234567890abc";
            Observation obs = {};
            obs.category = L"architecture";
            obs.testName = t.name;
            obs.returnCode = ret;
            obs.hasLengthPointer = true;
            obs.inLength = callInLen;
            obs.outLength = len;
            obs.resultString = (ret == ERROR_SUCCESS) ? buf.data() : L"";
            obs.expectedString = expected;
            obs.sentinelChecked = false;
            obs.sentinelIntact = false;
            obs.notes = L"Architecture mapping valid";
            recordObservation(obs);
        }
        else
        {
            Observation obs = {};
            obs.category = L"architecture";
            obs.testName = t.name;
            obs.returnCode = ret;
            obs.hasLengthPointer = true;
            obs.inLength = inLen;
            obs.outLength = len;
            obs.sentinelChecked = false;
            obs.sentinelIntact = false;
            obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Invalid arch correctly rejected" : L"Unexpected ret";
            recordObservation(obs);
        }
    }
}

static void testParameters()
{
    PACKAGE_ID id = {0};
    id.name = (PWSTR)L"TestPackage";
    id.version.Major = 1;
    id.version.Minor = 0;
    id.version.Build = 0;
    id.version.Revision = 0;
    id.processorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
    id.resourceId = (PWSTR)L"res";
    id.publisherId = (PWSTR)L"1234567890abc";

    // 1. id == nullptr
    {
        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(nullptr, &len, nullptr);
        Observation obs = {};
        obs.category = L"parameters";
        obs.testName = L"null_id";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Expected invalid parameter" : L"Unexpected ret";
        recordObservation(obs);
    }

    // 2. length == nullptr
    {
        WCHAR buf[64] = {0};
        LONG ret = g_pfnPackageFullNameFromId(&id, nullptr, buf);
        Observation obs = {};
        obs.category = L"parameters";
        obs.testName = L"null_length";
        obs.returnCode = ret;
        obs.hasLengthPointer = false; // Missing pointer: represented as explicit "NA"
        obs.inLength = 0;
        obs.outLength = 0;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Expected invalid parameter" : L"Unexpected ret";
        recordObservation(obs);
    }

    // 3. name == nullptr
    {
        PACKAGE_ID idNullName = id;
        idNullName.name = nullptr;
        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&idNullName, &len, nullptr);
        Observation obs = {};
        obs.category = L"parameters";
        obs.testName = L"null_name";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Expected invalid parameter" : L"Unexpected ret";
        recordObservation(obs);
    }

    // 4. name == L"" (empty)
    {
        PACKAGE_ID idEmptyName = id;
        idEmptyName.name = (PWSTR)L"";
        UINT32 inLen = 0;
        UINT32 len = inLen;
        LONG ret = g_pfnPackageFullNameFromId(&idEmptyName, &len, nullptr);
        Observation obs = {};
        obs.category = L"parameters";
        obs.testName = L"empty_name";
        obs.returnCode = ret;
        obs.hasLengthPointer = true;
        obs.inLength = inLen;
        obs.outLength = len;
        obs.sentinelChecked = false;
        obs.sentinelIntact = false;
        obs.notes = (ret == ERROR_INVALID_PARAMETER) ? L"Expected invalid parameter" : L"Unexpected ret";
        recordObservation(obs);
    }
}

int main()
{
    HMODULE hMod = LoadLibraryW(L"kernelbase.dll");
    if (!hMod)
        hMod = LoadLibraryW(L"kernel32.dll");
    if (!hMod)
    {
        std::fprintf(stderr, "Failed to load kernelbase.dll or kernel32.dll\n");
        return 1;
    }

    g_pfnPackageFullNameFromId = (pfnPackageFullNameFromId)GetProcAddress(hMod, "PackageFullNameFromId");
    if (!g_pfnPackageFullNameFromId)
    {
        std::fprintf(stderr, "PackageFullNameFromId not found\n");
        return 2;
    }

    testBufferCapacities();
    testResourceIds();
    testPublisherDerivation();
    testArchitectures();
    testParameters();

    if (!g_output.empty())
    {
        std::string narrow;
        narrow.reserve(g_output.size());
        for (wchar_t c : g_output)
            narrow += static_cast<char>(c);
        std::fwrite(narrow.data(), 1, narrow.size(), stdout);
        std::fflush(stdout);
    }

    return 0;
}
