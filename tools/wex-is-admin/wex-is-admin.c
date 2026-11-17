/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * wex-is-admin USER: is USER a WineExplorer administrator?
 * USER is a local account's name, or a domain account's as the accounts map has it (DOMAIN\user): the latter is decided
 * by the server's own lookup (lookup_account), so that a member of the administrator group and a member of Domain Admins
 * are administrators exactly as the server takes them.
 *
 * The same rule and parser as the server (wine/server/accounts.c): the
 * administrator group is "admin_group=" of the root-owned, protected
 * /etc/wineexplorer/sysrun.conf, else "sudo"; membership comes from the group
 * database (getgrouplist, primary group included). A duplicate or invalid key,
 * a missing group or an unprotected configuration means nobody is an
 * administrator. Needs no privileges; never setuid/setgid.
 *
 * Exit status: 0 administrator, 1 not an administrator or not decidable
 * (unknown user, root, NSS disagreement, bad configuration), 2 usage error.
 * --print-group [SYSRUN_CONF] prints the effective administrator group, or
 * returns 1 for an invalid/unprotected configuration or a missing group.
 * The optional protected path is for provisioning an alternate machine root.
 * USER mode prints nothing; callers treat every nonzero status as denial.
 */
#define _GNU_SOURCE
#define WEX_ACCOUNTS_STANDALONE
#include "../../wine/server/accounts.c"

int main( int argc, char **argv )
{
    struct account_policy policy;
    struct account_native user;
    enum wex_account_status status;
    const char *name = NULL;
    int i, dir, member = 0;

    if (argc >= 2 && !strcmp( argv[1], "--print-group" ))
    {
        if (argc > 3) return 2;
        if ((dir = wex_sysrun_conf_parent( argc == 3 ? argv[2] : "/etc/wineexplorer/sysrun.conf" )) == -1) return 1;
        status = read_policy( dir, &policy );
        close( dir );
        if (status) return 1;
        puts( policy.admin_group );
        return 0;
    }
    for (i = 1; i < argc; i++)
    {
#ifdef WEX_ACCOUNTS_TEST
        if (!strcmp( argv[i], "--directory" ) && i + 1 < argc) { account_directory = argv[++i]; continue; }
        if (!strcmp( argv[i], "--test-passwd" ) && i + 1 < argc) { test_passwd = argv[++i]; continue; }
        if (!strcmp( argv[i], "--test-group" ) && i + 1 < argc) { test_group = argv[++i]; continue; }
        if (!strcmp( argv[i], "--test-winbind-passwd" ) && i + 1 < argc) { test_winbind_passwd = argv[++i]; continue; }
#endif
        if (name) return 2;
        name = argv[i];
    }
    if (!name) { fprintf( stderr, "usage: wex-is-admin USER | --print-group [SYSRUN_CONF]\n" ); return 2; }
    if (strchr( name, '\\' ))
    {
        /* a domain account: the map's record decides, as it does for the server (the administrator group by NSS, and
         * Domain Admins by the groups recorded at the logon) */
        struct wex_account account;

        if (!valid_any_account( name )) return 1;
        return lookup_account( name, 0, WEX_ACCOUNT_MAP_V1, &account ) == WEX_ACCOUNT_OK && account.administrator ? 0 : 1;
    }
    if (!accounts_valid_name( name )) return 1;
    if ((dir = protected_directory( account_directory, &status )) == -1) return 1;
    status = read_policy( dir, &policy );
    close( dir );
    if (status) return 1;
    /* Two-way NSS agreement, as the server requires; uid 0 is never an account. */
    if (!native_passwd( name, 0, &user ) || !native_match( &user )) return 1;
    if (administrator_member( &user, policy.admin_group, &member )) return 1;
    return member ? 0 : 1;
}
