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
