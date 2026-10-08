# Microsoft-derived resources

The repo is private and experimental. It uses Windows XP / Server 2003 artwork so the UI can be compared
reference file it matches or how it was produced. ReactOS (GPL) and original files are listed too.

```
```

Not covered by `swap` (text, needs a human rewrite if the project is ever released):
- `wine/resources/themes/*.theme` (listed as `ms-text`)
- dialog layouts and UI strings in `*.rc` that reproduce XP wording (e.g. desk.cpl confirm dialog, UAC dialog text)
- colour values and metrics copied from XP in the GDI drawing code (start menu, taskbar)

Add new assets, run `scan`, then set `origin` for any `ms?` entries by hand in the JSON.

## Fonts
Same scheme as the images: `assets/font_substitutes.json` is the exact list of Microsoft fonts the UI expects, with the open
replacement, match quality (`metric` / `approx` / `builtin` / `none`), license and Debian package for each.
The genuine files (exported from the XP VM into `references/xp_export/fonts`, never committed) are named per row.

```
python scripts/fonts.py list          # table + whether each genuine file is in the export
python scripts/fonts.py reg           # .reg mapping MS families to the open fonts (HKCU\Software\Wine\Fonts\Replacements)
scripts/vm.sh fonts xp                # copy the genuine fonts into the VM prefix (Wine picks up windows\Fonts)
scripts/vm.sh fonts open              # remove them again and import the replacement mapping
```
The open substitution table is fontconfig's `30-metric-aliases.conf` (Liberation = Arial/Times New Roman/Courier New,
Carlito = Calibri, Caladea = Cambria; all metric-compatible). It has no entry for Tahoma, Verdana, Trebuchet MS or
Segoe UI: Tahoma and Marlett come from `wine/fonts` (Wine's own LGPL fonts), Verdana/Trebuchet fall back to DejaVu
(approximate widths) and Segoe UI maps to Selawik (MIT, `github.com/microsoft/Selawik`, install by hand).
Not yet done: Luna's caption font (Trebuchet MS 10 bold) only applies once the theme engine sets the non-client metrics
from the msstyles `[SysMetrics]` when a theme is applied.
