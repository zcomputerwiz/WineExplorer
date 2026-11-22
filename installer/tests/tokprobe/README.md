# tokprobe

What a sandboxed child process (a restricted token like Chromium's GPU and utility processes, low integrity) can
reach on an image with elevation enforced: HKCU\Software\Wine opened as win32u does (create, MAXIMUM_ALLOWED), the
display keys under HKLM, display devices and monitors, a window. Diagnostic for the post-flip Copilot "Sign in"
dialog (blank, behind its owner).

Build (wexbuild or any machine with MinGW):

    x86_64-w64-mingw32-gcc -O2 -Wall -o tokprobe.exe tokprobe.c -ladvapi32 -luser32

Run in a user's desktop session (as root on the guest, with session-run.py staged as in check-wow64.sh):

    WINEDEBUG=err+all python3 /tmp/wex-session-run.py --user wineuser --wait /tmp/tokprobe.exe > /tmp/tokprobe.txt 2>&1

It prints one block per token: plain, low-il, restricted, restricted+low-il.
