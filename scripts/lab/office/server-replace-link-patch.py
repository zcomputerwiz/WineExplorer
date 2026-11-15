# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Lab: apply the pending Wine change of docs/OFFICE.md blocker 5 to a wine tree (argv: path of server/fd.c).

Overwriting a file that is a link to an unwritable file replaces the link.  To be committed to the wine fork."""
import sys

path = sys.argv[1]
text = open(path, newline='').read()
anchor = """                fd->unix_fd = sysrun_open( name, O_RDONLY | (flags & ~(O_TRUNC | O_CREAT | O_EXCL)), *mode );
        }
"""
addition = """
        /* a link to a file this process may not write (a font that the system setup links into the Fonts
         * directory) is replaced when the file is to be overwritten, as an existing file is on Windows */
        if (fd->unix_fd == -1 && errno == EACCES && (flags & O_CREAT) && (flags & O_TRUNC) &&
            (access & FILE_UNIX_WRITE_ACCESS) && !(options & FILE_DIRECTORY_FILE))
        {
            char *link_path = dup_fd_name( root, name );
            struct stat st_link;

            if (link_path && !lstat( link_path, &st_link ) && S_ISLNK( st_link.st_mode ) && !unlink( link_path ))
                fd->unix_fd = sysrun_open( name, rw_mode | (flags & ~O_TRUNC) | O_EXCL, *mode );
            else errno = EACCES;
            free( link_path );
        }
"""
if 'a link to a file this process may not write' in text:
    print('already patched')
    sys.exit(0)
if text.count(anchor) != 1:
    print('anchor found %d times' % text.count(anchor))
    sys.exit(1)
open(path, 'w', newline='').write(text.replace(anchor, anchor + addition))
print('patched')
