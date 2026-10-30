// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Test child that requests administrator rights in its manifest and exits with 100 + its token elevation type
// (TokenElevationTypeDefault 1, Full 2, Limited 3).
#include <windows.h>
int main()
{
    HANDLE token;
    TOKEN_ELEVATION_TYPE type = (TOKEN_ELEVATION_TYPE)0;
    DWORD size;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 2;
    if (!GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size)) return 3;
    return 100 + (int)type;
}
