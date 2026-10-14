# Work order: native Vista–11 visual style reader in uxtheme

Status: ready, 2026-10-13 (decision: native reader, not a converter). Assignee: Gemini unless the user says
otherwise. Background: `docs/THEMES.md`. Reference implementation: libmsstyle in our fork
**`zcomputerwiz/msstyleEditor`** (MIT, C#; upstream `nptr/msstyleEditor`) — `libmsstyle/VisualStyle.cs`,
`PropertyStream.cs`, `StyleProperty.cs`, `VisualStyle{Part,State,Property,Enum,Animation}Map.cs`,
`ResourceAccess.cs`, `ImageConverter.cs`.

## Goal

Wine's uxtheme loads and draws Windows Vista, 7, 8, 10 and 11 `.msstyles` files directly, alongside the XP format it
reads today (`dlls/uxtheme/msstyles.c` rejects anything but `PACKTHEM_VERSION` 3, `MSSTYLES_VERSION 0x0003`).
Controls, menus, window frames (our themed non-client code) and the shell's theme classes then draw from a newer
style. Out of scope: Aero glass/blur (needs compositor blur, D3/D4), Windows 7/11 shell *layouts*, the Open-Shell
skin format (separate plans).

## The format (from libmsstyle; verify against real files)

A Vista+ style is a resource-only PE like XP's, with different resources:

| Resource (type / name) | Content |
|---|---|
| `CMAP` / `CMAP` | class names, UTF-16, in index order (properties refer to classes by index) |
| `VARIANT` / `NORMAL` (and other variants) | the **binary property stream**: records with a header (name id, type id, class index, part, state, flags, size) followed by typed data — ints, bools, colors, margins, sizes, positions, rects, strings, int lists, filenames → image ids, fonts (`LOGFONT`), enums, high-contrast color types, … (`PropertyStream.cs`, `StyleProperty.cs`) |
| `IMAGE` / id | PNG images (with alpha); several per part for DPI (`IMAGEFILE1`…`IMAGEFILE7`, `TMT_IMAGEFILE`, DPI properties) |
| `STREAM` / id | Windows 8+: an image **atlas**; parts reference sub-rectangles (`ATLASRECT`, `ATLASIMAGE`) |
| `AMAP` / `AMAP` | Windows 8+: animations (transitions, timing functions) |
| `MUI` / 1, string tables | display names |

libmsstyle tells Vista / 7 / 8.1 / 10 / 11 apart (`VisualStyle.cs`, platform detection) — the part/state/property
id tables differ by platform; port the tables as **data** (generated C tables from the `.cs` maps, with a generator
script kept in `tools/` or the fork, MIT notice retained).

## Design in Wine's uxtheme

- Keep `MSSTYLES_OpenThemeFile` as the entry point: detect the format (XP: `PACKTHEM_VERSION` 3 + `THEMES_INI`;
  Vista+: `CMAP` + `VARIANT`) and dispatch to the existing INI reader or a new reader (new file
  `dlls/uxtheme/msstyles_v4.c` or similar).
- The new reader fills the **existing structures** (`THEME_FILE`, `THEME_CLASS`, `THEME_PARTSTATE`,
  `THEME_PROPERTY`, `msstyles.h`) so `draw.c`, `property.c`, `metric.c` and every caller keep working.
  `THEME_PROPERTY` stores values as strings parsed on use (`lpValue`); either convert binary values to the same
  string forms, or (better) add a typed value to `THEME_PROPERTY` and teach the getters to use it — choose with the
  smallest change to `draw.c`/`property.c` and explain the choice.
- Images: decode PNG through **windowscodecs (WIC)**, which Wine already has, into a 32-bit premultiplied DIB as
  `MSSTYLES_LoadBitmap` returns today (`hasAlpha` true). Pick the DPI variant for the current DPI. Atlas: load the
  `STREAM` once, crop per `ATLASRECT`.
- Colour schemes and sizes: XP styles have colour/size variants (`COLORNAMES`, `SIZENAMES`); Vista+ styles usually
  have one `VARIANT` per file. Make `EnumThemeColors`/`EnumThemeSizes`/`GetCurrentThemeName` return one sensible
  scheme ("NormalColor"/"NormalSize") so desk.cpl and `.theme` files work.
- New properties and parts that Wine's draw code does not know: store them; implement the ones that change what
  is drawn for existing calls (DPI image selection, atlas, `IMAGESELECTTYPE`, `TRUESIZESCALINGTYPE`, new glyph
  properties); log the rest once. Animations (`AMAP`): parse and expose through
  `GetThemeTransitionDuration`/`GetThemeAnimationProperty`/`GetThemeTimingFunction` if simple, else stub with a
  FIXME — not required for drawing.
- Glass/DWM-dependent parts: draw opaque (the style's fallback colours); no blur.
- Security: the reader parses untrusted files — bounds-check every size, count and offset; a malformed style fails
  to load instead of crashing. Add a fuzz-style test with truncated/corrupted streams.

## Where the code goes

- **Wine fork** `zcomputerwiz/wine`: branch **`upstream/uxtheme-msstyles-v4`** from the fork's `master` (current
  upstream) — this is meant for an upstream MR (Wine has no Vista+ style support at all). Wine style, small commits
  (format detection; CMAP; property stream; PNG/WIC images; DPI; atlas; schemes; tests). Note for the MR text: the
  format knowledge comes from libmsstyle (MIT, independent reverse engineering of the file format, no Microsoft code);
  upstream may still ask questions — that decision is the user's.
- Then the same commits on **`wineexplorer/11.19`** (listed in `docs/STOCK_CHANGES.md` under upstream
  candidates) so our builds get it before upstream merges it.
- **msstyleEditor fork**: add a small command-line dumper (`--dump style.msstyles` → JSON of classes, parts, states,
  typed properties, image ids) to produce expected values for tests. It runs on the Windows host (.NET; libmsstyle
  uses Win32 resource APIs). Push to the fork; it is MIT.

## Test material — Microsoft files stay out of the repos

- **Genuine styles** (Windows 11 `C:\Windows\Resources\Themes\aero\aero.msstyles` on this host; Windows 7/8/10
  styles if available) are Microsoft material: use them **only locally**, keep copies and dumps in
  `../WineExplorer-local` (never pushed), never in WineExplorer, the Wine fork or the msstyleEditor fork, never named
  in commits.
- **Committed tests** (Wine's `dlls/uxtheme/tests`) use a **synthetic** minimal Vista-format style built from our
  own resources (a test resource DLL with `CMAP`, a `VARIANT` stream written by a small generator, our own PNGs):
  format detection, property types, image decode, DPI choice, atlas crop, malformed-input rejection.
- Compare our reader's view of a genuine style against the dumper's JSON locally (every class/part/state/property
  matches) and record only the pass/fail counts in the report.

## Phases (each ends with its check; commit and push after each)

| Phase | Work | Check |
|---|---|---|
| **V0 Groundwork** | msstyleEditor fork: JSON dumper CLI. Generator for C tables from libmsstyle's part/state/property/enum maps (all platforms). Generator for a **synthetic** Vista-format test style (our resources only). Locally: dump the host's genuine Windows 11 `aero.msstyles` into `../WineExplorer-local`. | Dumper output for the synthetic style matches its generator input; generated tables compile. |
| **V1 Detect and parse** | Format detection in `MSSTYLES_OpenThemeFile`; `CMAP`; the `VARIANT` property stream into Wine's structures (typed values); platform detection; single colour/size scheme; bounds-checked parser + malformed-input test. | uxtheme tests: synthetic style opens; `GetThemeInt/Color/Margins/Font/Position/String/Enum` return the generator's values; corrupted streams fail cleanly. Locally: every genuine Windows 11 property matches the dumper JSON. XP styles unchanged. |
| **V2 Images** | `IMAGE` PNGs through WIC into premultiplied DIBs; DPI variant selection; `IMAGESELECTTYPE`/`TRUESIZESCALINGTYPE`; atlas (`STREAM` + `ATLASRECT`, Windows 8+). | Tests: synthetic PNG and atlas parts draw the expected pixels at 96 and 144 DPI. Locally: buttons, check/radio, edits, combos, scroll bars, progress, tabs, headers, menus draw from the Windows 11 style (screenshots kept local). |
| **V3 Integration** | `EnumThemeColors/Sizes`, `GetCurrentThemeName`; `winecfg` and our desk.cpl list and apply a Vista+ style from a `.theme`; our themed window frames (non-client code) and the shell's theme classes draw from it; glass parts opaque. | Apply/switch between Luna, Royale and the Windows 11 style live without restarting; XP styles pixel-identical to before (Xvfb diff); 64- and 32-bit (WoW64) test runs. |
| **V4 Animations (optional)** | `AMAP`: `GetThemeTransitionDuration`, `GetThemeAnimationProperty`, `GetThemeAnimationTransform`, `GetThemeTimingFunction`. | Tests against the synthetic style's animation records. |
| **V5 Upstream and fork** | Clean the series for `upstream/uxtheme-msstyles-v4` (Wine style, one topic per commit, tests), MR text in `docs/UPSTREAM.md`; same commits on `wineexplorer/11.19`; `STOCK_CHANGES.md`; `docs/THEMES.md` updated; report `docs/UXTHEME_V4.md`. | Branch builds on current upstream and its tests pass there; our assembled build passes the V3 checks. |

## Environment

WSL `wexbuild` (Wine build in `/root`, after the WoW64 switch: `--enable-archs=i386,x86_64`, see `CLAUDE.md`), or a
separate clone of the Wine fork there for the upstream branch (`git clone -b master
https://github.com/zcomputerwiz/wine`). Visual checks on Xvfb in `wexbuild`. Do not use Codex's Proton VM or the
main checkout.

## Acceptance (report in `docs/UXTHEME_V4.md`, screenshots of open/synthetic styles only)

- `wine/dlls/uxtheme/tests` pass (existing + new) for XP styles and the synthetic Vista-format style, 64- and 32-bit.
- Locally with the genuine Windows 11 style: `OpenThemeData` succeeds; the dumper comparison matches for all
  properties; `winecfg`'s Desktop Integration lists and applies it; buttons, check boxes, edits, combo boxes, scroll
  bars, progress, tabs, list view headers, menus and window frames draw from it (screenshots stay in
  `WineExplorer-local`; the report describes them).
- XP styles (Luna, Royale, Wine's Light) render exactly as before (screenshot diff on Xvfb).
- Malformed styles fail cleanly (no crash) — fuzz test.
- Branch `upstream/uxtheme-msstyles-v4` pushed with a ready-to-paste MR description in `docs/UPSTREAM.md`;
  `wineexplorer/11.19` carries the same commits; `docs/STOCK_CHANGES.md` updated.

Rules: commit after each working step; Wine's coding style in Wine files; GPL headers on our new files; never pass C
escapes through heredocs/sed/`python -c`.
