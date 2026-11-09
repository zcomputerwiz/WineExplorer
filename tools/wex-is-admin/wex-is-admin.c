/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * wex-is-admin USER: is USER a WineExplorer administrator?
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
 * Prints nothing; callers treat every nonzero status as "not an administrator".
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

    for (i = 1; i < argc; i++)
    {
#ifdef WEX_ACCOUNTS_TEST
        if (!strcmp( argv[i], "--directory" ) && i + 1 < argc) { account_directory = argv[++i]; continue; }
        if (!strcmp( argv[i], "--test-passwd" ) && i + 1 < argc) { test_passwd = argv[++i]; continue; }
        if (!strcmp( argv[i], "--test-group" ) && i + 1 < argc) { test_group = argv[++i]; continue; }
#endif
        if (name) return 2;
        name = argv[i];
    }
    if (!name) { fprintf( stderr, "usage: wex-is-admin USER\n" ); return 2; }
    if (!valid_name( name )) return 1;
    if ((dir = protected_directory( account_directory, &status )) == -1) return 1;
    status = read_policy( dir, &policy );
    close( dir );
    if (status) return 1;
    /* Two-way NSS agreement, as the server requires; uid 0 is never an account. */
    if (!native_passwd( name, 0, &user ) || !native_match( &user )) return 1;
    if (administrator_member( &user, policy.admin_group, &member )) return 1;
    return member ? 0 : 1;
}
