/*
 * WineExplorer Desktop Environment - file search engine (Search Companion, instant search)
 *
 * Walks the folders to look in on a worker thread, like Windows XP's search when the Indexing Service does not cover
 * the location, and posts every match to the window that asked.
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
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>
#include <windows.h>
#include <shlwapi.h>

#include "search_engine.h"

#define TEXT_CHUNK (1024 * 1024)

struct search_job
{
    SEARCH_CRITERIA c;
    HWND notify;
    UINT id;
    volatile LONG stop;
    HANDLE thread;
    LONG count;
    DWORD last_folder_tick;
    WCHAR windir[MAX_PATH];
    /* the phrase as the file may store it */
    char text_a[512];
    WCHAR text_w[256];
    int len_a, len_w;
};

void search_criteria_init(SEARCH_CRITERIA *c)
{
    memset(c, 0, sizeof(*c));
    c->subfolders = TRUE;
    c->system_folders = TRUE;
}

int search_local_drives(WCHAR roots[][MAX_PATH], int max)
{
    char *(CDECL *get_unix_name)(const WCHAR *) =
        (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "wine_get_unix_file_name");
    DWORD drives = GetLogicalDrives();
    int i, n = 0;

    for (i = 0; i < 26 && n < max; i++)
    {
        WCHAR root[4] = { 'A' + i, ':', '\\', 0 };
        char *unix_name;
        BOOL whole_system = FALSE;

        if (!(drives & (1 << i)) || GetDriveTypeW(root) != DRIVE_FIXED) continue;
        /* Z: shows the whole Linux file system: it is no Windows hard drive */
        if (get_unix_name && (unix_name = get_unix_name(root)))
        {
            whole_system = !strcmp(unix_name, "/");
            HeapFree(GetProcessHeap(), 0, unix_name);
        }
        if (whole_system) continue;
        lstrcpyW(roots[n++], root);
    }
    return n;
}

/* one pattern of the name field: with wildcards it is a file specification, otherwise a part of the name */
static BOOL name_matches(const SEARCH_CRITERIA *c, const WCHAR *name)
{
    const WCHAR *p = c->names;
    WCHAR token[MAX_PATH];

    if (!c->names[0]) return TRUE;
    while (*p)
    {
        int n = 0;
        while (*p == ';' || *p == ',' || *p == ' ') p++;
        while (*p && *p != ';' && *p != ',' && n < MAX_PATH - 1) token[n++] = *p++;
        while (n && token[n - 1] == ' ') n--;
        token[n] = 0;
        if (!n) continue;
        if (wcspbrk(token, L"*?") ? PathMatchSpecW(name, token) : StrStrIW(name, token) != NULL) return TRUE;
    }
    return FALSE;
}

static BOOL type_matches(const SEARCH_CRITERIA *c, const WCHAR *name, BOOL folder)
{
    const WCHAR *ext;
    WCHAR key[MAX_PATH + 2];

    if (!c->extensions[0]) return TRUE;
    if (folder) return FALSE;
    ext = PathFindExtensionW(name);
    if (!*ext) return FALSE;
    swprintf(key, ARRAY_SIZE(key), L"%s;", ext);
    return StrStrIW(c->extensions, key) != NULL;
}

static BOOL date_matches(const SEARCH_CRITERIA *c, const WIN32_FIND_DATAW *fd)
{
    const FILETIME *t;

    switch (c->date_kind)
    {
    case SEARCH_DATE_MODIFIED: t = &fd->ftLastWriteTime; break;
    case SEARCH_DATE_CREATED: t = &fd->ftCreationTime; break;
    case SEARCH_DATE_ACCESSED: t = &fd->ftLastAccessTime; break;
    default: return TRUE;
    }
    return CompareFileTime(t, &c->date_from) >= 0 && CompareFileTime(t, &c->date_to) <= 0;
}

static BOOL size_matches(const SEARCH_CRITERIA *c, ULONGLONG size, BOOL folder)
{
    if (!c->size_min && !c->size_max) return TRUE;
    if (folder) return FALSE;
    return size >= c->size_min && (!c->size_max || size <= c->size_max);
}

/* ---- "A word or phrase in the file" ---- */

static BOOL bytes_equal(const char *a, const char *b, int len, BOOL case_sensitive)
{
    int i;
    if (case_sensitive) return !memcmp(a, b, len);
    for (i = 0; i < len; i++)
        if (a[i] != b[i] && tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return FALSE;
    return TRUE;
}

static BOOL wide_equal(const WCHAR *a, const BYTE *b, int len, BOOL case_sensitive)
{
    int i;
    for (i = 0; i < len; i++)
    {
        WCHAR ch = b[2 * i] | (b[2 * i + 1] << 8);
        if (ch != a[i] && (case_sensitive || towlower(ch) != towlower(a[i]))) return FALSE;
    }
    return TRUE;
}

static BOOL buffer_contains(struct search_job *job, const BYTE *buf, DWORD len)
{
    BOOL cs = job->c.case_sensitive;
    DWORD i;

    if (job->len_a)
        for (i = 0; i + job->len_a <= len; i++)
            if (bytes_equal((const char *)buf + i, job->text_a, job->len_a, cs)) return TRUE;
    if (job->len_w)
        for (i = 0; i + 2 * job->len_w <= len; i++)
            if (wide_equal(job->text_w, buf + i, job->len_w, cs)) return TRUE;
    return FALSE;
}

static BOOL file_contains(struct search_job *job, const WCHAR *path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    DWORD keep = max(job->len_a, 2 * job->len_w), got;
    BYTE *buf;
    BOOL found = FALSE;
    DWORD have = 0;

    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!(buf = malloc(TEXT_CHUNK + keep))) { CloseHandle(file); return FALSE; }
    /* read in chunks; the end of a chunk is kept so that a phrase across two chunks is still found */
    while (!found && !job->stop && ReadFile(file, buf + have, TEXT_CHUNK, &got, NULL) && got)
    {
        DWORD len = have + got;
        found = buffer_contains(job, buf, len);
        have = min(keep, len);
        memmove(buf, buf + len - have, have);
    }
    free(buf);
    CloseHandle(file);
    return found;
}

/* ---- the walk ---- */

static void post_folder(struct search_job *job, const WCHAR *folder)
{
    DWORD now = GetTickCount();
    WCHAR *copy;

    if (now - job->last_folder_tick < 150) return;
    job->last_folder_tick = now;
    if ((copy = _wcsdup(folder))) PostMessageW(job->notify, WM_SEARCH_FOLDER, job->id, (LPARAM)copy);
}

static void found(struct search_job *job, const WCHAR *path, const WIN32_FIND_DATAW *fd)
{
    SEARCH_RESULT *r = malloc(sizeof(*r));

    if (!r) return;
    lstrcpynW(r->path, path, MAX_PATH);
    r->attributes = fd->dwFileAttributes;
    r->size = ((ULONGLONG)fd->nFileSizeHigh << 32) | fd->nFileSizeLow;
    r->modified = fd->ftLastWriteTime;
    InterlockedIncrement(&job->count);
    PostMessageW(job->notify, WM_SEARCH_RESULT, job->id, (LPARAM)r);
}

static BOOL skip_folder(struct search_job *job, const WCHAR *path, DWORD attributes)
{
    if (!job->c.hidden && (attributes & FILE_ATTRIBUTE_HIDDEN)) return TRUE;
    if (!job->c.system_folders && ((attributes & FILE_ATTRIBUTE_SYSTEM) || !wcsicmp(path, job->windir))) return TRUE;
    return attributes & FILE_ATTRIBUTE_REPARSE_POINT;   /* links to elsewhere: searched once, where they point */
}

static void walk(struct search_job *job, const WCHAR *folder, int depth)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    if (job->stop || depth > 64) return;
    post_folder(job, folder);
    PathCombineW(pattern, folder, L"*");
    if ((find = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE) return;
    do
    {
        BOOL dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        ULONGLONG size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;

        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (!job->c.hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        if (!PathCombineW(path, folder, fd.cFileName)) continue;

        if (name_matches(&job->c, fd.cFileName) && type_matches(&job->c, fd.cFileName, dir) &&
            date_matches(&job->c, &fd) && size_matches(&job->c, size, dir) &&
            (!job->c.text[0] || (!dir && file_contains(job, path))))
            found(job, path, &fd);

        if (dir && job->c.subfolders && !skip_folder(job, path, fd.dwFileAttributes)) walk(job, path, depth + 1);
    } while (!job->stop && FindNextFileW(find, &fd));
    FindClose(find);
}

static DWORD WINAPI search_thread(void *arg)
{
    struct search_job *job = arg;
    int i;

    for (i = 0; i < job->c.root_count && !job->stop; i++) walk(job, job->c.roots[i], 0);
    PostMessageW(job->notify, WM_SEARCH_DONE, job->id, job->count);
    return 0;
}

SEARCH_JOB *search_start(const SEARCH_CRITERIA *c, HWND notify, UINT id)
{
    struct search_job *job = calloc(1, sizeof(*job));

    if (!job) return NULL;
    job->c = *c;
    job->notify = notify;
    job->id = id;
    GetWindowsDirectoryW(job->windir, MAX_PATH);
    if (c->text[0])
    {
        lstrcpynW(job->text_w, c->text, ARRAY_SIZE(job->text_w));
        job->len_w = lstrlenW(job->text_w);
        job->len_a = WideCharToMultiByte(CP_ACP, 0, c->text, -1, job->text_a, sizeof(job->text_a), NULL, NULL);
        if (job->len_a) job->len_a--;
        /* UTF-8 is what Linux files use for text; it is the ANSI form for ASCII phrases */
        if (!job->len_a) job->len_a = WideCharToMultiByte(CP_UTF8, 0, c->text, -1, job->text_a, sizeof(job->text_a), NULL, NULL) - 1;
    }
    if (!(job->thread = CreateThread(NULL, 0, search_thread, job, 0, NULL)))
    {
        free(job);
        return NULL;
    }
    return job;
}

void search_stop(SEARCH_JOB *job)
{
    if (!job) return;
    InterlockedExchange(&job->stop, 1);
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    free(job);
}
