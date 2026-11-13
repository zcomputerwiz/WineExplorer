/*
 * WineExplorer U1.3 framework package DLL
 *
 * Copyright 2026 WineExplorer contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* the library of the framework package; a dependent package loads it by name without knowing where it is */
extern "C" __declspec(dllexport) int U1FrameworkAnswer(void)
{
    return 42;
}
