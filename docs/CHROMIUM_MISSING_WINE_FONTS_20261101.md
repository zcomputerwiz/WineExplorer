<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium's browser dies at start when the Wine runtime has no Tahoma

Date: 2026-11-01. Status: cause found and proven; the repair is in the runtime package, not in Wine. With the
[first thread data alignment](CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md) fix in, the R6 runtime package (Wine base
`66caeb46`) still failed, because it ships none of Wine's six TrueType fonts. The same Wine commits plus those fonts load
the fixture page in the lab VM (`chromium-prep-tfont1-20261101`). The third failure of the series, a stock Wine gap that
broke the network service's sandbox profile on a fresh prefix, is in
[CHROMIUM_APPCONTAINER_LABEL_20261101.md](CHROMIUM_APPCONTAINER_LABEL_20261101.md). None of the three is rendering,
security or token acceptance of Chromium.

## Result

- With the stall fixed the R6 runtime still fails: about two seconds after the start the browser's main thread overflows
  its stack and the fixture server never sees a request (`chromium-prep-tfix1-20261101`, `chromium-prep-tdiag1-20261101`).
- The cause is the package, not the code. The R6 runtime's `fonts` directory holds the 50 bitmap `.fon` files and a Makefile
  but none of Wine's six TrueType fonts (Marlett, Symbol, Tahoma, Tahoma Bold, Webdings, Wingdings). The R5 runtimes carry all
  six.
- Without `tahoma.ttf` DirectWrite has no Tahoma family, and Wine's font fallback (Latin text maps only to Tahoma) returns
  success with no font. Skia then cannot create Chromium's default typeface, and Chromium's font initialization calls itself
  until the 8 MB main thread stack is gone.
- Only Tahoma matters. In a local matrix `tahoma.ttf` alone is enough, the other four font files without it are not, and a
  complete host font set (244 families) without Tahoma is not either (below).
- Repair: ship the six real files (not links) in the runtime's `fonts` directory, as stock Wine installs them and as the R5
  packages did, and check the package with `scripts/check-runtime-fonts.py`. No Wine change, so no `STOCK_CHANGES.md` row.
- Candidate `tfont` (R6 runtime, the three Wine fixes and the six fonts) was run in the VM: `index.html` and `scene.js` are
  served with 200, the network service is sandboxed and there is no stack overflow.

How to recognize it: the fixture server sees no request, `chromium.log` stays near 19.5 KB (a good run writes 84 to 94 KB)
and at the end of the 40 s window only the crashpad handler and the GPU process are left.

## Evidence from the lab VM

| Run | Runtime | Result |
|---|---|---|
| `chromium-prep-tfix1-20261101` | R6 runtime and `2ebf5f72` | no sampler stall any more; the main thread dies about 2 s after the start; no fixture request |
| `chromium-prep-tdiag1-20261101` | R6 runtime, `2ebf5f72`, `1cc71ccd`, `67317644` and a trace-only diagnostic patch in ntdll (not kept) | the main thread raises `c00000fd` (stack overflow) at `rsp 0x21fe0`, 0x1fe0 above the bottom of its 8 MB stack (0x20000 to 0x820000), and the thread is then aborted (`abort_thread status 1`) |
| `chromium-prep-tfont1-20261101` | R6 runtime, the same three commits and the six fonts | works (below) |

Chromium's frames on the overflowing stack, symbolised with the pinned build's PDB: nine return addresses repeat every
0x6a0 bytes, about 4,900 times. Five are the font code's own call cycle, the other four land in `malloc`, string-to-int
conversion, a trace event helper and `SystemFonts::AddFont`:

| Function | Source line (pinned build) |
|---|---|
| `gfx::win::GetSystemFont`, with `SystemFonts::GetFont` and `SystemFonts::Initialize` inlined | `ui/gfx/system_fonts_win.cc` 269, 31, 194 |
| `SystemFonts::GetFontFromLOGFONT` | `ui/gfx/system_fonts_win.cc` 112 |
| `PlatformFont::CreateFromNameAndSize` and `PlatformFontSkia::PlatformFontSkia` | `ui/gfx/platform_font_skia.cc` 511, 115 |
| `PlatformFontSkia::InitFromDetails`, with `GetDefaultFont` inlined | `ui/gfx/platform_font_skia.cc` 393, 52 |
| `PlatformFontSkia::EnsuresDefaultFontIsInitialized` | `ui/gfx/platform_font_skia.cc` 166 |

The deepest frames at the moment of overflow are in Wine's DirectWrite (symbolised with the unstripped build, whose
`dwrite.dll` is byte-identical to the one in the staged runtimes of `tdiag1` and `tfont1`; the local runs below stop at
`dwrite.dll` offset 0x35414, and the VM trace has the same low 16 bits): `localizedstrings_GetString` (`dlls/dwrite/main.c:426`),
called from `collection_find_family` (`font.c:3331`), `dwritefontcollection_FindFamilyName` (`font.c:3347`),
`create_matching_font` (`analyzer.c:2603`) and `fontfallback_MapCharacters` (`analyzer.c:2652`).

## Mechanism

1. Chromium builds its system fonts from the non-client metrics (`ui/gfx/system_fonts_win.cc`): `SystemFonts::Initialize()`
   reads `SPI_GETNONCLIENTMETRICS` and `GetFontFromLOGFONT()` asks `PlatformFont::CreateFromNameAndSize()` for the face name
   it finds there. On Wine that name is "Tahoma", Wine's default UI font (the probe prints `system font message: "Tahoma"`).
2. `PlatformFontSkia::InitFromDetails()` (`ui/gfx/platform_font_skia.cc`) asks Skia for that typeface. When Skia returns none
   it calls `EnsuresDefaultFontIsInitialized()` and copies `GetDefaultFont()`. The default font is the system message font,
   which comes from the very `SystemFonts::Initialize()` that is still running and not yet marked done, so it starts again,
   creates another `PlatformFontSkia`, fails again, and so on until the stack is gone.
3. Skia (`src/ports/SkFontMgr_win_dw.cpp`, `SkFontMgr_DirectWrite::onLegacyMakeTypeface()`) looks the family up by name; when
   it does not exist and the system has a font fallback, it asks `IDWriteFontFallback::MapCharacters()` and returns whatever
   comes back.
4. Wine's DirectWrite (`dlls/dwrite/analyzer.c`): `fontfallback_MapCharacters()` (2626) tries the base family (2652), then
   `fallback_map_characters()` (2519), which looks the text up in `system_fallback_config` (233 to 241). Its Latin and Cyrillic
   ranges map only to `Tahoma`; other scripts map to Noto families, and DejaVu or Liberation are not in the table, so host
   fonts are never used for Latin text. `create_matching_font()` fails for an absent family (`FindFamilyName` finds nothing, 2603
   to 2608), and `fallback_map_characters()` then returns `S_OK` with `*ret_font = NULL` for the whole run (2586). The probe
   prints this as `MapCharacters(' ', base "Tahoma"): hr=00000000 mapped=1 font=0000000000000000`. Skia gets no typeface and
   step 2 never ends.
5. Where Tahoma can come from (`dlls/win32u/font.c`): `font_init()` (6729) loads the bitmap fonts, then
   `load_file_system_fonts()` (6532: `C:\windows\fonts`, then the runtime's own `fonts` directory through
   `get_fonts_data_dir_path()` (473) flagged `ADDFONT_EXTERNAL_FONT`, then the paths in `HKCU\Software\Wine\Fonts\Path`), then
   fontconfig. DirectWrite builds its system collection from the font list that win32u registers. Fontconfig has no Tahoma
   substitute (its metric aliases cover Arial, Times New Roman, Courier New, Calibri and Cambria, see `ASSETS.md`), and the
   guest's host fonts are DejaVu and Liberation only, so Wine's own `tahoma.ttf` is the only source.

Native Windows has Tahoma. A Wine runtime without its own fonts is incomplete, which is why the repair is in the package and
Wine's fallback stays as it is. A terminal stack overflow ends only the faulting thread in Wine (`abort_thread`); that is not
changed either.

## Local matrix

Each case ran Chromium from the harness snapshot with the sandbox on and a local logging fixture server for 40 s, on the staged
`tdiag` runtime (the three Wine commits and the trace-only diagnostic patch), `LANG=C.UTF-8`, in a fresh copy of one pristine
prefix that was created with no Wine TrueType fonts. The runtime's `fonts` directory held exactly the files listed. Host
fonts: "none" is a fontconfig file without font directories, "guest-like" adds only the two directories the guest's packages
fill (DejaVu and Liberation), "complete" is the build machine's own fontconfig (244 families, none of them Tahoma).

| Case | Host fonts | Wine TrueType fonts | Fixture log lines | `chromium.log` | Main-thread stack overflow |
|---|---|---|---|---|---|
| e1a | none | none | 0 | 19,576 B | yes |
| e1b | none | all six | 4 | 89,929 B | no |
| e2a | guest-like | none | 0 | 19,573 B | yes (`rsp 0x21fe0`, same place as the VM) |
| e2b | guest-like | tahoma, tahomabd | 4 | 89,255 B | no |
| e2c | guest-like | marlett, symbol, webdings, wingding | 0 | 19,682 B | yes |
| e2d | guest-like | tahoma only | 4 | 88,234 B | no |
| e2s | complete | none | 0 | 19,963 B | yes (same place as e2a) |
| tfont-local | complete | all six, on the fixed build tree | 4 | 93,844 B | no |

Four log lines means `index.html` and `scene.js` served with 200 and the `favicon.ico` 404 with the server's own `code 404`
line. Every failing case ends with only crashpad and the GPU process; every passing case has the whole tree (browser,
crashpad, GPU, utility processes, renderers). Wine's own fonts alone are enough (e1b: no host fonts at all), `tahoma.ttf`
alone is enough (e2d), and nothing else is (e2c, e2s).

DirectWrite as seen by `scripts/dwrite-font-probe.c` (same environment, a fresh prefix copy per case; the 64-bit and the 32-bit
probe print the same):

| Case | Wine TrueType fonts | Host fonts | System collection | `FindFamilyName("Tahoma")` | `MapCharacters(" ")` | Registry font values (file exists / missing) |
|---|---|---|---|---|---|---|
| pa | none | guest-like | 6 families | no | NULL font | 28 (24 / 4) |
| pb | tahoma only | guest-like | 7 | yes (index 0) | a font | 29 (25 / 4) |
| pc | marlett, symbol, webdings, wingding | guest-like | 10 | no | NULL font | 32 (28 / 4) |
| pd | all six | guest-like | 11 | yes (index 2) | a font | 34 (30 / 4) |
| ps | none | complete | 236 | no | NULL font | 441 (437 / 4) |

DejaVu Sans and Liberation Sans exist in every case; Segoe UI, Arial and MS Shell Dlg never do. The four missing registry
files are the same four bitmap fonts (`coure.fon`, `sserife.fon`, `serife.fon`, `smalle.fon`) in every case.

## Why the R6 package has no fonts

- The lab guest has six host font families (`fc-list`: DejaVu Sans, Sans Mono, Serif, Liberation Mono, Sans, Serif from
  fonts-dejavu-core, fonts-dejavu-mono and fonts-liberation), so it never supplies a Tahoma.
- Runtime directories in the guest (survey through the lab harness):

  | Runtime | Entries in `fonts` | TrueType fonts |
  |---|---|---|
  | `wine-control-channel-20261027` | 57 | 6 |
  | `wine-verify-r5-20261029.replay-A-20261101` | 57 | 6 |
  | `wine-verify-r6-20261030.replay-TFONT1-20261101` | 57 | 6 |
  | `wine-verify-r6-20261030`, `wine-recovered-r6-20261031` and the replays B, CM, CM2, TDIAG1, TFIX1B | 51 | 0 |

  57 is 50 `.fon` files, the Makefile and the six TrueType fonts (Marlett 6,136 bytes, Symbol 26,028, Tahoma 143,884,
  Tahoma Bold 137,836, Webdings 4,300, Wingdings 9,524); 51 is the same without them.
- The R6 staging tree on the build machine already has 51 entries under `wow64/fonts` and no TrueType file, and the R6
  preflight record (`chromium-combined-r6-20261030.preflight/preflight.json`) lists the same 51. The deploy script
  (`scripts/g4_combined_r6_deploy.py` in the token integration worktree, not on this branch) packs whatever `build/wow64`
  holds and archives symbolic links as links. In an out-of-tree Wine build the six files are links into the source tree
  (`fonts/tahoma.ttf -> SRC/fonts/tahoma.ttf`), so a package step that does not follow links loses them or leaves them
  dangling. Where exactly the R6 package lost them is not established; the staged tree was already without them, not even
  as dangling links.
- Wine registers the six fonts into an existing prefix at its next start (`font_init`), so adding them to the runtime repairs
  a prefix that was made without them: the `tfont1` run used the guest's existing prefix.

## Candidate `tfont` in the VM

`chromium-prep-tfont1-20261101` was built from the R6 staging relpath set plus the six real files (copied with `cp -L`,
mode 0644) before `prepare`; its advapi32, wow64 and ntdll files are byte-identical to the fixed build, so it is the three
Wine commits and the six fonts and nothing else.

- Fixture server: four requests, an empty speculative connection, `index.html` (200, 637 B), `scene.js` (200, 3,750 B) and
  `favicon.ico` (404, 5.7 s after the start).
- `chromium.log` 84 KB, net log 821 KB, trace 13.5 MB. The network service ran with `--service-sandbox-type=network`;
  storage and tracing ran as `service`; the ProcessorMetrics service ran unsandboxed (`none`) by design.
- No `c00000fd`, stack overflow, `abort_thread` or "crashed or was terminated" line. The only `err:` line is
  `err:winediag:wined3d_dll_init Using the OpenGL renderer.`
- The harness verdict is `passed=false` (`journal_outcome native_timeout`, "Linux helper failed/exit mismatch",
  `diagnostic_nonacceptance_expected true`), the same as for the good R5 replay. The run is diagnostic evidence only.

## Recommended harness change

1. After `stage` and before `prepare`, copy the six real files into `<staging>/build/wow64/fonts` with `cp -L` (or put them
   in `share/wine/fonts` of an install tree), mode 0644. Do not re-run stage or prepare for a candidate that already has them.
2. Fail the preflight when `python3 scripts/check-runtime-fonts.py RUNTIME_ROOT` does not exit 0. It accepts a build tree
   (`fonts`) or an install prefix (`share/wine/fonts`) and rejects missing files, dangling links and files that are not
   TrueType. Checked: the fixed build tree and the `tfont` staging pass; the R6 staging fails with all six listed missing.
3. Optionally run `scripts/dwrite-font-probe.c` in the prefix as a canary: `FindFamilyName(Tahoma)` must say `exists=1` and the
   `MapCharacters` lines must not print a NULL font.

Do not work around this with host font packages or in Chromium: the complete host set did not help (`ps`, `e2s`).

## Reproduce

```
# 1. Is the package complete? (exit 0: all six TrueType fonts are real, readable files)
python3 scripts/check-runtime-fonts.py RUNTIME_ROOT

# 2. What does DirectWrite see? Run both probes in a prefix of that runtime.
x86_64-w64-mingw32-gcc -O2 -Wall -o dwrite-font-probe64.exe scripts/dwrite-font-probe.c -ldwrite -luuid
i686-w64-mingw32-gcc   -O2 -Wall -o dwrite-font-probe32.exe scripts/dwrite-font-probe.c -ldwrite -luuid
LANG=C.UTF-8 WINEPREFIX=PREFIX RUNTIME_ROOT/wine dwrite-font-probe64.exe
LANG=C.UTF-8 WINEPREFIX=PREFIX RUNTIME_ROOT/wine dwrite-font-probe32.exe
```

To reproduce the matrix, copy the runtime, leave exactly the wanted `*.ttf` in its `fonts` directory, start every case from a
fresh copy of one pristine prefix, and point `FONTCONFIG_FILE` at one of these (the "none" case is the same file without the two
`dir` lines and the `include`; "complete" leaves the variable unset):

```
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts/truetype/dejavu</dir>
  <dir>/usr/share/fonts/truetype/liberation</dir>
  <cachedir>/tmp/fontexp-fccache</cachedir>
  <include ignore_missing="yes">/etc/fonts/conf.d</include>
</fontconfig>
```

The local Chromium driver of E1 to E3 (`chrome.exe --app=` the fixture URL with the sandbox on, a logging fixture server and a
40 s window) was session scratch and is not kept; the VM runs were made with the token integration worktree's launcher
(`scripts/g4-virgl-launcher-session.ps1`).

## Open items

- The R6 harness must ship the fonts and run the guard (above). It lives in another worktree and was not changed.
- The `tfont` run used the guest's existing prefix. A fresh prefix also needs the AppContainer label fix, which was validated
  in local fresh-prefix runs only, so the whole chain on a fresh prefix has not run in the VM.
- The matrix ran on the staged diagnostic runtime and the local fixture harness; only the final candidate ran in the VM.
- Chromium source paths and function names were checked in the code index at a newer commit than the pinned build; the
  frames and line numbers in the table come from the pinned build's PDB.
- Wine's DirectWrite fallback returns no font when its only Latin target is missing. Left alone, because every complete runtime
  ships Tahoma; a fallback to any installed family would be a Wine change and needs a decision first.
