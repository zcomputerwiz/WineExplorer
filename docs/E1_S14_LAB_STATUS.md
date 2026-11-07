# E1-S14 lab status

Run 2026-11-07 (scripts/test_hku_load.sh in the clean wexbuild; owned lab tree rebuilt from the synced clone):
builds pass (main and normal, protocol 979); S13 cases accept, absent, unprovisioned, wrong-incarnation,
wrong-reference, owner, already-loaded, replay and the S14 unload case (unload with live process denied, unload after the
last process ends, persistence and reload, stale root, double unload) all PASS for the 64-bit broker.

Fixes made while bringing S14 up (all in wine/server/registry.c): a persisted hive already contains Software\Classes
and Wow6432Node and its saved parent keys, so create_key_recursive and load_keys leave STATUS_OBJECT_NAME_EXISTS set
and the reload failed on `if (get_error()) goto failed` after set_value (and in e1s_read_hive). That warning is now
cleared at both places, and after a successful managed load.

Open: the first crash case (crash-1, checkpoint 1 of the durable save, lab-only hook WEX_E1S14_CRASH_TEST in
wine/server/wex_profile_storage.c save_checkpoint, _exit(86)) fails in scripts/lib/e1s13-fixture.sh e1s14_crash_finish:
the crash did happen (server stderr has E1S14 save-crash=1) but the peer (32-bit, Bob) ended with rc=124 (the
`timeout -k 2 $PEER_SECONDS` kill, 90 s) instead of dying with the server. The peer log stops after
"E1S10 PEER MARKER SENT" (waiting for the broker's result byte). Unknown: when the server died relative to the peer
(the peer is a live user of the hive, so the unload that triggers the save may be denied until the peer is gone, in
which case the crash happens after the peer ended and the check must not demand that the peer saw the death), and
what a Wine client does when its server _exit()s while it is blocked in ReadFile on a pipe.
