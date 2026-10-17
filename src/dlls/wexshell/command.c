/*
 * explorer.exe
 *
 * Copyright 2004 CodeWeavers, Mike Hearn
 * Copyright 2005,2006 CodeWeavers, Aric Stewart
 * Copyright 2011 Jay Yang
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
 */

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "wexshell.h"
#include "wexbrowse.h"
#include "wextheme.h"
#include "wexpower.h"
#include "wexnet.h"
#include "wexwlan.h"
#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(explorer);

typedef struct parametersTAG {
    BOOL    explorer_mode;
    WCHAR   root[MAX_PATH];
    WCHAR   selection[MAX_PATH];
} parameters_struct;

static WCHAR *copy_path_string(WCHAR *target, WCHAR *source)
{
    INT i = 0;

    while (iswspace(*source)) source++;

    if (*source == '\"')
    {
        source ++;
        while (*source && *source != '\"') target[i++] = *source++;
        target[i] = 0;
        if (*source) source++;
    }
    else
    {
        while (*source && *source != ',') target[i++] = *source++;
        target[i] = 0;
    }
    PathRemoveBackslashW(target);
    return source;
}


static void copy_path_root(LPWSTR root, LPWSTR path)
{
    LPWSTR p,p2;
    INT i = 0;

    p = path;
    while (*p!=0)
        p++;

    while (*p!='\\' && p > path)
        p--;

    if (p == path)
        return;

    p2 = path;
    while (p2 != p)
    {
        root[i] = *p2;
        i++;
        p2++;
    }
    root[i] = 0;
}

/*
 * Command Line parameters are:
 * [/n]  Opens in single-paned view for each selected items. This is default
 * [/e,] Uses Windows Explorer View
 * [/cd,object] Specifies the root level of the view
 * [/root,object] Specifies the root level of the view
 * [/select,object] parent folder is opened and specified object is selected
 */
static void parse_command_line(LPWSTR commandline,parameters_struct *parameters)
{
    static const WCHAR arg_n[] = L"/n";
    static const WCHAR arg_e[] = L"/e,";
    static const WCHAR arg_cd[] = L"/cd,";
    static const WCHAR arg_root[] = L"/root,";
    static const WCHAR arg_select[] = L"/select,";
    static const WCHAR arg_desktop[] = L"/desktop";
    static const WCHAR arg_desktop_quotes[] = L"\"/desktop";
    const size_t len_n = wcslen(arg_n);
    const size_t len_e = wcslen(arg_e);
    const size_t len_cd = wcslen(arg_cd);
    const size_t len_root = wcslen(arg_root);
    const size_t len_select = wcslen(arg_select);
    const size_t len_desktop = wcslen(arg_desktop);
    const size_t len_desktop_quotes = wcslen(arg_desktop_quotes);

    LPWSTR p = commandline;

    while (*p)
    {
        while (iswspace(*p)) p++;
        if (wcsnicmp(p, arg_n, len_n )==0)
        {
            parameters->explorer_mode = FALSE;
            p += len_n;
            if (*p == ',') p++;
        }
        else if (wcsnicmp(p, arg_e, len_e )==0)
        {
            parameters->explorer_mode = TRUE;
            p += len_e;
        }
        else if (wcsnicmp(p, L"/e", 2) == 0 && (p[2] == 0 || iswspace(p[2])))
        {
            parameters->explorer_mode = TRUE;
            p += 2;
        }
        else if (wcsnicmp(p, arg_cd, len_cd )==0)
        {
            p += len_cd;
            p = copy_path_string(parameters->root,p);
        }
        else if (wcsnicmp(p, arg_root, len_root )==0)
        {
            p += len_root;
            p = copy_path_string(parameters->root,p);
        }
        else if (wcsnicmp(p, arg_select, len_select )==0)
        {
            p += len_select;
            p = copy_path_string(parameters->selection,p);
            if (!parameters->root[0])
                copy_path_root(parameters->root,
                               parameters->selection);
        }
        else if (wcsnicmp(p, L"/theme", 6) == 0 && (p[6] == ' ' || p[6] == '=' || p[6] == ':'))
        {
            /* explorer /theme <file.theme>: apply a Windows theme file (visual style, colors, sounds, icons) */
            WCHAR file[MAX_PATH];
            p += 7;
            while (*p == ' ') p++;
            copy_path_string(file, p);
            theme_apply_file(file);
            ExitProcess(0);
        }
        else if (wcsnicmp(p, L"/turnoff", 8) == 0 || wcsnicmp(p, L"-turnoff", 8) == 0)
        {
            /* the Welcome screen's Turn off computer (logonui.exe) */
            power_mgmt_show_shutdown_dialog(NULL);
            ExitProcess(0);
        }
        else if (wcsnicmp(p, L"/wlan", 5) == 0 || wcsnicmp(p, L"-wlan", 5) == 0)
        {
            wlan_dialog_show(NULL);
            ExitProcess(0);
        }
        else if (wcsnicmp(p, L"/netstatus", 10) == 0 || wcsnicmp(p, L"-netstatus", 10) == 0)
        {
            wlan_status_show(NULL);
            ExitProcess(0);
        }
        else if (wcsnicmp(p, L"/netprops", 9) == 0 || wcsnicmp(p, L"-netprops", 9) == 0 ||
                 wcsnicmp(p, L"/lanprops", 9) == 0 || wcsnicmp(p, L"-lanprops", 9) == 0)
        {
            net_props_show(NULL);
            ExitProcess(0);
        }
        else
        {
            /* left over command line is generally the path to be opened */
            copy_path_string(parameters->root,p);
            break;
        }
    }
}

/* Keep the browser loaded while its window classes/COM callbacks are live. */
static INIT_ONCE browser_once = INIT_ONCE_STATIC_INIT;
static HMODULE browser_module;
static BOOL CALLBACK load_browser(INIT_ONCE *once, void *parameter, void **context)
{
    browser_module = LoadLibraryW(L"wexbrowse.dll");
    return TRUE;
}
static BOOL open_browser(const WCHAR *root, const WCHAR *selection, BOOL mode, int show)
{
    BOOL (WINAPI *open)(const WCHAR *, const WCHAR *, BOOL, int);
    InitOnceExecuteOnce(&browser_once, load_browser, NULL, NULL);
    open = browser_module ? (void *)GetProcAddress(browser_module, "WexBrowseOpen") : NULL;
    return open ? open(root, selection, mode, show) : FALSE;
}

BOOL WINAPI WexShellCommand(DWORD version, WCHAR *cmdline, int cmdshow)
{
    WCHAR *p;
    if (version != WEX_SHELL_VERSION || !cmdline) return FALSE;
    for (p = cmdline; *p; p++)
    {
        if ((p == cmdline || iswspace(p[-1])) && !wcsnicmp(p, L"/shell", 6) &&
            (!p[6] || iswspace(p[6]))) return FALSE;
        if ((p == cmdline || iswspace(p[-1]) || p[-1] == ',' || p[-1] == '"') &&
            !wcsnicmp(p, L"/desktop", 8) &&
            (!p[8] || p[8] == '=' || p[8] == ',' || iswspace(p[8])))
            return FALSE;
    }

    parameters_struct   parameters;

    memset(&parameters,0,sizeof(parameters));
    parse_command_line(cmdline,&parameters);
    if(parameters.root[0] && !PathIsDirectoryW(parameters.root))
        if(ShellExecuteW(NULL,NULL,parameters.root,NULL,NULL,SW_SHOWDEFAULT) > (HINSTANCE)32)
            ExitProcess(EXIT_SUCCESS);
    return open_browser(parameters.root, parameters.selection, parameters.explorer_mode, cmdshow);
}
