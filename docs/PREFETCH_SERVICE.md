<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# WineExplorer Prefetcher and Page-Aligned PE Image Cache (`wexprefetchd`)

Design specification for WineExplorer's shared PE image-alignment cache, Windows XP Prefetcher (`C:\Windows\Prefetch`), and host memory tuning (KSM and `zram` swap) on Debian 13 (Trixie).

## 0. Status: what is built (2026-11-01)

**Built and tested: the image cache half (phase 2).** Not built: traces, readahead, `EnablePrefetcher`, the Prefetch folder,
KSM/zram tuning inside the daemon (KSM is enabled with `MemoryKSM=yes` on the session unit, zram with
`systemd-zram-generator`; both verified by hand, see `EDGE_DEVELOPMENT_RUN_20261101.md`).

Deviations from the design below, all deliberate:

- **Transport is a systemd-activated `AF_UNIX` `SOCK_SEQPACKET` socket with `SCM_RIGHTS`, not D-Bus.** Passing file
  descriptors through libdbus from inside `ntdll`, which holds the process's virtual-memory lock while mapping an image,
  was judged too heavy and risky. The security model is unchanged: one request carries one open descriptor, never a path,
  and the daemon takes the identity and contents from that descriptor and hashes them itself. A D-Bus control and
  properties interface can be added beside it. The wire format and the flat image layout are specified in
  `src/wexprefetchd/protocol.h`.
- **The cache entry is a "flat image", not a compact per-section file.** Section bytes are stored at their RVA in a sparse
  file named by the SHA-256 of the source (`<sha256>.img`), so a client maps a section with one `mmap` at the offset equal to
  its `VirtualAddress` and needs no layout table. The per-user in-process cache (tier 2) keeps its compact layout.
- **Layout of the code:** `src/wexprefetchd/` (`wexprefetchd.c` daemon, `sha256.c`, `wexpf-client.c` test client,
  `verify_flat.py` independent oracle, `test_wexprefetchd.py` suite, `Makefile`), `installer/systemd/wexprefetchd.*`
  and `prefetch.conf.example`, packaging in `installer/package-wine.sh`, Wine client in `dlls/ntdll/unix/virtual.c`
  (`open_prefetch_image`, three tiers: daemon, in-process cache, `pread`; `WINE_PE_CACHE=0` disables the first two,
  `WINE_PREFETCH_SOCKET` overrides the socket path for tests).

Tests (`make -C src/wexprefetchd check` for SHA-256; `python3 test_wexprefetchd.py ./wexprefetchd SMALL.exe [BIG.dll]`
for the rest): 46 checks pass on the release build, and 41 on an AddressSanitizer plus UBSan build. They cover: exact
flat-image bytes against the independent oracle, the reported digest, a byte-identical copy dedup, a modified copy getting
its own entry, protocol abuse (no or two descriptors, wrong magic, version or op, short datagram, write-only, directory and
socket descriptors), hostile and non-PE files, 300 random header mutations, 12 concurrent first requests producing one build,
a warm cache with an empty memo, a 48-request start-up burst, `MaxImageMB`, `Enabled=0` with `SIGHUP`, the cache cap,
`SIGTERM` exit, the status file, and no leftover temporary files.

Integration (Edge 156 and Chromium 156 on Wine 11.21, 16 GiB guest, fresh prefix each run, daemon installed from the real
unit files and socket-activated as `wexcache`): both pass with the daemon. All images (211 requests for one Edge run, 198
from the memo) come from the service and the prefix's in-process cache stays empty. With the service stopped, Edge still
passes through tier 2 (12 files, 328 MiB). The unit rates 3.2 ("OK") in `systemd-analyze security`. A first request for
`msedge.dll` (340 MB) takes 2.2 s, then 0.27 ms from the memo.

Bugs the integration found, now fixed and covered by tests:

1. After taking the global build lock the daemon did not re-check its memo, so concurrent requests for one big DLL each
   re-read and re-hashed it in turn (12 requests: 20 s, past the client's 15 s timeout, so some clients fell back).
2. The Wine client treated `EINTR` as failure. Wine delivers signals to its threads constantly and a socket with a timeout
   is not restarted after one, so a `recvmsg` returned early, the client closed the socket and the daemon logged
   `reply ... failed: Broken pipe`. The client now retries `connect`, `sendmsg` and `recvmsg` on `EINTR`.
3. `MaxConcurrent` default raised from 16 to 128 (a browser start-up has 20+ processes loading images at once), thread
   stacks set to 256 KiB, and a `BUSY` reply bounded the burst test.

Codex review (adversarial review of the daemon, standard review of the Wine commits), all findings fixed with tests:

- *Sparse images bypassed the cache cap* (the cap counted allocated blocks only, so near-empty images cost nothing and entries
  were never evicted): every entry is now also charged 4 KiB, entries are capped by count (`MaxEntries`), a PE with no
  section data is refused, and unseen-file reads and hashes are limited per user (`MaxBuildsPerMinute`).
- *A stalled read could hold the global build lock forever* (a file on a stalled or hostile FUSE mount): sources on FUSE, NFS, SMB
  and 9p are refused (`RejectNetworkFS`, default on); the client then loads the image itself.
- *No `fsync` before publication*: the data is synced before the rename and the directory after it.
- *The accept thread blocked on client input when at `MaxConcurrent`*: an overloaded connection is now turned away at once,
  without reading from it, so a silent client cannot stall accepting. The client sees BUSY or a reset and falls back.
- Wine client: socket flags guarded for systems without `SOCK_CLOEXEC` and `MSG_CMSG_CLOEXEC`; a cache that cannot be mapped
  executable (a `noexec` filesystem) is detected and not used, where before the program died with an access violation on its
  first instruction (reproduced and fixed); `EmptyWorkingSet` walks per-page commitment so reserve-then-commit memory is trimmed
  too (534 MB resident to 10 MB in the test, from 270 MB untouched by the first version).

The three modules are wired in (2026-11-01):

- **Traces** (`trace.c`). Every image the daemon serves is noted per client process (`SO_PEERCRED` pid; the path comes from
  `/proc/self/fd/N` of the descriptor it received, never from the client). The first non-DLL image names the program. After
  30 seconds (`TRACE_WINDOW_SECONDS`) or when the process is gone, `<name>-<hash>.pf` is written atomically (fsync, rename) under
  `/var/cache/wineexplorer/prefetch/<uid>/` with `pf_v17.c`; the run count of the previous file is carried over and the file list is
  replaced by the newest run's. At most 1024 `.pf` files per user. Each user's directory is owned by the daemon (mode 0750) with a POSIX ACL
  that gives only that user read access (and a default ACL so the files inherit it), because a trace lists the paths a
  user's programs load; other users cannot read or list it. Paths in the file are Unix paths (UTF-16, not NT paths) and the hash is
  taken over the upper-cased Unix path, so these files are for this daemon and for parsers, not for Windows. A process that
  loads fewer than two images writes nothing. Config: `Prefetch=0` turns recording off.
- **Readahead** (`readahead.c`). When a program that has a `.pf` starts (its executable is served), a detached thread opens
  each listed file (`O_NOFOLLOW`, regular files only, not on a network or FUSE filesystem) and replays whole-file ranges with
  `ra_replay` (serial on rotational disks, parallel otherwise). The same program is replayed at most once a minute.
  Config: `Readahead=0` turns it off. The daemon warms the files it can read (the machine root, system installs). The
  ones it cannot open (a private home directory, `ProtectHome=yes`) are kept per process: the replies for that process carry
  `WEXPF_HINT_TRACE`, and Wine sends one `WEXPF_OP_GET_TRACE` request (no descriptor) and gets those paths back, once, which
  it opens with its own rights and passes to `posix_fadvise(POSIX_FADV_WILLNEED)` (`fetch_prefetch_trace`, at most 64 files and
  512 MB). The list is the same user's own earlier trace, so nothing leaks. Ranges are whole files, not the sections
  actually touched (a refinement for later). Only processes that ask the daemon for at least one image (one with unaligned sections) can get the hint.
- **KSM tuner** (`ksm_tuner.c`). `wexprefetchd --ksm-tuner` runs the `ksmtuned`-style loop every 5 seconds (MemAvailable,
  `pages_sharing`) and writes `/sys/kernel/mm/ksm/{run,pages_to_scan,sleep_millisecs}` only when a value changes. It needs
  write access to sysfs, so it is its own unit, `wexksmtuned.service` (no capabilities, `ReadWritePaths=/sys/kernel/mm/ksm`,
  `ConditionPathExists` on KSM), enabled by the package postinst next to the socket.

Tests: `make check` (modules) and `make check-daemon PE=small.exe` (the 40-check daemon suite plus `test_trace.py`: `.pf`
written and named by the XP hash, run count 2 on the second run, replay of three files, a corrupted `.pf` leaving the daemon up,
`Prefetch=0`, and the tuner against a fake sysfs and meminfo). `test_trace.py` also passes with the daemon built under
AddressSanitizer and UBSan. A first version looped four billion times on an unreadable `.pf` (`(unsigned)-1` as the file
count); the corrupt-file test caught it.

Windows side: `scripts/setup-machine-root.sh` links `C:\Windows\Prefetch` to `/var/cache/wineexplorer/prefetch` (one
directory per user inside it, see above) and sets
`HKLM\System\CurrentControlSet\Control\Session Manager\Memory Management\PrefetchParameters\EnablePrefetcher` to 3.
`installer/systemd/zram-generator.conf` (`zram-size = ram / 2`, zstd, priority 100) and `99-wineexplorer-memory.conf`
(`vm.swappiness = 100`, `vm.page-cluster = 0`) ship in the session package, which recommends `systemd-zram-generator`.

Verified in the QEMU guest with the real socket-activated service and the WoW64 build (2026-11-02):
- Chromium twice: functional, 64-bit and 32-bit startup exit 0; one `CHROME.EXE-<hash>.pf` appears (820 bytes: the daemon is
  asked only for images with unaligned sections, so a trace lists chrome.exe, chrome_elf.dll and chrome.dll, not the
  page-aligned DLLs). Every `chrome.exe` child process counts as a run (the count reached 27 in one Chromium start).
- `chrome.exe --version` from `/opt` three times: `.pf` after the first; the second start logged `readahead: 3 files, 3 ranges`; the
  third, 40 s later, was throttled by the 60 s rule.
- A program under `/home/debian` (mode 0700) writes a trace, but its replay opens nothing: the service user cannot read it.
- `wexksmtuned.service` starts under `systemd-analyze security` 3.7 OK. With 15 GiB free it leaves `run=0`, as `ksmtuned` does:
  merging starts only under memory pressure (below about 20% of RAM plus 2 GiB free).

Not verified: the packaged ISO, the replay effect on a rotational disk, `EnablePrefetcher` and the folder link in a freshly
built machine root (the script passes `bash -n`; it was not run).

Where the format document of `pf_v17.c` is ambiguous, the writer chose (these files are for tools and parity, not read back by
Windows): file header offset 8 is `0x0000000f` (the value seen on XP/2003 files); unused bytes of the 60-byte executable name
and the unknown fields at file information offsets 44 and 64 are zero; each file gets one trace chain entry (one block, or the
caller's `chain_blocks` count; flags 0x02 for executables, else 0x08); the filename strings array is not padded; the volume
device path is stored with a trailing NUL and its character count excludes the NUL; each volume carries an 8-byte version 1
file references header with no entries and no directory strings; the hash is computed over the unsigned bytes of the UTF-16LE
uppercase path with the documented folding step (`CcPfHashValue`'s signed-char variant is not used).

Open items: whether the tier-2 cache should move to `$XDG_CACHE_HOME`, and whether to count only top-level starts as runs.

---

## 1. Problem Statement (Measured)

Under stock Wine (`dlls/ntdll/unix/virtual.c`, `map_file_into_view`), mapping a PE image section with `mmap()` requires the section's raw file offset (`PointerToRawData`) to be a multiple of the host page size (4096 bytes). Many large Windows binaries—including Microsoft Edge's `msedge.dll` and Chromium's `chrome.dll`—are linked with `FileAlignment = 0x200` (512 bytes), placing sections such as `.text` (`0x600`) and `.data` (`0x141f1800`) at unaligned file offsets.

When `map_file_into_view` encounters an unaligned section offset, it falls back from `mmap()` to `mprotect()` + `pread()` into private anonymous memory (`docs/EDGE_DEVELOPMENT_RUN_20261101.md`):

- Each Edge/Chromium renderer holds **~330 MB of private anonymous memory** for the browser DLL image alone (`r-xp` 278 MB fully resident with no pathname, plus 37 MB `r--p`), instead of sharing clean file-backed pages across processes.
- In an 8 GiB guest with **no swap configured**, launching Edge in a normal browser window exhausted RAM (`7.35 / 7.95 GB` used, `114 MB` page cache), causing `kswapd` thrashing and probe timeouts.
- Realigning `msedge.dll` on disk to `FileAlignment = 0x1000` (`ChromiumLab/tools/pe_realign.py`) moved ~280 MB per process from private anonymous memory into shared page cache (16 GiB guest, `t = 10 s`, 8 renderers):

| Metric (`t = 10 s`) | Original (`0x200`) | Realigned (`0x1000`) | Delta |
|---|---|---|---|
| 8 renderers (total PSS) | 5926 MB (~740 MB each) | 4007 MB (~462 MB typical) | −1919 MB |
| Browser / GPU (PSS) | 480 MB / 482 MB | 233 MB / 201 MB | −247 / −281 MB |
| Guest used / page cache | 9.1 GB / 4.4 GB | 5.4 GB / 5.6 GB | −3.7 GB used |

Rewriting installed PE binaries in place is unsafe: it breaks Authenticode signatures, package/updater integrity checks, and read-only shared installations in `/var/lib/wineexplorer/machine`. Wine needs a transparent, out-of-place page-aligned image cache paired with boot/application prefetching and host memory deduplication/swap.

---

## 2. Architecture Overview

The subsystem has two halves managed by a single unprivileged system daemon, `wexprefetchd`:

1. **Content-Addressed PE Image Cache & Prefetcher**: Builds 4096-aligned copies of unaligned PE images in `/var/cache/wineexplorer/images`, hands read-only file descriptors back to Wine's loader over D-Bus, records startup working sets into XP v17 `.pf` files under `C:\Windows\Prefetch`, and replays page ranges via `readahead(2)` / `posix_fadvise(POSIX_FADV_WILLNEED)`.
2. **Session Memory Tuning**: Enables Kernel Samepage Merging (`MemoryKSM=yes`) for the Wine session with adaptive scan-rate control and configures compressed RAM swap via `systemd-zram-generator`.

```
Wine process (64-bit or WoW64 32-bit)
  ntdll/unix/virtual.c: map_image_into_view()
    │  PE has FileAlignment < 0x1000
    ├─ Tier 1: D-Bus org.wineexplorer.Prefetch1 (pass open PE fd, never a path)
    │            │
    │            ▼
    │     wexprefetchd (user: wexcache, system bus)
    │       1. fstat(fd) -> (st_dev, st_ino, st_size, st_mtim_ns, st_ctim_ns)
    │       2. Lookup identity -> SHA-256 memo; on miss, read fd & compute SHA-256
    │       3. Materialize 4 KiB-aligned image at /var/cache/wineexplorer/images/<sha256>
    │       4. Record trace -> C:\Windows\Prefetch\<EXENAME>-<HASH>.pf + replay sidecar
    │       5. Return O_RDONLY cache fd -> caller mmap(MAP_PRIVATE)
    │
    ├─ Tier 2 (daemon absent): per-user $XDG_CACHE_HOME/wine/pe_section_cache
    └─ Tier 3 (cache error/disabled): stock Wine mprotect + pread fallback
```

---

## 3. Detailed Design

### 3.1 System Daemon (`wexprefetchd`) and D-Bus Contract

- **Daemon binary**: `/usr/local/sbin/wexprefetchd`, managed by `wexprefetchd.service`.
- **Dedicated account**: System user and group `wexcache` (`useradd --system --user-group --home-dir /var/cache/wineexplorer --shell /usr/sbin/nologin wexcache`).
- **Bus name & object**: System bus `org.wineexplorer.Prefetch1`, object path `/org/wineexplorer/Prefetch1`, interface `org.wineexplorer.Prefetch1`.
- **Authorization**: No polkit action is required. Callers must already hold an open readable file descriptor to the source PE binary; returning a read-only file descriptor to a page-aligned transformation of those exact bytes grants no new privilege.

#### D-Bus Methods (`org.wineexplorer.Prefetch1`)

| Method | Signature | Description |
|---|---|---|
| `GetAlignedImage` | `h -> h, s, u` | Input: open `O_RDONLY` fd (`h`) to source PE. Output: `O_RDONLY` fd (`h`) to aligned cache image, hex SHA-256 (`s`), layout version (`u`). |
| `NotifyProcessLaunch` | `s, h, a(uu)` | Input: UTF-8 uppercased NT image path (`s`), main exe fd (`h`), initial `(rva, size)` ranges (`a(uu)`). Triggers trace recording and async readahead of prior sidecar entries. |
| `ReplayBootPrefetch` | `-> u` | Replays `NTOSBOOT-B00DFAAD.pf` sidecar at session login/boot if `EnablePrefetcher` is `2` or `3`. Returns number of files prefetched (`u`). |

### 3.2 FD-Passing Protocol and Content-Addressed Cache

1. **Client invocation**: In `dlls/ntdll/unix/virtual.c` (`map_image_into_view`), when an image has `FileAlignment < page_size` (or any section with `(PointerToRawData & (page_size - 1)) != (VirtualAddress & (page_size - 1))`), the Unix side calls `org.wineexplorer.Prefetch1.GetAlignedImage` over a tiny `dlopen`'d `libdbus-1` (or `libsystemd` `sd-bus`) client helper (following `docs/DBUS_PLAN.md` and `wexhost`'s runtime-loaded D-Bus pattern).
2. **Never pass paths**: The client passes its already-open `unix_handle` fd (`DBUS_TYPE_UNIX_FD`), **never a filesystem path**. This prevents confused-deputy reads, symlink races, and chroot/namespace mismatches.
3. **Kernel-supplied identity**: `wexprefetchd` calls `fstat(fd)` on the received descriptor and extracts the 7-tuple:
   `(st_dev, st_ino, st_size, st_mtim.tv_sec, st_mtim.tv_nsec, st_ctim.tv_sec, st_ctim.tv_nsec)`.
4. **Memo lookup & hashing**:
   - `wexprefetchd` consults its bounded `(identity -> sha256)` memo table.
   - On a memo hit where `/var/cache/wineexplorer/images/<sha256>` exists and passes a size/header sanity check, it immediately opens the cache file `O_RDONLY | O_CLOEXEC` and returns the fd.
   - On a miss, `wexprefetchd` reads the PE file from `fd` via `pread()`, computes the SHA-256 digest of the source file **itself** (never trusting any client-supplied hash), and verifies `fstat(fd)` after reading to ensure the file was not modified in flight.
5. **Aligned image construction**:
   - If `/var/cache/wineexplorer/images/<sha256>` does not yet exist, the daemon writes a temporary file `/var/cache/wineexplorer/images/.tmp.<sha256>.<tid>` (mode `0644`, owner `wexcache:wexcache`) where every PE section raw offset is placed at its page-aligned RVA offset (`VirtualAddress` aligned to 4096 bytes) so a single file offset alignment matches `map_image_into_view`'s RVA layout, `fsync()`s it, and atomically `rename()`s it to `/var/cache/wineexplorer/images/<sha256>`.
   - Directory `/var/cache/wineexplorer/images` has mode `0755`, owner `wexcache:wexcache`.
6. **Client mapping**: The client receives the read-only cache fd and maps each section (or the contiguous image) with `mmap(..., MAP_PRIVATE | MAP_FIXED, cache_fd, aligned_offset)`. Clean pages are shared across all Wine processes and users through the Linux page cache; base relocations and writable data pages remain private copy-on-write (`MAP_PRIVATE`).
7. **Cache-poisoning immunity**: Because cache filenames are the SHA-256 of the source bytes and only `wexcache` can write `/var/cache/wineexplorer/images`, an unprivileged user who modifies a local copy of a DLL simply gets a different SHA-256 entry and cannot alter the cached image mapped by other users or elevated processes.

### 3.3 Three-Tier Fallback Chain

1. **Tier 1 (`wexprefetchd` via D-Bus)**: System-wide content-addressed cache in `/var/cache/wineexplorer/images`. Shared across all users and prefixes.
2. **Tier 2 (In-process per-user cache)**: Used when `wexprefetchd` is absent, stopped, or unreachable. Writes aligned section/image files under `$XDG_CACHE_HOME/wine/pe_section_cache` (default `~/.cache/wine/pe_section_cache`, mode `0700`). Derived from the Argon prototype (`ChromiumLab/tools/wine-pe-section-cache.patch`) with three required hardening fixes:
   - Include nanosecond `st_mtim` (`tv_sec`, `tv_nsec`) and `st_ctim` (`tv_sec`, `tv_nsec`) in the cache key alongside `st_dev`, `st_ino`, and `st_size`.
   - Guard cache file creation with `flock(LOCK_EX)` on a lockfile and re-check for the target file after acquiring the lock (writing to a sibling temp file + atomic `rename`) so 8 simultaneous renderer launches do not corrupt or double-write the entry.
   - Enforce an LRU size cap (default 2 GiB per user) by evicting oldest entries by `st_atim`/`st_mtim`.
3. **Tier 3 (Stock Wine `pread` path)**: Used when caching is disabled (`WINE_PE_CACHE=0`), disk space is exhausted (`ENOSPC`), or a PE header is non-standard. Preserves exact stock Wine `mprotect` + `pread` behavior.

### 3.4 Storage Backing Decision

`/var/cache/wineexplorer/images` **must remain on an ordinary on-disk filesystem** (ext4/btrfs on the root partition), **NOT `tmpfs` and NOT a `zram`-backed filesystem**:

- **Ordinary disk file**: Clean `MAP_PRIVATE` pages mapped from disk are standard VFS page cache. Under memory pressure, the kernel drops clean executable pages for free and faults them back from disk on demand.
- **Why not `tmpfs`**: `tmpfs` is backed by `shmem`. Pages in `tmpfs` cannot be discarded without being written to swap; placing 1–2 GB of aligned DLLs in `tmpfs` permanently pins RAM or swap.
- **Why not a `zram`-backed filesystem**: Mounting a filesystem on `/dev/zramX` keeps the compressed block data in RAM *and* populates an uncompressed copy in the VFS page cache when `mmap`ped, storing hot code twice in memory.
- **Disk compression**: Filesystem-level compression (`btrfs` with `compress=zstd` or a prebuilt EROFS image) is optional and unnecessary right now because disk capacity is not the bottleneck (37 GB free on the guest root after prefix cleanup).

### 3.5 Memory Tuning Half: KSM and `zram` Swap

Even with shared PE code pages, each Edge/Chromium renderer retains ~350 MB of resident anonymous read-write memory (V8 cages, PartitionAlloc metadata, zero/template pages).

#### Kernel Samepage Merging (KSM)
- **Session enablement**: Set `MemoryKSM=yes` on the WineExplorer session systemd unit / LightDM session scope (**VERIFIED**: `systemd` 257 on Debian 13 accepts `MemoryKSM=yes`; **VERIFIED** on Linux kernel 6.18 that `prctl(PR_SET_MEMORY_MERGE, 1)` survives both `fork()` and `execve()`, so `wineserver`, `explorer.exe`, and all Wine/Chromium/Edge descendant processes inherit KSM eligibility automatically without patching Wine).
- **Kernel activation**: `wexprefetchd` writes `1` to `/sys/kernel/mm/ksm/run` at startup when KSM is enabled.
- **Adaptive scan tuning**:
  - *Option A (`ksmtuned`-style loop)*: `ksmtuned` (GPL-2.0-or-later, `github.com/ksmtuned/ksmtuned`, originally Red Hat) is a small shell control loop that adjusts `/sys/kernel/mm/ksm/pages_to_scan` and `sleep_millisecs` from free-memory thresholds. Because upstream `ksmtuned` hardcodes `qemu-kvm` process RSS checks, we either adapt its process selector to sum Wine/Chromium processes (`wineserver`, `*.exe`) or run the equivalent threshold loop inside `wexprefetchd`'s 10-second housekeeping timer.
  - *Option B (Kernel built-in scan-time advisor — zero-code alternative)*: Linux's built-in KSM advisor (`docs.kernel.org/admin-guide/mm/ksm.html`) can be enabled by writing `scan-time` to `/sys/kernel/mm/ksm/advisor_mode` (defaults: `advisor_max_cpu = 70`, `advisor_target_scan_time = 200` s, `advisor_min_pages_to_scan = 500`, `advisor_max_pages_to_scan = 30000`). `wexprefetchd` prefers `advisor_mode=scan-time` when the sysfs knob is present and falls back to the internal `ksmtuned`-style loop otherwise.
- **Security note (Side-channel boundary)**: Cross-process KSM deduplication has known memory-timing side-channel implications (deduplication attacks across untrusted workloads). KSM is therefore a **documented opt-in setting that defaults to enabled for the single-user WineExplorer desktop session** and can be disabled by an administrator via `/etc/wineexplorer/prefetch.conf` (`EnableKSM=false`), which sets `/sys/kernel/mm/ksm/run=2` (unmerge and stop) and skips `MemoryKSM=yes`.

#### Compressed RAM Swap (`zram`)
- The validated 8 GiB guest has **no swap** and thrashed under normal-window Edge (`docs/EDGE_DEVELOPMENT_RUN_20261101.md`).
- Package `systemd-zram-generator` (MIT license; version `1.2.1` in Debian 13) provides declarative `zram` swap via `/etc/systemd/zram-generator.conf`:
  ```ini
  [zram0]
  zram-size = min(ram / 2, 4096)
  compression-algorithm = zstd
  swap-priority = 100
  # Optional backing partition/file for cold incompressible pages:
  # writeback-device = /dev/disk/by-partlabel/wex-zram-wb
  ```

### 3.6 Prefetcher Half (Windows XP Names and Registry Contract)

To match Windows XP / Server 2003 (`CLAUDE.md`):

- **Registry key**: `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management\PrefetchParameters`
- **Value**: `EnablePrefetcher` (`REG_DWORD`, default `3`):
  - `0`: Disabled (neither record nor replay prefetch traces; aligned PE cache remains active unless separately disabled).
  - `1`: Application launch prefetching only.
  - `2`: Boot / session logon prefetching only.
  - `3`: Both application launch and boot prefetching (Windows XP default).
- **Folder**: `C:\Windows\Prefetch`, mapped in the shared machine root at `/var/lib/wineexplorer/machine/drive_c/windows/Prefetch` (with user prefixes symlinking `drive_c/windows/Prefetch` into the machine root, matching `docs/PREFIX_SPLIT.md` and `docs/ELEVATION.md`).
- **Recording and Replay**:
  - When a Wine process starts, `wexprefetchd` logs the executable and the PE/data files opened during the first 10 seconds of execution, plus optional `mincore()` residency bitmaps sampled from the aligned cache mappings at `t = 2 s` and `t = 10 s`.
  - On subsequent launches (or at session startup for boot traces), `wexprefetchd` issues `readahead(2)` / `posix_fadvise(fd, offset, len, POSIX_FADV_WILLNEED)` on the recorded page ranges.
  - Following `ureadahead`, `wexprefetchd` inspects `/sys/dev/block/<maj>:<min>/queue/rotational`: it performs parallel window readahead on non-rotational media (`rotational == 0`, SSD/virtio) and skips or serializes readahead on rotational disks (`rotational == 1`) to avoid seek storms.
  - Traces unused for **30 days (~1 month)** are expired during periodic cleanup, keeping `C:\Windows\Prefetch` capped at 128 `.pf` files (the Windows XP limit).

---

## 4. Trace Formats (Verified Research and Decision)

### 4.1 Windows Prefetch (`.pf`) Format
Per the `libyal/libscca` specification (*Windows Prefetch File (PF) format*, GFDL-1.3+ documentation):
- **Format versions**:
  - `17` (`0x00000011`): Windows XP and Windows Server 2003 — **uncompressed**, magic `'SCCA'` (`0x41434353`) at byte offset 4, 84-byte file header.
  - `23` (`0x17`): Windows Vista / 7.
  - `26` (`0x1A`): Windows 8 / 8.1.
  - `30` (`0x1E`): Windows 10; `31` (`0x1F`): Windows 11 (wrapped in an 8-byte `'MAM\x04'` header with LZXPRESS Huffman compression).
- **Version 17 layout**:
  - Bytes `0..83`: Header (`uint32` version = 17, `uint32` `'SCCA'`, `uint32` unknown `0x0000000f`, `uint32` file size, 60-byte UTF-16LE executable basename padded with NULs, `uint32` prefetch hash, `uint32` flags).
  - Bytes `84..151`: 68-byte File Information block containing offsets and entry counts for four sections:
    - **Section A (File Metrics Array)**: 20 bytes per entry (start time, duration, filename string offset/length, flags).
    - **Section B (Trace Chains Array)**: 12 bytes per cluster chain entry.
    - **Section C (Filename Strings)**: Concatenated NUL-terminated UTF-16LE NT device paths (`\DEVICE\HARDDISKVOLUME1\WINDOWS\SYSTEM32\NTDLL.DLL`).
    - **Section D (Volume Information)**: Volume device path, serial number, creation time, and directory strings.

### 4.2 Decision: Clean-Room v17 `.pf` Writer + Private Replay Sidecar

1. **User-visible `.pf` files**: `wexprefetchd` writes **version-17 `.pf` files** named `<EXENAME>-<8HEXHASH>.pf` (and `NTOSBOOT-B00DFAAD.pf` for boot) in `C:\Windows\Prefetch`. Version 17 is uncompressed, matches our Windows XP / Server 2003 target (`CLAUDE.md`), and is directly readable by standard forensic and inspection tools.
2. **Licensing rule**: We implement the v17 writer from the documented binary layout ourselves and **MUST NOT copy code from `libscca`**. `libscca` source code is LGPL-3.0-or-later, which would turn the containing GPL-2.0-or-later module into GPL-3.0 (`CLAUDE.md`). Implementing from the factual format description (GFDL-1.3+ doc, cited in §11) keeps all WineExplorer code GPL-2.0-or-later.
3. **PF filename hash**: The 32-bit hash in `<EXENAME>-<HASH>.pf` must use the XP SCCA hash function defined in the `libscca` documentation over the uppercased UTF-16LE NT path.
4. **Private replay sidecar**: Because v17 `.pf` files store NT file lists and FAT/NTFS cluster trace chains rather than Linux content-addressed cache hashes and 4 KiB page ranges, `wexprefetchd` stores a small private binary sidecar per `.pf` under `/var/cache/wineexplorer/traces/<EXENAME>-<HASH>.pf.sidecar`:
   - Header: magic `'WXP1'`, `uint32` version (`1`), `uint8` rotational flag, `uint64` `last_used_unix_sec`, `uint32` entry count.
   - Entries: `(uint8 sha256[32], uint64 dev, uint64 ino, uint32 range_count, struct { uint32 start_page; uint32 page_count; } ranges[])`.
5. **Why not reuse an existing Linux readahead format**:
   - `ureadahead`'s pack file is GPL-2.0-only and an unstable internal struct dump.
   - `systemd-readahead` was removed upstream in `systemd` v217.
   - `preload` uses an internal Markov-state text file (`/var/lib/preload/preload.state`), not per-app page traces.
   - Android `iorapd` (Apache-2.0) uses `perfetto` + Protobuf schemas.
   - None is a reusable standard; we borrow only the best ideas: per-mount rotational detection and ~1-month trace expiry from `ureadahead`, and application-correlation ideas from `preload`.

---

## 5. Threat Model and Security Boundaries

| Threat / Vector | Mitigation |
|---|---|
| **Confused-deputy file read** (client asks daemon to read `/etc/shadow` or another user's private file) | Daemon accepts **only** `DBUS_TYPE_UNIX_FD` (`GetAlignedImage`), never file paths. The caller had to open the file itself with `FILE_READ_DATA` / `O_RDONLY`. |
| **Cross-user cache poisoning** (User A mutates a DLL or passes a fake hash to corrupt User B's process) | Daemon ignores client claims, reads bytes from the passed fd, computes SHA-256 itself, verifies post-read `fstat()`, and names the cache file by `<sha256>`. Returned fd is `O_RDONLY` on a `0644` `wexcache:wexcache` file mapped `MAP_PRIVATE`. |
| **In-flight mutation (TOCTOU) while hashing** | Daemon streams the source fd into the temporary cache buffer while hashing; if bytes read or post-read `(st_size, st_mtim, st_ctim)` differ from pre-read `fstat()`, the temp file is unlinked and the call fails closed. |
| **Disk / CPU DoS per UID** | Daemon queries caller UID via D-Bus credentials (`GetConnectionUnixUser`) and enforces: max source PE size `512 MiB`, max `2` concurrent builds per UID, token-bucket rate limit (`30` misses/min/UID), and global `/var/cache/wineexplorer/images` cap (`4 GiB` default, LRU eviction). |
| **Machine root (`wexsys`) isolation** | `/var/lib/wineexplorer/machine` stays `root:wexsys` (`docs/ELEVATION.md`). Only the subdirectory `drive_c/windows/Prefetch` is owned `wexcache:wexsys` (`2775`), confining `wexprefetchd` with `ProtectSystem=strict` and `ReadWritePaths` to that single folder and `/var/cache/wineexplorer`. |
| **KSM side-channel exposure** | Documented opt-in (default on for single-user desktop session); admin kill-switch in `/etc/wineexplorer/prefetch.conf`. |

---

## 6. Failure Modes and Recovery

- **Daemon stopped, crashed, or D-Bus timeout (>200 ms on hit / >5 s on miss)**: Client logs a `WARN_(virtual)` once per process and falls back to Tier 2 (`$XDG_CACHE_HOME/wine/pe_section_cache`) or Tier 3 (`pread`). Application launch never fails because of `wexprefetchd`.
- **`ENOSPC` / quota reached**: `wexprefetchd` unlinks any partial `.tmp.*` file, runs immediate LRU eviction down to 80% of `MaxCacheBytes`, and if space is still insufficient returns `org.wineexplorer.Prefetch1.Error.NoSpace`; client uses Tier 3 `pread`.
- **Corrupt / truncated PE headers**: Strict bounds validation on `IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS`, and `IMAGE_SECTION_HEADER` table (`NumberOfSections <= 96`, no integer overflow on `PointerToRawData + SizeOfRawData`). Invalid files return `org.wineexplorer.Prefetch1.Error.InvalidPE` and fall back to stock Wine loader handling.
- **Unclean shutdown**: On startup, `wexprefetchd` sweeps and unlinks stale `.tmp.*` files in `/var/cache/wineexplorer/images` and `/var/cache/wineexplorer/traces`.

---

## 7. Observability

- **Service health & logs**: `systemctl status wexprefetchd.service` and `journalctl -u wexprefetchd.service` (logs cache builds, SHA-256, build latency in ms, LRU evictions, and KSM advisor state).
- **D-Bus Properties (`org.freedesktop.DBus.Properties` on `/org/wineexplorer/Prefetch1`)**:
  - `CacheHits` (`t`), `CacheMisses` (`t`), `CacheEntries` (`u`), `CacheBytesUsed` (`t`), `BytesSavedEstimate` (`t`, computed as `(CacheHits * aligned_section_bytes)`).
  - `EnablePrefetcher` (`u`), `KsmPagesShared` (`t`), `KsmPagesSharing` (`t`, read from `/sys/kernel/mm/ksm/pages_sharing`).
  - Queryable with `busctl get-property org.wineexplorer.Prefetch1 /org/wineexplorer/Prefetch1 org.wineexplorer.Prefetch1 CacheHits BytesSavedEstimate`.

---

## 8. Packaging and Installer Integration

| Artifact | Installed Path | Purpose |
|---|---|---|
| Daemon unit | `/usr/lib/systemd/system/wexprefetchd.service` | Runs as `User=wexcache`, `Group=wexcache`, `SupplementaryGroups=wexsys`, `NoNewPrivileges=yes`, `ProtectSystem=strict`, `ProtectHome=yes`. |
| Session KSM drop-in | `/usr/lib/systemd/system/lightdm.service.d/wex-ksm.conf` | Sets `MemoryKSM=yes` so the desktop session and all Wine children inherit `PR_SET_MEMORY_MERGE`. |
| D-Bus policy | `/usr/share/dbus-1/system.d/org.wineexplorer.Prefetch1.conf` | Allows `user="wexcache"` to own `org.wineexplorer.Prefetch1`; allows any local user to call `GetAlignedImage` / `NotifyProcessLaunch` and read properties. |
| `tmpfiles.d` config | `/usr/lib/tmpfiles.d/wineexplorer-prefetch.conf` | `d /var/cache/wineexplorer/images 0755 wexcache wexcache -` <br> `d /var/cache/wineexplorer/traces 0750 wexcache wexcache -` <br> `d /var/lib/wineexplorer/machine/drive_c/windows/Prefetch 2775 wexcache wexsys 30d` |
| `zram` config | `/etc/systemd/zram-generator.conf` | Configures `zram0` swap (`zstd`, `min(ram / 2, 4096)`). |

**Installer integration (`docs/INSTALLER.md`)**:
- `scripts/setup-machine-root.sh`: Creates system user/group `wexcache` alongside `wexsys` and preserves `wexcache:wexsys` (`2775`) on `drive_c/windows/Prefetch` during `--apply` and `--update`.
- `installer/build-iso.sh` & `0500-enable-services.hook.chroot`: Installs `systemd-zram-generator`, stages the unit/D-Bus/`tmpfiles.d` files, and links `wexprefetchd.service` into `multi-user.target.wants`.

---

## 9. Phased Implementation and Test Plan

### Phased Plan
- **P1 — In-process cache + hardening**: Land Tier 2 in `dlls/ntdll/unix/virtual.c` (`docs/STOCK_CHANGES.md`) with nanosecond `st_mtim`/`st_ctim` keys, `flock` + re-check atomic creation, and LRU size cap.
- **P2 — `wexprefetchd` daemon + D-Bus client**: Implement `wexprefetchd` (`GetAlignedImage`, SHA-256 content addressing in `/var/cache/wineexplorer/images`, D-Bus properties) and Tier 1 client in `ntdll`/`wexhost`.
- **P3 — Prefetch traces, readahead, registry & UI**: Implement v17 `.pf` writer in `C:\Windows\Prefetch`, private replay sidecar, `readahead(2)` on non-rotational block devices, and `EnablePrefetcher` registry support.
- **P4 — KSM & `zram` tuning**: Enable `MemoryKSM=yes` on the session unit, configure kernel `scan-time` advisor / `ksmtuned` loop in `wexprefetchd`, and ship `/etc/systemd/zram-generator.conf`.

### Verification and Test Plan
1. **PSS reduction measurement (`ChromiumLab` `mem-who` method)**: Run normal-window Edge (8 renderers) in the guest and collect `/proc/<pid>/smaps_rollup` by role at `t = 10 s`. Verify renderer PSS drops from ~5.9 GB to ~4.0 GB without modifying `msedge.dll` on disk, and verify normal-window Edge completes validation in the **8 GiB guest** without thrashing.
2. **Page-cache residency (`vmtouch` / `fincore`)**: Run `fincore /var/cache/wineexplorer/images/<sha256>` (or `vmtouch -v`) before and after launching Edge and after `ReplayBootPrefetch`; verify shared file-backed mapping (`smaps` shows `/var/cache/wineexplorer/images/<sha256>` instead of anonymous `r-xp`).
3. **32-bit WoW64 regression check (`CLAUDE.md`)**: Build a 32-bit PE test binary (`i686-w64-mingw32-gcc -Wl,--file-alignment,0x200`) and run it under the WoW64 build (`--enable-archs=i386,x86_64`); verify it maps through the cache and executes cleanly.
4. **Cache-poisoning negative test**: Have unprivileged User A open a modified copy of a shared DLL (or race a file mutation during `GetAlignedImage`); verify User B's mapping of the canonical DLL receives a distinct SHA-256 fd and its bytes are unmodified.
5. **Daemon-absent fallback test**: Stop `wexprefetchd.service` and verify that launching 64-bit and 32-bit applications falls back cleanly to Tier 2 (`$XDG_CACHE_HOME/wine/pe_section_cache`) and Tier 3 (`pread` when `WINE_PE_CACHE=0`).

---

## 10. Open Questions and Unverified Items

1. **Renderer ~350 MB RW anonymous region (`UNVERIFIED`)**: Each Edge renderer still holds a ~350 MB resident RW anonymous mapping (`docs/EDGE_DEVELOPMENT_RUN_20261101.md`). Its exact composition (V8 pointer-compression cage vs PartitionAlloc) and the exact MB saved by `MemoryKSM=yes` on that region are **UNVERIFIED** until measured on the guest.
2. **Direct `ntdll.so` D-Bus connection vs `wexhost`/unix socket (`UNVERIFIED`)**: Whether opening a private D-Bus connection per process in `ntdll.so` adds measurable startup latency compared to caching the bus connection per process or routing through `wineserver` is **UNVERIFIED** and will be benchmarked in P2.
3. **Live `EnablePrefetcher` reload**: `system.reg` is flushed by `wineserver`; `wexprefetchd` can watch `/var/lib/wineexplorer/machine/system.reg` with `inotify` or accept a property update from `wexhost` when the registry value changes.

---

## 11. Prior Art and Licences

| Project / Specification | Licence | Relevance and Decision |
|---|---|---|
| `preload` (Behdad Esfahbod) | GPL-2.0-or-later | Adaptive Markov model predicting which binaries/libraries follow which; borrow correlation concept only (state format is internal). |
| `ureadahead` (Canonical) | GPL-2.0-only | Borrow per-mount pack concept, `/sys/.../queue/rotational` SSD vs HDD check, and ~1-month trace expiry. Pack format is internal/GPL-2.0-only; do not reuse code. |
| `systemd-readahead` | LGPL-2.1-or-later | Removed upstream in `systemd` v217 (2014); historical reference only. |
| `iorapd` (Android) | Apache-2.0 | Uses `perfetto` + Protobuf (`TraceFileProto`); too heavy and Apache-2.0 is incompatible with GPL-2.0-only paths (`CLAUDE.md`). |
| `ksmtuned` (Red Hat) | GPL-2.0-or-later | Small shell loop tuning `pages_to_scan` / `sleep_millisecs` from free RAM thresholds; adapt process selection from `qemu` to Wine/Chromium or use kernel `scan-time` advisor. |
| `systemd-zram-generator` | MIT | Packaged in Debian 13 (`1.2.1`); used directly via `/etc/systemd/zram-generator.conf` (`[zram0]`, `zstd`). |
| `libyal/libscca` | Code: LGPL-3.0-or-later <br> Docs: GFDL-1.3-or-later | Cite `Windows Prefetch File (PF) format.asciidoc` for the v17 (`SCCA`) layout and filename hash function. **Never copy `libscca` code** so our module stays GPL-2.0-or-later. |
| `composefs` / `OSTree` | GPL-2.0-or-later / LGPL-2.0-or-later | Inspiration for content-addressed backing files (`/var/cache/wineexplorer/images/<sha256>`) shared across processes and users via the kernel page cache. |

Verified (2026-11-02): `make check-daemon` 40/40 and `test_trace.py` 20/20 (also under ASan/UBSan), covering the private
directory ACL (the owner reads, another uid cannot read or list; run as root with `setuid`), the hint, the trace fetch and
its once-only delivery. In the QEMU guest the daemon created `/var/cache/wineexplorer/prefetch/1000` with
`user:debian:r-x`; a second `chrome.exe --version` from `/home/debian` (which the service cannot read) logged
`asked the kernel to read ahead 3 files of the previous run`; a Chromium start still passes, 64-bit and 32-bit.
Not measured: the benefit of the client-side readahead on a cold disk.
