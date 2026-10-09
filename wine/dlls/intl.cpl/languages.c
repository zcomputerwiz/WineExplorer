/*
 * Regional and Language Options - Languages page
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
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

#include "intl.h"

/* On Linux the keyboard layouts (text services and input languages) and the fonts for complex scripts and East Asian
 * languages come from the system, so this page only shows them: the controls are there, grayed. */
INT_PTR CALLBACK
LanguagesPageProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_INITDIALOG)
    {
        EnableWindow(GetDlgItem(hwndDlg, IDC_DETAIL_BUTTON), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_INST_FILES_FOR_RTOL_LANG), FALSE);
        EnableWindow(GetDlgItem(hwndDlg, IDC_INST_FILES_FOR_ASIAN), FALSE);
    }
    return FALSE;
}
