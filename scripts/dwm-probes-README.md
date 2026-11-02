# DWM Clean-Room Host Measurement Probes (Section 6 of docs/DWM_PLAN.md)
Build (from repo root, -ldwmapi optional as dwmapi.dll is loaded dynamically):
  x86_64-w64-mingw32-gcc -Wall -O2 scripts/dwm-frame-metrics-probe.c -o dwm-frame-metrics-probe.exe -lgdi32 -luser32 -ldwmapi
  x86_64-w64-mingw32-gcc -Wall -O2 scripts/dwm-corner-shadow-probe.c -o dwm-corner-shadow-probe.exe -lgdi32 -luser32 -ldwmapi
  x86_64-w64-mingw32-gcc -Wall -O2 scripts/dwm-backdrop-color-probe.c -o dwm-backdrop-color-probe.exe -lgdi32 -luser32 -ldwmapi
  x86_64-w64-mingw32-gcc -Wall -O2 scripts/dwm-timing-iconic-probe.c -o dwm-timing-iconic-probe.exe -lgdi32 -luser32 -ldwmapi
Run: execute each .exe on Windows 11 (composition on) or Wine; each logs key=value lines to stdout and Z:\tmp\<probename>.txt.
- dwm-frame-metrics-probe (Z:\tmp\dwm-frame-metrics-probe.txt): DPI 96/120/144/192 system metrics, window/client/extended/caption rects, and side deltas.
- dwm-corner-shadow-probe (Z:\tmp\dwm-corner-shadow-probe.txt) & dwm-backdrop-color-probe (Z:\tmp\dwm-backdrop-color-probe.txt): corner radii, arc coverage, shadow extents/offset, and Mica/Acrylic/colorization RGB samples.
- dwm-timing-iconic-probe (Z:\tmp\dwm-timing-iconic-probe.txt): DwmGetCompositionTimingInfo 100ms deltas, 30-call DwmFlush latency, normal/minimized/cloaked thumbnail sizes, and iconic messages.
