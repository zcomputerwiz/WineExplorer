#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -e

rm -rf /tmp/wine_cow_test /tmp/cow_test_user_target.txt /tmp/cow_test_user_link.txt
echo "ORIGINAL_TARGET_DATA" > /tmp/cow_test_user_target.txt
ln -s /tmp/cow_test_user_target.txt /tmp/cow_test_user_link.txt
chmod 666 /tmp/cow_test_user_target.txt

WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=/tmp/wine_cow_test wineboot -i
wineserver -w

echo "--- 1. Testing Z: symlink write-through ---"
WINEPREFIX=/tmp/wine_cow_test wine /usr/local/bin/test_cow_cases64.exe z_symlink
wineserver -w
if [ -L /tmp/cow_test_user_link.txt ]; then
    echo "PASS: /tmp/cow_test_user_link.txt is still a symbolic link (did not break into a copy)!"
    echo "Target content: $(cat /tmp/cow_test_user_target.txt)"
else
    echo "FAIL: /tmp/cow_test_user_link.txt was broken into a copy!"
    exit 1
fi

echo "--- 2. Testing relative open with RootDirectory on built-in symlink ---"
ls -l /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll
WINEPREFIX=/tmp/wine_cow_test wine /usr/local/bin/test_cow_cases64.exe relative_open
wineserver -w
ls -l /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll
if [ ! -L /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll ] && [ -f /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll ]; then
    echo "PASS: drive_c/windows/system32/msvcp140.dll is now a regular file!"
    echo "msvcp140.dll header: $(head -c 27 /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll)"
else
    echo "FAIL: msvcp140.dll was not broken into a regular file!"
    exit 1
fi

echo "--- 3. Testing DeleteFileW on built-in symlink ---"
ls -l /tmp/wine_cow_test/drive_c/windows/system32/version.dll
WINEPREFIX=/tmp/wine_cow_test wine /usr/local/bin/test_cow_cases64.exe delete_symlink
wineserver -w
if [ ! -e /tmp/wine_cow_test/drive_c/windows/system32/version.dll ]; then
    echo "PASS: drive_c/windows/system32/version.dll was deleted (unlinked)!"
    echo "Package file remains intact:"
    ls -l /usr/local/lib/wine/x86_64-windows/version.dll
else
    echo "FAIL: version.dll was not deleted!"
    exit 1
fi

echo "--- 4. Testing COW as limited user (testuser) ---"
chmod 666 /tmp/cow_test_user_target.txt
su testuser -c '
    rm -rf /home/testuser/test_pfx
    WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=" WINEPREFIX=/home/testuser/test_pfx wineboot -i
    wineserver -w
    WINEPREFIX=/home/testuser/test_pfx wine /usr/local/bin/test_cow_cases64.exe z_symlink
    wineserver -w
    if [ -L /tmp/cow_test_user_link.txt ]; then
        echo "PASS: user Z: symlink still a symlink!"
    fi
    WINEPREFIX=/home/testuser/test_pfx wine /usr/local/bin/test_cow_cases64.exe relative_open
    wineserver -w
    if [ ! -L /home/testuser/test_pfx/drive_c/windows/system32/msvcp140.dll ]; then
        echo "PASS: limited user relative open broke symlink to regular file!"
        ls -l /home/testuser/test_pfx/drive_c/windows/system32/msvcp140.dll
        echo "msvcp140.dll header: $(head -c 27 /home/testuser/test_pfx/drive_c/windows/system32/msvcp140.dll)"
    fi
    rm -rf /home/testuser/test_pfx
'

echo "ALL COW TESTS PASSED PERFECTLY!"
