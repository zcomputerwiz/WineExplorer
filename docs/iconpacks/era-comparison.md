# Windows-era icon comparison

This review covers the assets currently present in the checkout on 2026-11-04. It distinguishes three things: an extracted/official reference asset, an open mapping in `assets/iconpacks`, and a theme implementation that is not itself an icon pack.

## Sheets

| Era | Sheet | Result |
|---|---|---|
| XP / Luna | [reference vs open sheet](sheets/xp-reference-open.svg) | Reference resource sheets and the open XP preview are present. |
| Vista / Aero | [comparison sheet](sheets/vista-reference-open.svg) | No Vista reference icon pack or Vista open icon mapping is present. `aero-open` covers theme chrome. |
| Windows 7 / Aero | [comparison sheet](sheets/windows7-reference-open.svg) | No Windows 7 reference icon pack or Windows 7 open icon mapping is present. |
| Windows 10 / 11 | [comparison sheet](sheets/windows10-11-reference-open.svg) | `fluent.json` is a source/mapping definition, not a 10/11 reference pack; no official 10/11 reference set is present. |

The XP open pack is defined by [assets/iconpacks/xp.json](../../assets/iconpacks/xp.json) and its rendered preview is [xp-preview.png](xp-preview.png). The open theme implementations are [luna-open](../../src/resources/themes/luna-open) and [aero-open](../../src/resources/themes/aero-open); their resources are controls and window chrome, not a complete era-specific shell icon library.

## Concepts that remain the same or similar across the eras

These are semantic/icon concepts that are stable across Windows versions. The repository does not contain all four reference packs, so “same or similar” here means the subject is stable; it does not assert pixel identity.

| Concept | Typical subject and continuity |
|---|---|
| Folder / open folder | Yellow or colored folder silhouette; the tab-and-body metaphor persists. |
| File / document | White sheet with folded corner; later styles simplify the outline and add flatter color. |
| Computer / desktop | Monitor or desktop-device silhouette; XP adds dimensional bezels, later eras reduce shading. |
| Storage drive | Disk/drive body, often with a colored status accent; floppy and optical variants are legacy XP-era forms. |
| Network / Internet | Globe, connected computers, or network adapter; colors move from glossy blue/green to flatter blue. |
| Printer | Printer body with paper feed; the subject remains recognizable while perspective and shading change. |
| Recycle Bin | Wastebasket with recycling arrows; XP is glossy and dimensional, later art is flatter. |
| Search / Help | Magnifying glass and question mark; the glyphs remain highly consistent. |
| Settings / Control Panel | Gear or control-panel metaphor; later versions use flatter monochrome or accent-color treatments. |
| Power / Log off | Power symbol, red/orange action button, or door/arrow; intent is stable even when the treatment changes. |
| Audio / Volume | Speaker with wave marks, plus mute overlay; waveform count and color vary. |
| Battery / Plug | Battery silhouette with charge/plug overlay; green indicates healthy/connected state in most sets. |
| Display / Mouse / Keyboard | Hardware silhouettes used in Control Panel; geometry changes less than the rendering style. |
| Clock / Date | Analog clock or calendar/clock combination; the time/date metaphor remains stable. |
| Wireless | Radio-wave fan or adapter; status colors commonly progress gray → blue/green. |

## Reference icons without an open equivalent

### XP reference-only families

The extracted XP tree contains more reference resources than the current open mapping covers. The following families are present under `references/xp_export` or `references/xp_resources_sheets` but are not represented as a one-to-one open icon entry in `assets/iconpacks/xp.json`.

| Reference family | Visual description |
|---|---|
| `res/setupapi/*.ico` beyond the mapped shell/network entries | Small hardware-class emblems: boards, ports, buses, adapters, drives, and peripherals. They use XP's glossy 3-D treatment, cool gray metal, saturated blue/green status accents, and dark outlines. |
| `res/mmcndmgr/*.ico` and `mmcndmgr_bitmaps.png` | Management-console/device-manager symbols: computers, racks, hardware cards, tools, warning overlays, and connection motifs. The palette is steel gray, navy blue, teal, and amber/red for attention states. |
| `res/mmsys/*.ico` beyond the mapped speaker/playback entries | Audio-device and sound-event imagery: speakers, microphones, waveforms, media controls, and audio panels. The style is beveled gray hardware with blue, green, or red state accents. |
| `res/msgina/*.ico` and `res/msgina/*.bmp` | Logon, welcome, lock, shutdown, user, and account artwork. These are larger, presentation-oriented assets with royal blue gradients, white text/UI framing, warm orange action accents, and glossy user/power motifs. |
| `res/mmc/*.ico` / `res/mmc/*.bmp` | Console snap-in and administrative-tool imagery. Expect document panels, charts, hardware views, and tool overlays in muted gray/blue with occasional yellow or red status marks. |
| `res/appwiz/*` | Add/Remove Programs and setup imagery: boxes, discs, package/document forms, and installation actions; typically white/gray bodies with blue, green, and orange accents. |
| `res/shell32/*.bmp` not represented by the open shell mapping | Shell UI strips and special-state art rather than standalone icons: glossy blue/green gradients, white glyphs, yellow folders, and state overlays. |

### Vista, 7, and 10/11

No reference icon files for these eras were found in the current `references` tree, so there is no honest per-icon missing list to describe. The missing deliverable is the source pack itself. Once those reference packs are added, this page can be extended with exact file/resource IDs and per-icon descriptions.

## Coverage conclusion

The repository currently supports a meaningful XP side-by-side review, including open mappings and extracted reference material. It does not currently support four-era pixel comparison: Vista/7/10/11 reference icon packs and corresponding open mappings must be supplied before those sheets can contain actual icon pairs.
