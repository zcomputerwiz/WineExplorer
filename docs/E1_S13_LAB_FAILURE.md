# E1-S13 lab failure (first run of the S13 harness)

Status 2026-11-07. The S10/S11 harness passes (E1S10_TOKEN OK, 52 PASS lines, both ABIs), S6 passes (302 checks), S12
passes (scripts/test_profile_storage.sh), the main and normal Wine builds pass. The S13 harness
(scripts/test_hku_load.sh, run in the clean wexbuild as root with WINE_BUILD_DIR=/root/owned-e1s6/build/wow64 and
WINE_NORMAL_BUILD_DIR=/root/owned-e1s11/build/normal-wow64) now builds both client ABIs with the S13 define and gets
through fixture setup and the S12 provisioning of Bob's profile for case accept (both fixed in the harness: the seed
file must be mode 0644 and use the audited default-template format). The first real S13 case fails:

lab_hku_accept_64, peer (32-bit, standard user Bob), step "S13 BEFORE", before the broker loads anything:

    FAIL e1s13_key_path:34 call=query && !last_status last_NT_call=NtQueryKey(KeyNameInformation) NTSTATUS=c0000008
    FAIL e1s13_key_path:36 call=!_wcsicmp(value.name, expected)
    ... later: FAIL peer:481 marker == (death ? 0x7e : 0)

c0000008 is STATUS_INVALID_HANDLE: RegOpenCurrentUser(KEY_READ) (and the KEY_ALL_ACCESS variant in e1s13_peer_state)
returns success but the returned handle is not a valid key handle when queried with NtQueryKey in the 32-bit peer.
Bob is an ordinary token at that point (nothing loaded yet).

Open questions for the next fix: which handle does RegOpenCurrentUser return in the lab build (ntdll reg.c
RtlOpenCurrentUser versus the kernelbase e1s_special_root path), is the handle valid in the 32-bit process
(WoW64 handle truncation or a handle from the wrong process or a closed handle in a cache), and does the 64-bit
peer behave the same? Run one case by hand if useful.

Separate flake, not yet understood: about every second harness run `wex-accounts sync` in the harness fails with
status=6 (WEX_ACCOUNT_MAP_UNPROTECTED, a read_protected check in wine/server/accounts.c) right after the machine
root setup, and the next run passes. The harness now writes map-state-pre.log (stat and getfacl of /, /etc,
/etc/wineexplorer and the map, plus the group) before the sync and keeps it on failure. Candidates: a stale
/etc/wineexplorer or group id from the previous run, a default ACL, or a timing issue in save_map.
