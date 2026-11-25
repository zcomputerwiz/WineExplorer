/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Local Users and Groups through netapi32 (docs/LOCAL_USERS_DESIGN.md): the NetUser* and NetLocalGroup* calls on the local
 * computer, which netapi32 sends to the accounts service of session 0. The suite's section s0_accounts_chain runs this program
 * in a session, and elevated, against the whole chain.
 *
 * Usage: netapi_accounts_probe.exe MODE [ARG...] [--out FILE]
 *   list                 NetUserEnum level 0       "status=N total=T count=C names=a,b,c"
 *   info NAME            NetUserGetInfo level 20   "status=N" and, for 0, "name=NAME full=F flags=0xH rid=R"
 *   add NAME PASSWORD    NetUserAdd level 1        "status=N"
 *   del NAME             NetUserDel                "status=N"
 *   fullname NAME TEXT   NetUserSetInfo level 1011 "status=N"
 *   locked NAME 0|1      NetUserSetInfo level 1008 (UF_ACCOUNTDISABLE) "status=N"
 *   groups               NetLocalGroupEnum level 1 "status=N total=T names=a,b"
 *   members GROUP        NetLocalGroupGetMembers 3 "status=N total=T names=A\x,B\y"
 *   localgroups NAME     NetUserGetLocalGroups     "status=N names=a,b"
 *   addmember GROUP NAME NetLocalGroupAddMembers 3 "status=N"
 *   delmember GROUP NAME NetLocalGroupDelMembers 3 "status=N"
 *   changepw NAME OLD NEW NetUserChangePassword    "status=N"
 * The output goes to FILE when --out is given (an elevated process has no console).
 */
#include <windows.h>
#include <lm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static WCHAR *w(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *r = malloc(n * sizeof(WCHAR));

    MultiByteToWideChar(CP_UTF8, 0, s, -1, r, n);
    return r;
}

static void pw(const WCHAR *s)
{
    char buf[1024];

    WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL);
    fputs(buf, stdout);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    NET_API_STATUS st;
    DWORD a, i, read = 0, total = 0, parm = 0;
    BYTE *buf = NULL;

    for (a = 1; a + 1 < (DWORD)argc; a++)
        if (!strcmp(argv[a], "--out")) { freopen(argv[a + 1], "w", stdout); argc = a; break; }

    if (!strcmp(mode, "list"))
    {
        st = NetUserEnum(NULL, 0, FILTER_NORMAL_ACCOUNT, &buf, MAX_PREFERRED_LENGTH, &read, &total, NULL);
        printf("status=%lu\ntotal=%lu count=%lu names=", st, total, read);
        for (i = 0; st == 0 && i < read; i++) { if (i) putchar(','); pw(((USER_INFO_0 *)buf)[i].usri0_name); }
        printf("\n");
    }
    else if (!strcmp(mode, "info") && argc > 2)
    {
        st = NetUserGetInfo(NULL, w(argv[2]), 20, &buf);
        printf("status=%lu\n", st);
        if (!st)
        {
            USER_INFO_20 *u = (USER_INFO_20 *)buf;

            printf("name="); pw(u->usri20_name); printf(" full="); pw(u->usri20_full_name);
            printf(" flags=0x%lx rid=%lu\n", u->usri20_flags, u->usri20_user_id);
        }
    }
    else if (!strcmp(mode, "add") && argc > 3)
    {
        USER_INFO_1 u = { 0 };

        u.usri1_name = w(argv[2]);
        u.usri1_password = w(argv[3]);
        u.usri1_priv = USER_PRIV_USER;
        u.usri1_flags = UF_SCRIPT | UF_NORMAL_ACCOUNT;
        st = NetUserAdd(NULL, 1, (BYTE *)&u, &parm);
        printf("status=%lu\nparm=%lu\n", st, parm);
    }
    else if (!strcmp(mode, "del") && argc > 2)
    {
        st = NetUserDel(NULL, w(argv[2]));
        printf("status=%lu\n", st);
    }
    else if (!strcmp(mode, "fullname") && argc > 3)
    {
        USER_INFO_1011 u = { w(argv[3]) };

        st = NetUserSetInfo(NULL, w(argv[2]), 1011, (BYTE *)&u, &parm);
        printf("status=%lu\n", st);
    }
    else if (!strcmp(mode, "locked") && argc > 3)
    {
        USER_INFO_1008 u = { UF_SCRIPT | UF_NORMAL_ACCOUNT | (atoi(argv[3]) ? UF_ACCOUNTDISABLE : 0) };

        st = NetUserSetInfo(NULL, w(argv[2]), 1008, (BYTE *)&u, &parm);
        printf("status=%lu\n", st);
    }
    else if (!strcmp(mode, "groups"))
    {
        st = NetLocalGroupEnum(NULL, 1, &buf, MAX_PREFERRED_LENGTH, &read, &total, NULL);
        printf("status=%lu\ntotal=%lu names=", st, total);
        for (i = 0; st == 0 && i < read; i++) { if (i) putchar(','); pw(((LOCALGROUP_INFO_1 *)buf)[i].lgrpi1_name); }
        printf("\n");
    }
    else if (!strcmp(mode, "members") && argc > 2)
    {
        st = NetLocalGroupGetMembers(NULL, w(argv[2]), 3, &buf, MAX_PREFERRED_LENGTH, &read, &total, NULL);
        printf("status=%lu\ntotal=%lu names=", st, total);
        for (i = 0; st == 0 && i < read; i++) { if (i) putchar(','); pw(((LOCALGROUP_MEMBERS_INFO_3 *)buf)[i].lgrmi3_domainandname); }
        printf("\n");
    }
    else if (!strcmp(mode, "localgroups") && argc > 2)
    {
        st = NetUserGetLocalGroups(NULL, w(argv[2]), 0, 0, &buf, MAX_PREFERRED_LENGTH, &read, &total);
        printf("status=%lu\nnames=", st);
        for (i = 0; st == 0 && i < read; i++) { if (i) putchar(','); pw(((LOCALGROUP_USERS_INFO_0 *)buf)[i].lgrui0_name); }
        printf("\n");
    }
    else if ((!strcmp(mode, "addmember") || !strcmp(mode, "delmember")) && argc > 3)
    {
        LOCALGROUP_MEMBERS_INFO_3 m = { w(argv[3]) };

        st = !strcmp(mode, "addmember") ? NetLocalGroupAddMembers(NULL, w(argv[2]), 3, (BYTE *)&m, 1)
                                        : NetLocalGroupDelMembers(NULL, w(argv[2]), 3, (BYTE *)&m, 1);
        printf("status=%lu\n", st);
    }
    else if (!strcmp(mode, "changepw") && argc > 4)
    {
        st = NetUserChangePassword(NULL, w(argv[2]), w(argv[3]), w(argv[4]));
        printf("status=%lu\n", st);
    }
    else { printf("usage\n"); return 2; }
    fflush(stdout);
    return 0;
}
