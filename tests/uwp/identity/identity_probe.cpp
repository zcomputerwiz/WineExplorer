/*
 * WineExplorer U1-M2 unpackaged package identity observation probe
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

/*
 * Observes how the plain Win32 package-identity ABI reports the identity of
 * this (expectedly unpackaged) process, through every self-owned handle form:
 *
 *   - the current-process queries (full / family / path),
 *   - the explicit self process through the pseudo-handle (full / family),
 *   - the explicit self process through an opened self handle (full / family),
 *   - the opened current process token (full / family),
 *   - the optional native RtlQueryPackageIdentity (NTSTATUS domain).
 *
 * It targets only this process and its own token. It uses valid, bounded
 * buffers and the two-call length-then-value pattern. It records the status,
 * the input/output lengths and any handle-open error for every case. It never
 * performs invalid-pointer or crash experiments and never targets unrelated
 * processes or tokens.
 *
 * Output is one machine-readable JSON record per line (JSONL) on stdout. Each
 * record carries the schema version, architecture, API, handle form, the
 * status domain (Win32 LONG vs NTSTATUS, never conflated), the status in
 * decimal and hex, the input/output lengths, the observed result and a reason
 * for any NOT-RUN case. No package name, path, token credential or user
 * identifier is printed: an unexpectedly packaged process is reported by
 * context only, and the probe stops without printing its identity.
 *
 * Exit status: 0 = completed (every case observed as no-package or an explicit
 * NOT-RUN), 3 = the process unexpectedly has package identity (stopped early),
 * 1 = the output could not be written.
 */

#include <windows.h>
#include <appmodel.h>

#include <cstdio>
#include <cwchar>
#include <functional>
#include <string>
#include <vector>

/* Matches the Windows SDK and Wine (wine/include/winerror.h). */
#ifndef APPMODEL_ERROR_NO_PACKAGE
#define APPMODEL_ERROR_NO_PACKAGE 15700
#endif

/*
 * RtlQueryPackageIdentity is an explicit NOT-RUN in this probe, never a
 * silent pass. The installed Windows SDK 10.0.26100.0 does not declare it
 * (there is no ntintsafe.h, and a search of every installed SDK header on
 * 2026-11-11 found no prototype), so there is no local declaration to verify
 * against and no call is made.
 *
 * This flag is false and the probe carries no implementation of the case:
 * setting it to true does not enable a call, and no prototype or
 * PackageIdentityInfo ABI is assumed here. A real call is a separate future
 * order; for reference, the Wine ntdll declaration it would have to match is
 * RtlQueryPackageIdentity(token, fullname, fullname_size, appid, appid_size,
 * packaged) returning NTSTATUS (wine/include/winternl.h,
 * wine/dlls/ntdll/rtl.c).
 */
static const bool RtlQueryPackageIdentityDeclarationVerified = false;

static const wchar_t *const kSchema = L"wex-uwp-identity/1";

#if defined(_M_X64)
static const wchar_t *const kArch = L"x64";
#elif defined(_M_IX86)
static const wchar_t *const kArch = L"x86";
#else
static const wchar_t *const kArch = L"unknown";
#endif

/* Bounded buffer for identity strings, in characters. Package identifiers are
 * far shorter; this bound only guards the allocation, it is not an expectation
 * about any observed value. */
static const UINT32 kMaxBufferChars = 32768;

enum class Result
{
    NoPackage,
    UnexpectedPackaged,
    NotRun
};

struct Record
{
    std::wstring api;
    std::wstring handle;
    bool ntStatus; /* true = NTSTATUS domain, false = Win32 LONG domain */
    LONG status;
    DWORD inLen;
    DWORD outLen;
    Result result;
    std::wstring reason;
};

using StringQueryFn = std::function<LONG(UINT32 *, PWSTR)>;

static std::wstring jsonEscape(std::wstring const &s)
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
            if (static_cast<unsigned short>(c) < 0x20)
            {
                wchar_t buf[8];
                std::swprintf(buf, 8, L"\\u%04x",
                              static_cast<unsigned short>(c));
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

static std::wstring resultName(Result r)
{
    switch (r)
    {
    case Result::NoPackage: return L"no_package";
    case Result::UnexpectedPackaged: return L"unexpected_packaged";
    case Result::NotRun: return L"not_run";
    }
    return L"unknown";
}

static std::wstring recordLine(Record const &r)
{
    std::wstring line;
    line += L"{";
    line += L"\"schema\":\""; line += jsonEscape(kSchema); line += L"\",";
    line += L"\"arch\":\""; line += jsonEscape(kArch); line += L"\",";
    line += L"\"api\":\""; line += jsonEscape(r.api); line += L"\",";
    line += L"\"handle\":\""; line += jsonEscape(r.handle); line += L"\",";
    line += L"\"status_domain\":\"";
    line += (r.ntStatus ? L"ntstatus" : L"win32");
    line += L"\",";
    line += L"\"status_dec\":"; line += std::to_wstring(r.status); line += L",";
    line += L"\"status_hex\":\"0x";
    wchar_t hex[16];
    std::swprintf(hex, 16, L"%08lX",
                  static_cast<unsigned long>(static_cast<unsigned int>(r.status)));
    line += hex;
    line += L"\",";
    line += L"\"in_len\":"; line += std::to_wstring(r.inLen); line += L",";
    line += L"\"out_len\":"; line += std::to_wstring(r.outLen); line += L",";
    line += L"\"result\":\""; line += resultName(r.result); line += L"\",";
    line += L"\"reason\":\""; line += jsonEscape(r.reason); line += L"\"";
    line += L"}";
    return line;
}

/*
 * Two-call bounded query. The first call passes a NULL buffer to learn the
 * required length; the second allocates a bounded buffer and reads the value.
 * Returns the final status. Sets inLen (buffer size used in the value call),
 * outLen (length reported by the API) and gotValue (true only when the API
 * returned a value). Never reads past the bounded buffer and never passes an
 * invalid pointer.
 */
static LONG boundedQuery(StringQueryFn fn, DWORD &inLen, DWORD &outLen, bool &gotValue)
{
    inLen = 0;
    outLen = 0;
    gotValue = false;
    UINT32 length = 0;
    LONG status = fn(&length, nullptr);
    if (status != ERROR_INSUFFICIENT_BUFFER)
        return status;
    outLen = length;
    if (length == 0 || length > kMaxBufferChars)
        return status;
    std::vector<wchar_t> buffer(length);
    inLen = length;
    status = fn(&length, buffer.data());
    outLen = length;
    if (status == ERROR_SUCCESS)
        gotValue = true;
    return status;
}

/*
 * Classify an observed query without inventing expectations for unobserved
 * error paths: a returned value means the process is (unexpectedly) packaged;
 * APPMODEL_ERROR_NO_PACKAGE means no package identity; anything else is
 * recorded honestly as NOT-RUN with a reason naming the situation.
 */
static Result classify(LONG status, bool gotValue, std::wstring &reason)
{
    reason.clear();
    if (gotValue)
        return Result::UnexpectedPackaged;
    if (status == APPMODEL_ERROR_NO_PACKAGE)
        return Result::NoPackage;
    reason = (status == ERROR_INSUFFICIENT_BUFFER) ? L"insufficient_buffer"
                                                   : L"unobserved_status";
    return Result::NotRun;
}

int main(void)
{
    std::wstring output;
    bool stop = false;

    auto emit = [&](Record const &r)
    {
        output += recordLine(r);
        output += L"\n";
    };

    auto runStringCase = [&](std::wstring const &api, std::wstring const &handle,
                             StringQueryFn fn)
    {
        if (stop)
            return;
        DWORD inLen = 0, outLen = 0;
        bool got = false;
        LONG status = boundedQuery(fn, inLen, outLen, got);
        std::wstring reason;
        Result result = classify(status, got, reason);
        emit(Record{ api, handle, false, status, inLen, outLen, result, reason });
        if (result == Result::UnexpectedPackaged)
            stop = true;
    };

    auto emitNotRun = [&](std::wstring const &api, std::wstring const &handle,
                          LONG status, std::wstring const &reason)
    {
        emit(Record{ api, handle, false, status, 0, 0, Result::NotRun, reason });
    };

    /* 1. Current-process identity: full, family, path. */
    runStringCase(L"GetCurrentPackageFullName", L"current", GetCurrentPackageFullName);
    runStringCase(L"GetCurrentPackageFamilyName", L"current", GetCurrentPackageFamilyName);
    runStringCase(L"GetCurrentPackagePath", L"current", GetCurrentPackagePath);

    /* 2. Explicit self process: pseudo-handle, then an opened self handle. */
    if (!stop)
    {
        runStringCase(L"GetPackageFullName", L"process_pseudo",
                      [](UINT32 *n, PWSTR p)
                      { return GetPackageFullName(GetCurrentProcess(), n, p); });
        runStringCase(L"GetPackageFamilyName", L"process_pseudo",
                      [](UINT32 *n, PWSTR p)
                      { return GetPackageFamilyName(GetCurrentProcess(), n, p); });

        HANDLE hSelf = OpenProcess(PROCESS_QUERY_INFORMATION |
                                       PROCESS_QUERY_LIMITED_INFORMATION,
                                   FALSE, GetCurrentProcessId());
        if (hSelf)
        {
            runStringCase(L"GetPackageFullName", L"process_opened",
                          [hSelf](UINT32 *n, PWSTR p)
                          { return GetPackageFullName(hSelf, n, p); });
            runStringCase(L"GetPackageFamilyName", L"process_opened",
                          [hSelf](UINT32 *n, PWSTR p)
                          { return GetPackageFamilyName(hSelf, n, p); });
            CloseHandle(hSelf);
        }
        else
        {
            DWORD err = GetLastError();
            emitNotRun(L"GetPackageFullName", L"process_opened",
                       static_cast<LONG>(err), L"handle_open_failed");
            emitNotRun(L"GetPackageFamilyName", L"process_opened",
                       static_cast<LONG>(err), L"handle_open_failed");
        }
    }

    /* 3. Opened current process token: full, family. */
    if (!stop)
    {
        HANDLE hToken = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        {
            runStringCase(L"GetPackageFullNameFromToken", L"token_opened",
                          [hToken](UINT32 *n, PWSTR p)
                          { return GetPackageFullNameFromToken(hToken, n, p); });
            runStringCase(L"GetPackageFamilyNameFromToken", L"token_opened",
                          [hToken](UINT32 *n, PWSTR p)
                          { return GetPackageFamilyNameFromToken(hToken, n, p); });
            CloseHandle(hToken);
        }
        else
        {
            DWORD err = GetLastError();
            emitNotRun(L"GetPackageFullNameFromToken", L"token_opened",
                       static_cast<LONG>(err), L"handle_open_failed");
            emitNotRun(L"GetPackageFamilyNameFromToken", L"token_opened",
                       static_cast<LONG>(err), L"handle_open_failed");
        }
    }

    /* 4. Optional native RtlQueryPackageIdentity (NTSTATUS domain). */
    if (!stop)
    {
        if (!RtlQueryPackageIdentityDeclarationVerified)
        {
            emit(Record{ L"RtlQueryPackageIdentity", L"token_opened", true, 0,
                         0, 0, Result::NotRun, L"declaration_unverified" });
        }
        else
        {
            /*
             * Unreachable while the flag is false, and there is no
             * implementation here to reach: this probe does not call
             * RtlQueryPackageIdentity. A real call is a separate future
             * order (see the flag comment above); it must first verify the
             * local declaration before any prototype is assumed.
             */
        }
    }

    if (!output.empty())
    {
        /* The JSON is pure ASCII (fixed literals and numbers); convert the
         * wide buffer to narrow for the console write. */
        std::string narrow;
        narrow.reserve(output.size());
        for (wchar_t c : output)
            narrow += static_cast<char>(c);
        if (std::fwrite(narrow.data(), 1, narrow.size(), stdout) != narrow.size())
            return 1;
    }
    std::fflush(stdout);
    return stop ? 3 : 0;
}
