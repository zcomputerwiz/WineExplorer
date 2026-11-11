/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * First-use provisioning of an administrator's managed profile in a standard
 * user's prefix (E1-S12 storage). Started by the root verifier after that
 * administrator has authenticated; root-owned, never setuid/setgid.
 *
 * Inputs and where they come from:
 *   NAME KEY MAP_GEN  the authenticated account; looked up again here in the
 *                     protected accounts map, which supplies the SID.
 *   USER_UID          the standard user (the verifier's kernel-checked peer).
 *   PREFIX_FD         the prefix directory, handed over by that user's
 *                     wineserver; never a path.
 * The seed is a fixed, root-protected file. All prefix I/O happens after the
 * permanent change to the user's identity. Normal build denies.
 */
#include "profile.c"
#define WEX_ACCOUNTS_STANDALONE
#define valid_name account_valid_name   /* both sources have a static of this name */
#include "../../wine/server/accounts.c"
#undef valid_name

#ifdef WEX_E1S12_PROFILE_TEST
#include <sys/prctl.h>

#include "anchor.h"

#ifndef WEX_PROFILE_SEED_PATH
#define WEX_PROFILE_SEED_PATH "/etc/skel/.wine/user.reg"
#endif

static int decimal( const char *text, uint64_t maximum, uint64_t *value )
{
    const char *p;
    uint64_t v = 0;
    if (!text[0] || (text[0] == '0' && text[1])) return 0;
    for (p = text; *p; p++)
    {
        unsigned int d = (unsigned char)*p - '0';
        if (d > 9 || d > maximum || v > (maximum - d) / 10) return 0;
        v = v * 10 + d;
    }
    *value = v;
    return 1;
}

static int key_bytes( const char *text, unsigned char *key )
{
    size_t i;
    if (strlen( text ) != 32) return 0;
    memset( key, 0, 16 );
    for (i = 0; i < 32; i++)
    {
        unsigned int c = (unsigned char)text[i], n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else return 0;
        key[i / 2] = (key[i / 2] << 4) | n;
    }
    return 1;
}

int main( int argc, char **argv )
{
    struct absolute_path seed = {0};
    struct wex_account a;
    struct wex_profile_storage out;
    struct passwd *pw;
    struct stat st;
    unsigned char key[16];
    uint64_t uid, generation, fd;
    gid_t gid;
    enum wex_profile_status s = WEX_PROFILE_COMPONENT;
    const char *site = "arguments";
    int pfd;

    memset( &a, 0, sizeof(a) );
    if (argc != 6) goto done;
    site = "root";
    if (geteuid() || getuid()) { s = WEX_PROFILE_OWNER; goto done; }
    site = "arguments";
    if (!account_valid_name( argv[1] ) || !key_bytes( argv[2], key ) ||
        !decimal( argv[3], UINT64_MAX, &generation ) || !generation ||
        !decimal( argv[4], UINT32_MAX - 1, &uid ) || !uid ||
        !decimal( argv[5], 1023, &fd ) || fd < 3) goto done;
    pfd = fd;

    /* the account as the protected map has it now, not as the caller describes it */
    site = "account";
    s = WEX_PROFILE_IDENTITY;
    if (lookup_account( argv[1], 0, WEX_ACCOUNT_MAP_V1, &a ) != WEX_ACCOUNT_OK) goto done;
    if (!a.administrator || a.generation != generation || memcmp( a.incarnation, key, 16 ) ||
        a.uid == uid) goto done;

    site = "user";
    s = WEX_PROFILE_OWNER;
    errno = 0;
    if (!(pw = getpwuid( uid )) || pw->pw_uid != uid || !pw->pw_gid) goto done;
    gid = pw->pw_gid;

    /* the prefix belongs to that user and is a directory; nothing is written yet */
    site = "prefix";
    if (fstat( pfd, &st ) || !S_ISDIR( st.st_mode ) || st.st_uid != uid) goto done;
    if (fcntl( pfd, F_SETFD, FD_CLOEXEC )) { s = WEX_PROFILE_IO; goto done; }

    site = "seed";
    if ((s = open_absolute( WEX_PROFILE_SEED_PATH, uid, 1, &seed ))) goto done;

    /* Clear root and supplementary authority permanently BEFORE prefix I/O. */
    site = "identity";
    s = WEX_PROFILE_OWNER;
    umask( 077 );
    if (setgroups( 0, NULL ) || setresgid( gid, gid, gid ) || setresuid( uid, uid, uid ) ||
        prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 )) goto done;
    if (!setuid( 0 ) || geteuid() != uid || getegid() != gid) goto done;

    site = "prepare";
    s = wex_profile_prepare( pfd, st.st_dev, st.st_ino, uid, gid, seed.edge[seed.count - 1].fd, &a, &out );
    if (!s)
    {
        if ((s = check_absolute( &seed ))) site = "seed-substitution";
        else printf( "status=0 reused=%d generation=%"PRIu64"\n", out.reused, out.profile_generation );
        wex_profile_storage_close( &out );
    }
done:
    if (s) printf( "status=%u site=%s\n", s, site );
    close_absolute( &seed );
    return s;
}
#else
int main( void )
{
    fputs( "DENIED=1 (E1-S12 storage disabled)\n", stderr );
    return WEX_PROFILE_DISABLED;
}
#endif
