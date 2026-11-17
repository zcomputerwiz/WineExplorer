/*
 * secpol: the Local Security Settings editor (secpol.msc), hosted by mmc.exe
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * docs/GROUP_POLICY.md, "The local security settings editor". A two-pane window as the one of gpedit: the tree of
 * Security Settings (Account Policies, Local Policies) and the settings of the chosen folder with their value; a
 * setting's Properties are the "Local Security Setting" page and an Explain page. The settings are the table of
 * settings.c. The value shown is the one in the local object's template (GroupPolicy\Machine\Microsoft\Windows
 * NT\SecEdit\GptTmpl.inf, readable by anyone) or, when the template has none, the one the system keeps now. A change
 * edits the template's text (template.c keeps every other line) and hands the file to gpsvc through the spool
 * (wexgpclient.h, as secedit /configure does), which needs a process of the system group: without administrator
 * rights the window shows the settings and offers to open again elevated when a change is made.
 */

#define SNDMSG SendMessageW   /* Wine's headers want the W form named; the list and tree macros send through this */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#include <shellapi.h>

#include "resource.h"
#include "wexgpclient.h"
#include "settings.h"
#include "template.h"

static HINSTANCE instance;
static HWND frame, tree, list, status;
static int split = 230;
static int icon_folder, icon_open;   /* the shell's folder icons in the system image list */
static struct tmpl *tmpl;            /* the local object's template as it was read */
static BOOL load_failed;             /* a template is there and cannot be read: a save would drop it, so it is refused */
static BOOL quiet;                   /* no message boxes: SecPol_SetW is for scripts */

#define MAX_FOLDERS 16

/* the folders of the tree, parents before their children; the settings name theirs in "node" */
static const char * const folders[] =
{
    "Account Policies",
    "Account Policies\\Password Policy",
    "Account Policies\\Account Lockout Policy",
    "Local Policies",
    "Local Policies\\Audit Policy",
    "Local Policies\\User Rights Assignment",
    "Local Policies\\Security Options",
};

struct node
{
    const char *path;    /* NULL for the root */
};

static struct node nodes[MAX_FOLDERS + 1];
static unsigned int node_count;

static void to_wide( const char *text, WCHAR *out, int count )
{
    if (!MultiByteToWideChar( CP_UTF8, 0, text, -1, out, count )) out[0] = 0;
}

/* ---------------------------------------------------------------------------------------------------- the template */

static BOOL template_path( WCHAR *path, DWORD chars )
{
    static const WCHAR tail[] = L"\\GroupPolicy\\Machine\\Microsoft\\Windows NT\\SecEdit\\GptTmpl.inf";
    UINT n = GetSystemDirectoryW( path, chars );

    if (!n || n + ARRAY_SIZE(tail) > chars) return FALSE;
    wcscpy( path + n, tail );
    return TRUE;
}

/* the text of the file as UTF-8, malloc'd; UTF-16 with a byte order mark, else the ANSI code page */
static char *bytes_to_utf8( const BYTE *bytes, DWORD size, size_t *length )
{
    WCHAR *wide;
    char *text;
    int n, wide_chars;

    if (size >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe)
    {
        wide_chars = (size - 2) / sizeof(WCHAR);
        if (!(wide = malloc( (wide_chars + 1) * sizeof(WCHAR) ))) return NULL;
        memcpy( wide, bytes + 2, wide_chars * sizeof(WCHAR) );
    }
    else
    {
        wide_chars = MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, size, NULL, 0 );
        if (!(wide = malloc( (wide_chars + 1) * sizeof(WCHAR) ))) return NULL;
        if (wide_chars) MultiByteToWideChar( CP_ACP, 0, (const char *)bytes, size, wide, wide_chars );
    }
    wide[wide_chars] = 0;
    n = wide_chars ? WideCharToMultiByte( CP_UTF8, 0, wide, wide_chars, NULL, 0, NULL, NULL ) : 0;
    if (!(text = malloc( n + 1 ))) { free( wide ); return NULL; }
    if (n) WideCharToMultiByte( CP_UTF8, 0, wide, wide_chars, text, n, NULL, NULL );
    text[n] = 0;
    free( wide );
    *length = n;
    return text;
}

/* the template of the local object into tmpl; no file is an empty template, a file that is there and cannot be read
 * is reported and leaves load_failed set (nothing is then written over it) */
static void load_template(void)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD size, done;
    BYTE *bytes;
    char *text;
    size_t length;
    int ret = 0;

    tmpl_free( tmpl );
    tmpl = tmpl_new();
    load_failed = FALSE;
    if (!template_path( path, ARRAY_SIZE(path) )) return;
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        ret = err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    else
    {
        size = GetFileSize( file, NULL );
        if (size == INVALID_FILE_SIZE || size > WEX_GP_TEMPLATE_LIMIT) ret = -1;
        else if (size)
        {
            if (!(bytes = malloc( size ))) ret = -1;
            else
            {
                if (ReadFile( file, bytes, size, &done, NULL ) && done == size)
                {
                    if (!(text = bytes_to_utf8( bytes, size, &length ))) ret = -1;
                    else
                    {
                        if (tmpl_load( tmpl, text, length )) ret = -1;
                        free( text );
                    }
                }
                else ret = -1;
                free( bytes );
            }
        }
        CloseHandle( file );
    }
    if (ret)
    {
        load_failed = TRUE;
        tmpl_free( tmpl );
        tmpl = tmpl_new();
        if (!quiet)
            MessageBoxW( frame, L"The local security template cannot be read. It is left as it is: changes are not saved.",
                         L"Local Security Settings", MB_OK | MB_ICONWARNING );
    }
}

/* ---------------------------------------------------------------------------------------------------- the values */

static BOOL read_registry( const struct sec_setting *s, uint32_t *number )
{
    HKEY key;
    DWORD value, size = sizeof(value), type;
    LONG err;

    if (!s->regkey) return FALSE;
    {
        WCHAR path[256], name[64];

        to_wide( s->regkey, path, ARRAY_SIZE(path) );
        to_wide( s->regvalue, name, ARRAY_SIZE(name) );
        if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key )) return FALSE;
        err = RegQueryValueExW( key, name, NULL, &type, (BYTE *)&value, &size );
        RegCloseKey( key );
    }
    if (err || type != REG_DWORD) return FALSE;
    *number = sec_from_registry( s, value );
    return TRUE;
}

/* the setting's value: from the template, else the system's; defined says whether either has one (a number has its
 * default when neither does) */
static BOOL current_value( const struct sec_setting *s, uint32_t *number, BOOL *defined )
{
    char text[64];
    int ok;

    *defined = TRUE;
    if (tmpl && tmpl_get( tmpl, s->section, s->key, text, sizeof(text) ))
    {
        *number = sec_from_template( s, text, &ok );
        if (ok) return TRUE;
    }
    if (read_registry( s, number )) return TRUE;
    *number = s->def;
    *defined = s->kind == SEC_NUMBER;
    return *defined;
}

static BOOL lockout_off(void)
{
    uint32_t number;
    BOOL defined, found = FALSE;
    unsigned int i;

    for (i = 0; i < sec_setting_count; i++)
    {
        if (!sec_settings[i].key || strcmp( sec_settings[i].key, "LockoutBadCount" )) continue;
        found = current_value( &sec_settings[i], &number, &defined );
        break;
    }
    return found && !number;
}

static void value_text( const struct sec_setting *s, WCHAR *text, int chars )
{
    uint32_t number;
    BOOL defined;
    const struct sec_choice *c;
    WCHAR unit[80], word[200];

    if (s->reason) { lstrcpynW( text, L"Not applicable", chars ); return; }
    if (!current_value( s, &number, &defined )) { lstrcpynW( text, L"Not Defined", chars ); return; }
    switch (s->kind)
    {
    case SEC_FLAG:
        lstrcpynW( text, number ? L"Enabled" : L"Disabled", chars );
        break;
    case SEC_CHOICE:
        swprintf( text, chars, L"%lu", (unsigned long)number );
        for (c = s->choices; c && c->text; c++)
            if (c->value == number) { to_wide( c->text, word, ARRAY_SIZE(word) ); lstrcpynW( text, word, chars ); }
        break;
    default:
        /* the duration and the reset time mean nothing while no account is ever locked out */
        if (s->key && strcmp( s->key, "LockoutBadCount" ) && lockout_off()) { lstrcpynW( text, L"Not applicable", chars ); return; }
        /* a zero that means something of its own: the lockout duration says so, the threshold shows its number too */
        if (!number && s->zero_text && s->forever) { to_wide( s->zero_text, word, ARRAY_SIZE(word) ); lstrcpynW( text, word, chars ); return; }
        to_wide( s->unit, unit, ARRAY_SIZE(unit) );
        swprintf( text, chars, L"%lu %ls", (unsigned long)number, unit );
    }
}

static void support_text( const struct sec_setting *s, WCHAR *text, int chars )
{
    WCHAR reason[300];

    if (!s->reason) { lstrcpynW( text, L"This setting is acted on by this system.", chars ); return; }
    to_wide( s->reason, reason, ARRAY_SIZE(reason) );
    swprintf( text, chars, L"Not applicable: %ls.", reason );
}

/* ---------------------------------------------------------------------------------------------------- saving */

static void offer_elevation( HWND owner )
{
    if (MessageBoxW( owner, L"Changing the local security settings needs the rights of an administrator.\n\n"
                            L"Open Local Security Settings again as an administrator?", L"Local Security Settings",
                     MB_YESNO | MB_ICONQUESTION ) != IDYES)
        return;
    if ((INT_PTR)ShellExecuteW( owner, L"runas", L"mmc.exe", L"secpol.msc", NULL, SW_SHOWNORMAL ) > 32) DestroyWindow( frame );
}

static void report_error( HWND owner, DWORD err, const WCHAR *message )
{
    WCHAR text[600], system[300];

    if (err == ERROR_ACCESS_DENIED) { offer_elevation( owner ); return; }
    if (err == ERROR_INVALID_DATA && message[0]) swprintf( text, ARRAY_SIZE(text), L"The change was not accepted.\n\n%ls", message );
    else if (err == ERROR_TIMEOUT)
        lstrcpynW( text, L"The change was not taken, or it did not take effect in time: the policy service is not "
                         L"running, or the local policy store is not set up for it.", ARRAY_SIZE(text) );
    else
    {
        if (!FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, system, ARRAY_SIZE(system), NULL ))
            swprintf( system, ARRAY_SIZE(system), L"Error %lu.", (unsigned long)err );
        swprintf( text, ARRAY_SIZE(text), L"The change could not be saved.\n\n%ls", system );
    }
    MessageBoxW( owner, text, L"Local Security Settings", MB_OK | MB_ICONERROR );
}

/* the template in tmpl goes to gpsvc, which waits for the refresh to end; the template is read again either way */
static DWORD commit_template( WCHAR *message, DWORD chars )
{
    char *text;
    size_t length;
    WCHAR *wide;
    int n;
    DWORD err, bytes;

    message[0] = 0;
    if (load_failed)
    {
        lstrcpynW( message, L"The existing template cannot be read, so it is not replaced.", chars );
        return ERROR_INVALID_DATA;
    }
    if (tmpl_save( tmpl, &text, &length )) { load_template(); return ERROR_NOT_ENOUGH_MEMORY; }
    n = MultiByteToWideChar( CP_UTF8, 0, text, length, NULL, 0 );
    if (!(wide = malloc( (n + 1) * sizeof(WCHAR) ))) { free( text ); load_template(); return ERROR_NOT_ENOUGH_MEMORY; }
    wide[0] = 0xfeff;
    MultiByteToWideChar( CP_UTF8, 0, text, length, wide + 1, n );
    free( text );
    bytes = (n + 1) * sizeof(WCHAR);
    err = wex_gp_store_write( WEX_GP_MACHINE_TEMPLATE, wide, bytes, 120, message, chars, !quiet );
    free( wide );
    load_template();
    return err;
}

/* one setting into the template and out to the system */
static DWORD change_setting( HWND owner, const struct sec_setting *s, uint32_t number )
{
    WCHAR message[260];
    char value[40];
    HCURSOR old = SetCursor( LoadCursorW( NULL, (LPCWSTR)IDC_WAIT ) );
    DWORD err;

    load_template();
    sec_to_template( s, number, value, sizeof(value) );
    if (tmpl_set( tmpl, s->section, s->key, value )) err = ERROR_NOT_ENOUGH_MEMORY;
    else
    {
        /* (the window is disabled while the wait pumps its messages: no second change can start meanwhile) */
        if (frame) EnableWindow( frame, FALSE );
        err = commit_template( message, ARRAY_SIZE(message) );
        if (frame) EnableWindow( frame, TRUE );
    }
    SetCursor( old );
    if (err && owner) report_error( owner, err, message );
    else if (err && message[0])
    {
        /* without a window (SecPol_SetW): gpsvc's reason goes to the output for the script that called */
        char reason[304];
        DWORD done;

        WideCharToMultiByte( CP_UTF8, 0, message, -1, reason, sizeof(reason) - 1, NULL, NULL );
        reason[sizeof(reason) - 1] = 0;
        strcat( reason, "\n" );
        WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), reason, strlen( reason ), &done, NULL );
    }
    return err;
}

/* ---------------------------------------------------------------------------------------------------- Properties */

struct edit
{
    const struct sec_setting *setting;
    uint32_t number;
    BOOL changed;
};

static void update_zero_hint( HWND dlg, const struct edit *edit )
{
    WCHAR text[300], hint[300];
    WCHAR *end;
    unsigned long n;

    if (edit->setting->kind != SEC_NUMBER || !edit->setting->zero_text) return;
    GetDlgItemTextW( dlg, IDC_NUMBER, text, ARRAY_SIZE(text) );
    n = wcstoul( text, &end, 10 );
    if (text[0] && !*end && !n) to_wide( edit->setting->zero_text, hint, ARRAY_SIZE(hint) );
    else hint[0] = 0;
    SetDlgItemTextW( dlg, IDC_HINT, hint );
}

static void init_setting_page( HWND dlg, struct edit *edit )
{
    const struct sec_setting *s = edit->setting;
    WCHAR text[600], wide[300];
    BOOL usable = !s->reason;
    const struct sec_choice *c;
    int index = 0, i;

    to_wide( s->name, wide, ARRAY_SIZE(wide) );
    SetDlgItemTextW( dlg, IDC_NAME, wide );
    support_text( s, text, ARRAY_SIZE(text) );
    SetDlgItemTextW( dlg, IDC_SUPPORT, text );
    ShowWindow( GetDlgItem( dlg, IDC_LABEL ), s->kind == SEC_NUMBER ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_NUMBER ), s->kind == SEC_NUMBER ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_SPIN ), s->kind == SEC_NUMBER ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_UNIT ), s->kind == SEC_NUMBER ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_HINT ), s->kind == SEC_NUMBER ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_ENABLED ), s->kind == SEC_FLAG ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_DISABLED ), s->kind == SEC_FLAG ? SW_SHOW : SW_HIDE );
    ShowWindow( GetDlgItem( dlg, IDC_CHOICE ), s->kind == SEC_CHOICE ? SW_SHOW : SW_HIDE );
    if (s->kind == SEC_NUMBER)
    {
        to_wide( s->label ? s->label : "", wide, ARRAY_SIZE(wide) );
        SetDlgItemTextW( dlg, IDC_LABEL, wide );
        to_wide( s->unit ? s->unit : "", wide, ARRAY_SIZE(wide) );
        SetDlgItemTextW( dlg, IDC_UNIT, wide );
        SendDlgItemMessageW( dlg, IDC_SPIN, UDM_SETBUDDY, (WPARAM)GetDlgItem( dlg, IDC_NUMBER ), 0 );
        SendDlgItemMessageW( dlg, IDC_SPIN, UDM_SETRANGE32, s->min, s->max );
        swprintf( text, ARRAY_SIZE(text), L"%lu", (unsigned long)edit->number );
        SetDlgItemTextW( dlg, IDC_NUMBER, text );
        update_zero_hint( dlg, edit );
    }
    else if (s->kind == SEC_FLAG)
        CheckRadioButton( dlg, IDC_ENABLED, IDC_DISABLED, edit->number ? IDC_ENABLED : IDC_DISABLED );
    else
    {
        for (c = s->choices, i = 0; c && c->text; c++, i++)
        {
            to_wide( c->text, wide, ARRAY_SIZE(wide) );
            SendDlgItemMessageW( dlg, IDC_CHOICE, CB_ADDSTRING, 0, (LPARAM)wide );
            if (c->value == edit->number) index = i;
        }
        SendDlgItemMessageW( dlg, IDC_CHOICE, CB_SETCURSEL, index, 0 );
    }
    if (!usable)
    {
        EnableWindow( GetDlgItem( dlg, IDC_NUMBER ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_SPIN ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_ENABLED ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_DISABLED ), FALSE );
        EnableWindow( GetDlgItem( dlg, IDC_CHOICE ), FALSE );
    }
}

/* the page's value into the edit; FALSE (with a message) for a number that is not accepted */
static BOOL apply_setting_page( HWND dlg, struct edit *edit )
{
    const struct sec_setting *s = edit->setting;
    WCHAR text[64], message[200];

    if (s->reason) return TRUE;
    if (s->kind == SEC_FLAG) edit->number = IsDlgButtonChecked( dlg, IDC_ENABLED ) == BST_CHECKED ? 1 : 0;
    else if (s->kind == SEC_CHOICE)
    {
        int index = (int)SendDlgItemMessageW( dlg, IDC_CHOICE, CB_GETCURSEL, 0, 0 ), i = 0;
        const struct sec_choice *c;

        for (c = s->choices; c && c->text; c++, i++) if (i == index) edit->number = c->value;
    }
    else
    {
        WCHAR *end;
        unsigned long value;

        GetDlgItemTextW( dlg, IDC_NUMBER, text, ARRAY_SIZE(text) );
        value = wcstoul( text, &end, 10 );
        if (!text[0] || *end || value > 0xfffffffeUL || !sec_number_valid( s, (uint32_t)value ))
        {
            swprintf( message, ARRAY_SIZE(message), L"The value is not accepted. Accepted values: %lu to %lu.",
                      (unsigned long)s->min, (unsigned long)s->max );
            MessageBoxW( dlg, message, L"Local Security Settings", MB_OK | MB_ICONWARNING );
            return FALSE;
        }
        edit->number = (uint32_t)value;
    }
    edit->changed = TRUE;
    return TRUE;
}

static INT_PTR CALLBACK setting_proc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
    struct edit *edit = (struct edit *)GetWindowLongPtrW( dlg, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
        edit = (struct edit *)((PROPSHEETPAGEW *)lp)->lParam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)edit );
        init_setting_page( dlg, edit );
        return TRUE;
    case WM_COMMAND:
        if (!edit) break;
        if (LOWORD(wp) == IDC_ENABLED || LOWORD(wp) == IDC_DISABLED || (LOWORD(wp) == IDC_CHOICE && HIWORD(wp) == CBN_SELCHANGE))
            SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
        else if (LOWORD(wp) == IDC_NUMBER && HIWORD(wp) == EN_CHANGE)
        {
            update_zero_hint( dlg, edit );
            SendMessageW( GetParent( dlg ), PSM_CHANGED, (WPARAM)dlg, 0 );
        }
        break;
    case WM_NOTIFY:
        if (edit && ((NMHDR *)lp)->code == PSN_APPLY)
        {
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, apply_setting_page( dlg, edit ) ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE );
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR CALLBACK explain_proc( HWND dlg, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_INITDIALOG)
    {
        const struct sec_setting *s = ((struct edit *)((PROPSHEETPAGEW *)lp)->lParam)->setting;
        WCHAR text[1500], explain[900], support[400];

        to_wide( s->explain, explain, ARRAY_SIZE(explain) );
        support_text( s, support, ARRAY_SIZE(support) );
        swprintf( text, ARRAY_SIZE(text), L"%ls\r\n\r\n%ls", explain, support );
        SetDlgItemTextW( dlg, IDC_EXPLAIN, text );
        return TRUE;
    }
    return FALSE;
}

static void refresh_list(void);

static void show_properties( HWND owner, const struct sec_setting *s )
{
    struct edit edit = { s, s->def, FALSE };
    PROPSHEETPAGEW pages[2];
    PROPSHEETHEADERW header;
    WCHAR title[300];
    BOOL defined;

    /* the template again before it is edited: another administrator may have changed it since the window was opened */
    load_template();
    refresh_list();
    current_value( s, &edit.number, &defined );
    memset( pages, 0, sizeof(pages) );
    pages[0].dwSize = sizeof(pages[0]);
    pages[0].hInstance = instance;
    pages[0].pszTemplate = MAKEINTRESOURCEW( IDD_SETTING );
    pages[0].pfnDlgProc = setting_proc;
    pages[0].lParam = (LPARAM)&edit;
    pages[1] = pages[0];
    pages[1].pszTemplate = MAKEINTRESOURCEW( IDD_EXPLAIN );
    pages[1].pfnDlgProc = explain_proc;
    to_wide( s->name, title, ARRAY_SIZE(title) );
    memset( &header, 0, sizeof(header) );
    header.dwSize = sizeof(header);
    header.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_PROPTITLE;
    header.hwndParent = owner;
    header.hInstance = instance;
    header.pszCaption = title;
    header.nPages = 2;
    header.ppsp = pages;
    if (PropertySheetW( &header ) > 0 && edit.changed && !s->reason)
    {
        change_setting( owner, s, edit.number );
        refresh_list();
    }
}

/* ---------------------------------------------------------------------------------------------------- the panes */

static struct node *selected_node(void)
{
    TVITEMW item;

    item.mask = TVIF_PARAM;
    item.hItem = TreeView_GetSelection( tree );
    if (!item.hItem || !TreeView_GetItemW( tree, &item )) return NULL;
    return (struct node *)item.lParam;
}

/* a folder directly below parent (NULL: the root): its path has parent and one more part */
static BOOL is_child( const char *parent, const char *path )
{
    size_t n = parent ? strlen( parent ) : 0;

    if (parent && (strncmp( path, parent, n ) || path[n] != '\\')) return FALSE;
    return !strchr( path + (parent ? n + 1 : 0), '\\' );
}

static void refresh_list(void)
{
    struct node *node = selected_node();
    LVITEMW item;
    WCHAR name[300], value[300], text[80];
    unsigned int i, count = 0;
    int row = 0;

    ListView_DeleteAllItems( list );
    if (node)
    {
        for (i = 0; i < ARRAY_SIZE(folders); i++)
        {
            /* a folder lists its folders (the root, and Account Policies and Local Policies) */
            if (!is_child( node->path, folders[i] )) continue;
            to_wide( strrchr( folders[i], '\\' ) ? strrchr( folders[i], '\\' ) + 1 : folders[i], name, ARRAY_SIZE(name) );
            memset( &item, 0, sizeof(item) );
            item.mask = LVIF_TEXT | LVIF_IMAGE;
            item.iItem = row;
            item.pszText = name;
            item.iImage = icon_folder;
            ListView_InsertItemW( list, &item );
            row++;
            count++;
        }
        for (i = 0; node->path && i < sec_setting_count; i++)
        {
            if (strcmp( sec_settings[i].node, node->path )) continue;
            to_wide( sec_settings[i].name, name, ARRAY_SIZE(name) );
            value_text( &sec_settings[i], value, ARRAY_SIZE(value) );
            memset( &item, 0, sizeof(item) );
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.pszText = name;
            item.lParam = (LPARAM)&sec_settings[i];
            ListView_InsertItemW( list, &item );
            ListView_SetItemTextW( list, row, 1, value );
            row++;
            count++;
        }
    }
    swprintf( text, ARRAY_SIZE(text), L"%u item%ls", count, count == 1 ? L"" : L"s" );
    SendMessageW( status, SB_SETTEXTW, 0, (LPARAM)text );
}

static HTREEITEM add_node( HTREEITEM parent, const WCHAR *text, const char *path )
{
    TVINSERTSTRUCTW insert;
    struct node *node = NULL;

    if (node_count < ARRAY_SIZE(nodes))
    {
        node = &nodes[node_count++];
        node->path = path;
    }
    memset( &insert, 0, sizeof(insert) );
    insert.hParent = parent;
    insert.hInsertAfter = TVI_LAST;
    insert.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    insert.item.iImage = icon_folder;
    insert.item.iSelectedImage = icon_open;
    insert.item.pszText = (WCHAR *)text;
    insert.item.lParam = (LPARAM)node;
    return TreeView_InsertItemW( tree, &insert );
}

static void expand_all( HTREEITEM item )
{
    HTREEITEM child;

    TreeView_Expand( tree, item, TVE_EXPAND );
    for (child = TreeView_GetChild( tree, item ); child; child = TreeView_GetNextSibling( tree, child )) expand_all( child );
}

static void build_tree(void)
{
    HTREEITEM root, items[ARRAY_SIZE(folders)];
    unsigned int i, j;
    WCHAR name[128];

    node_count = 0;
    TreeView_DeleteAllItems( tree );
    root = add_node( TVI_ROOT, L"Security Settings", NULL );
    for (i = 0; i < ARRAY_SIZE(folders); i++)
    {
        HTREEITEM parent = root;

        for (j = 0; j < i; j++) if (is_child( folders[j], folders[i] )) parent = items[j];
        to_wide( strrchr( folders[i], '\\' ) ? strrchr( folders[i], '\\' ) + 1 : folders[i], name, ARRAY_SIZE(name) );
        items[i] = add_node( parent, name, folders[i] );
    }
    expand_all( root );
    TreeView_SelectItem( tree, root );
}

static void layout(void)
{
    RECT rect;
    int status_height, width, height;

    GetClientRect( frame, &rect );
    SendMessageW( status, WM_SIZE, 0, 0 );
    GetWindowRect( status, &rect );
    status_height = rect.bottom - rect.top;
    GetClientRect( frame, &rect );
    width = rect.right;
    height = rect.bottom - status_height;
    if (split > width - 100) split = width - 100;
    if (split < 80) split = 80;
    MoveWindow( tree, 0, 0, split, height, TRUE );
    MoveWindow( list, split + 3, 0, width - split - 3, height, TRUE );
}

static const struct sec_setting *selected_setting(void)
{
    LVITEMW item;
    int index = ListView_GetNextItem( list, -1, LVNI_SELECTED );

    if (index < 0) return NULL;
    memset( &item, 0, sizeof(item) );
    item.mask = LVIF_PARAM;
    item.iItem = index;
    return ListView_GetItemW( list, &item ) ? (const struct sec_setting *)item.lParam : NULL;
}

/* a folder of the list (a row with no setting) is opened in the tree */
static void open_folder_row(void)
{
    WCHAR wanted[128], buffer[128];
    HTREEITEM child, parent = TreeView_GetSelection( tree );
    int index = ListView_GetNextItem( list, -1, LVNI_SELECTED );
    TVITEMW item;

    if (index < 0) return;
    ListView_GetItemTextW( list, index, 0, wanted, ARRAY_SIZE(wanted) );
    for (child = TreeView_GetChild( tree, parent ); child; child = TreeView_GetNextSibling( tree, child ))
    {
        item.mask = TVIF_TEXT;
        item.hItem = child;
        item.pszText = buffer;
        item.cchTextMax = ARRAY_SIZE(buffer);
        if (TreeView_GetItemW( tree, &item ) && !wcscmp( buffer, wanted ))
        {
            TreeView_SelectItem( tree, child );
            return;
        }
    }
}

static LRESULT notify( NMHDR *hdr )
{
    if (hdr->hwndFrom == tree && hdr->code == TVN_SELCHANGEDW) refresh_list();
    else if (hdr->hwndFrom == list && hdr->code == NM_DBLCLK)
    {
        const struct sec_setting *s = selected_setting();

        if (s) show_properties( frame, s );
        else open_folder_row();
    }
    else if (hdr->hwndFrom == list && hdr->code == NM_CUSTOMDRAW)
    {
        NMLVCUSTOMDRAW *draw = (NMLVCUSTOMDRAW *)hdr;

        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && draw->nmcd.lItemlParam &&
            ((const struct sec_setting *)draw->nmcd.lItemlParam)->reason)
        {
            draw->clrText = GetSysColor( COLOR_GRAYTEXT );
            return CDRF_NEWFONT;
        }
    }
    return 0;
}

static LRESULT CALLBACK frame_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        SHFILEINFOW info;
        HIMAGELIST images;
        LVCOLUMNW column;

        frame = hwnd;
        tree = CreateWindowExW( WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_HASLINES |
                                TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        list = CreateWindowExW( WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                                LVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        status = CreateWindowExW( 0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, instance, NULL );
        ListView_SetExtendedListViewStyle( list, LVS_EX_FULLROWSELECT );
        memset( &column, 0, sizeof(column) );
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = (WCHAR *)L"Policy";
        column.cx = 470;
        ListView_InsertColumnW( list, 0, &column );
        column.pszText = (WCHAR *)L"Security Setting";
        column.cx = 260;
        ListView_InsertColumnW( list, 1, &column );
        images = (HIMAGELIST)SHGetFileInfoW( L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
                                             SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES );
        if (images)
        {
            icon_folder = info.iIcon;
            SHGetFileInfoW( L"C:\\", FILE_ATTRIBUTE_DIRECTORY, &info, sizeof(info),
                            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_OPENICON | SHGFI_USEFILEATTRIBUTES );
            icon_open = info.iIcon;
            TreeView_SetImageList( tree, images, TVSIL_NORMAL );
            ListView_SetImageList( list, images, LVSIL_SMALL );
        }
        load_template();
        build_tree();
        return 0;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_NOTIFY:
        return notify( (NMHDR *)lp );
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDM_EXIT: DestroyWindow( hwnd ); return 0;
        case IDM_PROPERTIES:
        {
            const struct sec_setting *s = selected_setting();

            if (s) show_properties( hwnd, s );
            return 0;
        }
        case IDM_REFRESH:
            load_template();
            refresh_list();
            return 0;
        case IDM_ABOUT:
            ShellAboutW( hwnd, L"Local Security Settings", L"Local security settings editor", NULL );
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* A setting by the name of its template line ("LockoutBadCount", or the last part of a registry value's) without the
 * window, for scripts and tests; the change goes the way the window's does (the template, gpsvc, the refresh), so it
 * needs the system group as well. 0 when it is installed and applied, else a Win32 error; ERROR_NOT_FOUND for a name
 * that is not in the table, ERROR_NOT_SUPPORTED for a setting that does not apply, ERROR_INVALID_PARAMETER for a
 * number out of range. */
DWORD WINAPI SecPol_SetW( const WCHAR *key, DWORD number )
{
    unsigned int i;
    WCHAR wide[128], *tail;

    if (!key) return ERROR_INVALID_PARAMETER;
    for (i = 0; i < sec_setting_count; i++)
    {
        if (!sec_settings[i].key[0]) continue;
        to_wide( sec_settings[i].key, wide, ARRAY_SIZE(wide) );
        tail = wcsrchr( wide, '\\' );
        if (!wcscmp( tail ? tail + 1 : wide, key )) break;
    }
    if (i == sec_setting_count) return ERROR_NOT_FOUND;
    if (sec_settings[i].reason) return ERROR_NOT_SUPPORTED;
    if (!sec_number_valid( &sec_settings[i], number )) return ERROR_INVALID_PARAMETER;
    quiet = TRUE;
    {
        DWORD err = change_setting( NULL, &sec_settings[i], number );

        quiet = FALSE;
        tmpl_free( tmpl );
        tmpl = NULL;
        return err;
    }
}

/* the entry point that mmc.exe calls for the console secpol.msc: runs until the window closes */
BOOL WINAPI SecPol_ExecuteW( HWND parent, HINSTANCE inst, const WCHAR *machine, int show )
{
    INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES |
                                                         ICC_UPDOWN_CLASS };
    WNDCLASSW wc;
    HACCEL accelerators;
    MSG msg;

    if (machine && machine[0])
    {
        MessageBoxW( parent, L"Only the local computer's security settings can be edited.", L"Local Security Settings",
                     MB_OK | MB_ICONINFORMATION );
        return FALSE;
    }
    InitCommonControlsEx( &controls );
    memset( &wc, 0, sizeof(wc) );
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SecPolFrame";
    wc.lpszMenuName = MAKEINTRESOURCEW( IDM_MAIN );
    RegisterClassW( &wc );
    frame = CreateWindowExW( 0, L"SecPolFrame", L"Local Security Settings", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             960, 520, parent, NULL, instance, NULL );
    if (!frame) return FALSE;
    accelerators = LoadAcceleratorsW( instance, MAKEINTRESOURCEW( IDA_MAIN ) );
    ShowWindow( frame, show ? show : SW_SHOWNORMAL );
    UpdateWindow( frame );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0)
    {
        if (!TranslateAcceleratorW( frame, accelerators, &msg )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    }
    UnregisterClassW( L"SecPolFrame", instance );
    tmpl_free( tmpl );
    tmpl = NULL;
    return TRUE;
}

BOOL WINAPI DllMain( HINSTANCE inst, DWORD reason, void *reserved )
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls( inst );
        instance = inst;
    }
    return TRUE;
}
