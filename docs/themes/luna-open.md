<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Open Luna Visual Style (`Luna.msstyles`) Provenance

This document records the provenance and origin of all visual resources and configuration in `src/resources/themes/luna-open/`.

## Origin and Clean-Room Policy
- **Base**: ReactOS Lunar (`references/reactos/media/themes/Lunar/lunar.msstyles/`), licensed under **GPL-2.0-or-later** (ReactOS Project). Imported unchanged in commit `59a71f29`.
- **Clean-room rule**: No Windows bitmaps, pixels, or sampled hex codes were extracted, traced, or copied into this repository. All colors, gradients, button shapes, and metrics were matched by eye against reference running systems under pair-programming inspection.
- **Redrawn assets**: All complex parts and redesigned elements are authored as clean SVG sources in `src/resources/themes/luna-open/svg/` and compiled to 24-bit/32-bit BMPs with ImageMagick (`-depth 8`) during the build.

## Bitmap Provenance Table

| Bitmap Resource | Dimensions | Origin | Description / Shapes |
|---|---|---|---|
| `NORMAL_CHEVRON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_CLOSEGLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_EXPLORERBARHEADERBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_EXPLORERBARHEADERCLOSE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_EXPLORERBARTOOLBARBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_FIELDOUTLINEBLUE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_FRAMECAPTIONMIN.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_FRAMECAPTIONSIZING.bmp` | Drawn SVG | `svg/NORMAL_FRAMECAPTIONSIZING.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_FRAMEMAXIMIZED.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_GRIPPER.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_GRIPPERVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_GROUPBOX.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_LISTVIEWHEADER.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_LISTVIEWHEADERBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_MDICAPTIONBUTTON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_MDIGLYPHCLOSE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_MDIGLYPHMINIMIZE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_MDIGLYPHRESTORE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_NORMALGROUPBACKGROUND.bmp` | Drawn SVG | `svg/NORMAL_NORMALGROUPBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_NORMALGROUPHEAD.bmp` | Drawn SVG | `svg/NORMAL_NORMALGROUPHEAD.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_PLACEBARBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_PLACEBARBUTTONS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_RESIZEGRIP2.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SCROLLARROWGLYPHS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SCROLLARROWGLYPHSSMALL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SMALLFRAMECAPTION.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SMALLFRAMECAPTIONSIZING.bmp` | Drawn SVG | `svg/NORMAL_SMALLFRAMECAPTIONSIZING.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_SPECIALGROUPBACKGROUND.bmp` | Drawn SVG | `svg/NORMAL_SPECIALGROUPBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_SPECIALGROUPHEAD.bmp` | Drawn SVG | `svg/NORMAL_SPECIALGROUPHEAD.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_SPINBUTTONBACKGROUNDDOWN.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINBUTTONBACKGROUNDLEFT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINBUTTONBACKGROUNDRIGHT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINBUTTONBACKGROUNDUP.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINDOWNGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINLEFTGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINRIGHTGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_SPINUPGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTGROUPBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTGROUPNEWAPPBUTTON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTGROUPSEPERATOR.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTGROUPTOOLBARBUTTON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELLOGOFFBACKGROUND.bmp` | Drawn SVG | `svg/NORMAL_STARTPANELLOGOFFBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_STARTPANELLOGOFFBUTTONS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELLOGOFFBUTTONSHOT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELMFUBACKGROUND.bmp` | Drawn SVG | `svg/NORMAL_STARTPANELMFUBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_STARTPANELMOREPROGARROW.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELMOREPROGARROWHOT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELMOREPROGBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPANELPLACESBACKGROUND.bmp` | Drawn SVG | `svg/NORMAL_STARTPANELPLACESBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_STARTPLACESSEPARATOR.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTPROGRAMSSEPARATOR.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_STARTUSERPANEL.bmp` | Drawn SVG | `svg/NORMAL_STARTUSERPANEL.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `NORMAL_STATUSPANE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDFLASHBUTTON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDFLASHBUTTONGROUPMENU.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDHOVER.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLARROWGLYPHS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLARROWS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLSHAFTHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLSHAFTVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLTHUMBGRIPPERHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLTHUMBGRIPPERVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLTHUMBHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBANDSCROLLTHUMBVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBARCHEVRON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBARCHEVRONVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBARGRIPPER.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TASKBARGRIPPERVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TOOLBARBUTTONSSPLITDROPDOWNGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TOOLBARGRIPPER.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TOOLBARGRIPPERVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_TREEEXPANDCOLLAPSE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `NORMAL_USERTILEBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_BALLOONCLOSE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_BUTTON.bmp` | Drawn SVG | `svg/REACTOS_BUTTON.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_CAPTIONBUTTON_BACKGROUND.bmp` | Drawn SVG | `svg/REACTOS_CAPTIONBUTTON_BACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_CAPTIONBUTTON_BACKGROUNDSMALL.bmp` | Drawn SVG | `svg/REACTOS_CAPTIONBUTTON_BACKGROUNDSMALL.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_CHECKBOX13.bmp` | Drawn SVG | `svg/REACTOS_CHECKBOX13.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_CHECKBOX16.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CHECKBOX25.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH12.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH19.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH23.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH6.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_CLOSEBUTTON_GLYPHSMALL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_COMBOBUTTON.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_COMBOBUTTONGLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_COMBOBUTTON_GLYPH.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_EXPLORERBARTOOLBARBUTTONS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_EXPLORERBARTOOLBARBUTTONSSPLIT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_EXPLORERBARTOOLBARBUTTONSSPLITDROPDOWN.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_FRAMECAPTION.bmp` | Drawn SVG | `svg/REACTOS_FRAMECAPTION.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_FRAMESIDES.bmp` | Drawn SVG | `svg/REACTOS_FRAMESIDES.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_HELPBUTTON_GLYPH13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_HELPBUTTON_GLYPH19.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_HELPBUTTON_GLYPH23.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_HELPBUTTON_GLYPH6.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_HELPBUTTON_GLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH12.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH19.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH23.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH6.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MAXBUTTON_GLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH12.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH19.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH23.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH6.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_MINBUTTON_GLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_PROGRESSCHUNK.bmp` | Drawn SVG | `svg/REACTOS_PROGRESSCHUNK.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_PROGRESSCHUNKVERT.bmp` | Drawn SVG | `svg/REACTOS_PROGRESSCHUNKVERT.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_PROGRESSTRACK.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_PROGRESSTRACKVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_RADIOBUTTON13.bmp` | Drawn SVG | `svg/REACTOS_RADIOBUTTON13.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_RESTOREBUTTON_GLYPH13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_RESTOREBUTTON_GLYPH19.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_RESTOREBUTTON_GLYPH23.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_RESTOREBUTTON_GLYPH6.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_RESTOREBUTTON_GLYPH9.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLARROWS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLSHAFTHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLSHAFTVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLTHUMBGRIPPERHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLTHUMBGRIPPERVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLTHUMBHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SCROLLTHUMBVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SEPARATOR.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SEPARATORVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SLIDERTRACK.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_SLIDERTRACKVERT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_STARTBUTTON.bmp` | Drawn SVG | `svg/REACTOS_STARTBUTTON.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_STATUSBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TABBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TABITEM.bmp` | Drawn SVG | `svg/REACTOS_TABITEM.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TABITEMBOTH.bmp` | Drawn SVG | `svg/REACTOS_TABITEMBOTH.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TABITEMLEFT.bmp` | Drawn SVG | `svg/REACTOS_TABITEMLEFT.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TABITEMRIGHT.bmp` | Drawn SVG | `svg/REACTOS_TABITEMRIGHT.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TABITEMTOP.bmp` | Drawn SVG | `svg/REACTOS_TABITEMTOP.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TABPANEEDGE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBANDBUTTON.bmp` | Drawn SVG | `svg/REACTOS_TASKBANDBUTTON.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TASKBANDBUTTONNOEDGE.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBANDBUTTONNOEDGEVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBANDBUTTONVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARBACKGROUND.bmp` | Drawn SVG | `svg/REACTOS_TASKBARBACKGROUND.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TASKBARBACKGROUNDLEFT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARBACKGROUNDRIGHT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARBACKGROUNDTOP.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARBACKGROUNDVERTICAL.bmp` | Drawn SVG | `svg/REACTOS_TASKBARBACKGROUNDVERTICAL.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TASKBARSIZINGBARBOTTOM.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARSIZINGBARLEFT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARSIZINGBARRIGHT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARSIZINGBARTOP.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARTRAY.bmp` | Drawn SVG | `svg/REACTOS_TASKBARTRAY.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TASKBARTRAYBOTTOM.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARTRAYLEFT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARTRAYRIGHT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARTRAYTOP.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TASKBARTRAYVERTICAL.bmp` | Drawn SVG | `svg/REACTOS_TASKBARTRAYVERTICAL.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TOOLBARBACKGROUND.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TOOLBARBUTTONS.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TOOLBARBUTTONSSPLIT.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TOOLBARBUTTONSSPLITDROPDOWN.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARDOWN13.bmp` | Drawn SVG | `svg/REACTOS_TRACKBARDOWN13.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TRACKBARDOWN16.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARDOWN25.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARHORIZONTAL.bmp` | Drawn SVG | `svg/REACTOS_TRACKBARHORIZONTAL.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TRACKBARLEFT13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARLEFT16.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARLEFT25.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARRIGHT13.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARRIGHT16.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARRIGHT25.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARUP13.bmp` | Drawn SVG | `svg/REACTOS_TRACKBARUP13.svg` | Redrawn from scratch: vector primitives, gradients, crisp corner masks |
| `REACTOS_TRACKBARUP16.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARUP25.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRACKBARVERTICAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRAYCHEVRONHORIZONTAL.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `REACTOS_TRAYCHEVRONHORIZONTALOPEN.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |
| `dummy.bmp` | Untouched | ReactOS Lunar (GPL) | Untouched ReactOS Lunar source bitmap |

## Drawn Vector Sources (`src/resources/themes/luna-open/svg/`)

The following 39 SVG files provide editable vector sources compiled into the visual style:

- **`NORMAL_FRAMECAPTIONSIZING.svg`**: Vector source compiled to `NORMAL_FRAMECAPTIONSIZING.bmp` at build time.
- **`NORMAL_NORMALGROUPBACKGROUND.svg`**: Vector source compiled to `NORMAL_NORMALGROUPBACKGROUND.bmp` at build time.
- **`NORMAL_NORMALGROUPCOLLAPSE.svg`**: Vector source compiled to `NORMAL_NORMALGROUPCOLLAPSE.bmp` at build time.
- **`NORMAL_NORMALGROUPEXPAND.svg`**: Vector source compiled to `NORMAL_NORMALGROUPEXPAND.bmp` at build time.
- **`NORMAL_NORMALGROUPHEAD.svg`**: Vector source compiled to `NORMAL_NORMALGROUPHEAD.bmp` at build time.
- **`NORMAL_SMALLFRAMECAPTIONSIZING.svg`**: Vector source compiled to `NORMAL_SMALLFRAMECAPTIONSIZING.bmp` at build time.
- **`NORMAL_SPECIALGROUPBACKGROUND.svg`**: Vector source compiled to `NORMAL_SPECIALGROUPBACKGROUND.bmp` at build time.
- **`NORMAL_SPECIALGROUPCOLLAPSE.svg`**: Vector source compiled to `NORMAL_SPECIALGROUPCOLLAPSE.bmp` at build time.
- **`NORMAL_SPECIALGROUPEXPAND.svg`**: Vector source compiled to `NORMAL_SPECIALGROUPEXPAND.bmp` at build time.
- **`NORMAL_SPECIALGROUPHEAD.svg`**: Vector source compiled to `NORMAL_SPECIALGROUPHEAD.bmp` at build time.
- **`NORMAL_STARTPANELLOGOFFBACKGROUND.svg`**: Vector source compiled to `NORMAL_STARTPANELLOGOFFBACKGROUND.bmp` at build time.
- **`NORMAL_STARTPANELMFUBACKGROUND.svg`**: Vector source compiled to `NORMAL_STARTPANELMFUBACKGROUND.bmp` at build time.
- **`NORMAL_STARTPANELPLACESBACKGROUND.svg`**: Vector source compiled to `NORMAL_STARTPANELPLACESBACKGROUND.bmp` at build time.
- **`NORMAL_STARTUSERPANEL.svg`**: Vector source compiled to `NORMAL_STARTUSERPANEL.bmp` at build time.
- **`REACTOS_BUTTON.svg`**: Vector source compiled to `REACTOS_BUTTON.bmp` at build time.
- **`REACTOS_CAPTIONBUTTON_BACKGROUND.svg`**: Vector source compiled to `REACTOS_CAPTIONBUTTON_BACKGROUND.bmp` at build time.
- **`REACTOS_CAPTIONBUTTON_BACKGROUNDSMALL.svg`**: Vector source compiled to `REACTOS_CAPTIONBUTTON_BACKGROUNDSMALL.bmp` at build time.
- **`REACTOS_CHECKBOX13.svg`**: Vector source compiled to `REACTOS_CHECKBOX13.bmp` at build time.
- **`REACTOS_CLOSEBUTTON_BACKGROUND.svg`**: Vector source compiled to `REACTOS_CLOSEBUTTON_BACKGROUND.bmp` at build time.
- **`REACTOS_CLOSEBUTTON_BACKGROUNDSMALL.svg`**: Vector source compiled to `REACTOS_CLOSEBUTTON_BACKGROUNDSMALL.bmp` at build time.
- **`REACTOS_FRAMECAPTION.svg`**: Vector source compiled to `REACTOS_FRAMECAPTION.bmp` at build time.
- **`REACTOS_FRAMESIDES.svg`**: Vector source compiled to `REACTOS_FRAMESIDES.bmp` at build time.
- **`REACTOS_PROGRESSCHUNK.svg`**: Vector source compiled to `REACTOS_PROGRESSCHUNK.bmp` at build time.
- **`REACTOS_PROGRESSCHUNKVERT.svg`**: Vector source compiled to `REACTOS_PROGRESSCHUNKVERT.bmp` at build time.
- **`REACTOS_RADIOBUTTON13.svg`**: Vector source compiled to `REACTOS_RADIOBUTTON13.bmp` at build time.
- **`REACTOS_STARTBUTTON.svg`**: Vector source compiled to `REACTOS_STARTBUTTON.bmp` at build time.
- **`REACTOS_TABITEM.svg`**: Vector source compiled to `REACTOS_TABITEM.bmp` at build time.
- **`REACTOS_TABITEMBOTH.svg`**: Vector source compiled to `REACTOS_TABITEMBOTH.bmp` at build time.
- **`REACTOS_TABITEMLEFT.svg`**: Vector source compiled to `REACTOS_TABITEMLEFT.bmp` at build time.
- **`REACTOS_TABITEMRIGHT.svg`**: Vector source compiled to `REACTOS_TABITEMRIGHT.bmp` at build time.
- **`REACTOS_TABITEMTOP.svg`**: Vector source compiled to `REACTOS_TABITEMTOP.bmp` at build time.
- **`REACTOS_TASKBANDBUTTON.svg`**: Vector source compiled to `REACTOS_TASKBANDBUTTON.bmp` at build time.
- **`REACTOS_TASKBARBACKGROUND.svg`**: Vector source compiled to `REACTOS_TASKBARBACKGROUND.bmp` at build time.
- **`REACTOS_TASKBARBACKGROUNDVERTICAL.svg`**: Vector source compiled to `REACTOS_TASKBARBACKGROUNDVERTICAL.bmp` at build time.
- **`REACTOS_TASKBARTRAY.svg`**: Vector source compiled to `REACTOS_TASKBARTRAY.bmp` at build time.
- **`REACTOS_TASKBARTRAYVERTICAL.svg`**: Vector source compiled to `REACTOS_TASKBARTRAYVERTICAL.bmp` at build time.
- **`REACTOS_TRACKBARDOWN13.svg`**: Vector source compiled to `REACTOS_TRACKBARDOWN13.bmp` at build time.
- **`REACTOS_TRACKBARHORIZONTAL.svg`**: Vector source compiled to `REACTOS_TRACKBARHORIZONTAL.bmp` at build time.
- **`REACTOS_TRACKBARUP13.svg`**: Vector source compiled to `REACTOS_TRACKBARUP13.bmp` at build time.

### Summary of Drawn Part Groups
1. **Window Frame & Caption Buttons (Part 1)**:
   - `REACTOS_FRAMECAPTION.svg` (52x52): Royal blue active title bar gradient with curved top shine and crisp 1px corner cutouts; slate blue inactive state.
   - `REACTOS_CAPTIONBUTTON_BACKGROUND.svg` (15x128): Blue rounded buttons with white glyphs for minimize, maximize, restore.
   - `REACTOS_CLOSEBUTTON_BACKGROUND.svg` (15x128): Red rounded buttons with white glyphs for window close.
   - `REACTOS_CAPTIONBUTTON_BACKGROUNDSMALL.svg` & `REACTOS_CLOSEBUTTON_BACKGROUNDSMALL.svg` (13x128): Tool window variants.
   - `NORMAL_FRAMECAPTIONSIZING.svg` (66x58) & `NORMAL_SMALLFRAMECAPTIONSIZING.svg` (66x38): Crisp monochrome corner sizing masks.
   - `REACTOS_FRAMESIDES.svg` (1x2): Royal blue active and slate blue inactive 1px window border strips.

2. **Taskbar and Start Menu (Part 2)**:
   - `REACTOS_STARTBUTTON.svg` (98x78): 3-state green pill with subtle top curvature, 4-color Windows emblem, and italicized 'start' text.
   - `REACTOS_TASKBARBACKGROUND.svg` (1x26) & `VERTICAL.svg` (26x1): Royal blue horizontal and vertical taskbar gradients.
   - `REACTOS_TASKBARTRAY.svg` (1x26) & `VERTICAL.svg` (26x1): Deeper navy tray notification area background.
   - `REACTOS_TASKBANDBUTTON.svg` (26x144): 6-state taskbar button strip featuring warm orange top strip (`#f89818`) for active window button.
   - `NORMAL_STARTUSERPANEL.svg` (101x64): Royal blue user banner with rounded top corners, top shine, and orange bottom accent strip.
   - `NORMAL_STARTPANELLOGOFFBACKGROUND.svg` (101x28): Deep navy bottom banner with rounded bottom corners and soft top highlight.
   - `NORMAL_STARTPANELPLACESBACKGROUND.svg` (100x205): Soft sky-blue fill (`#d3e5fa`) for right-hand places column.
   - `NORMAL_STARTPANELMFUBACKGROUND.svg` (17x176): Crisp white fill for left-hand programs column.

3. **Controls (Part 3)**:
   - `REACTOS_BUTTON.svg` (22x115): 5-state rounded push button with default blue glow border and orange hot ring.
   - `REACTOS_PROGRESSCHUNK.svg` (57x10) & `VERT.svg` (10x57): Segmented vibrant grass-green progress blocks with specular highlight shine.
   - `REACTOS_TABITEM.svg`, `TABITEMTOP.svg`, `LEFT.svg`, `RIGHT.svg`, `BOTH.svg` (18x90): Tab controls featuring vibrant orange/amber top strip (`#f88418`) on active tab.
   - `REACTOS_CHECKBOX13.svg` (13x156): Checkboxes with vibrant green checkmark.
   - `REACTOS_RADIOBUTTON13.svg` (13x104): Radio buttons with vibrant green center dot.
   - `REACTOS_TRACKBARDOWN13.svg`, `UP13.svg`, `HORIZONTAL.svg`: Pointed slider thumb with vertical green center indicator.

4. **Explorer Bar Task Panes (Part 4)**:
   - `NORMAL_SPECIALGROUPHEAD.svg` (111x24): Royal blue gradient with rounded top corners and white text for primary task pane header.
   - `NORMAL_NORMALGROUPHEAD.svg` (111x24): White-to-light-blue gradient with rounded corners and blue text for secondary task pane headers.
   - `NORMAL_SPECIALGROUPBACKGROUND.svg` & `NORMALGROUPBACKGROUND.svg` (111x10): Soft sky-blue fill with clean 1px border.
   - `NORMAL_SPECIALGROUPCOLLAPSE.svg`, `EXPAND.svg`, `NORMALGROUPCOLLAPSE.svg`, `EXPAND.svg` (16x48): Circular chevron toggle buttons.

## INI Changes from Untouched ReactOS Lunar

1. **Theme Scheme & Metrics Naming (`lunar.rc`)**:
   - Aligned color and size names to `NormalColor` and `NormalSize\0LargeFonts\0ExtraLargeFonts` so standard `.theme` files load cleanly.
   - Repointed `[Window.CloseButton]` and `[Window.CloseButtonSmall]` from `CaptionButton_Background.bmp` to red `CloseButton_Background.bmp`.
   - Repointed task pane collapse/expand resources from `dummy.bmp` to dedicated circular chevron bitmaps.

2. **SysMetrics (`NormalReactOS.INI`, `LargeFontsReactOS.INI`, `ExtraLargeReactOS.INI`)**:
   - `CaptionBarHeight = 25` (29 at LargeFonts, 35 at ExtraLarge).
   - `CaptionFont = Trebuchet MS, 10, Bold` (evaluated via fontconfig to DejaVu Sans Bold on open Linux/ISO; Tahoma 8 on controls).
   - `ActiveCaption = 0 84 227`, `GradientActiveCaption = 61 149 255`.
   - `InactiveCaption = 122 150 223`, `GradientInactiveCaption = 157 185 235`.
   - `ScrollbarWidth = 15`, `ScrollbarHeight = 15`.

3. **Progress Bar Theming**:
   - Added `[Progress.Fill]` and `[Progress.FillVert]` sections mapped to `ReactOS\ProgressChunk.bmp` and `ReactOS\ProgressChunkVert.bmp` with `ProgressChunkSize = 6, ProgressSpaceSize = 2`, `SizingType = Tile` to render classic XP segmented green blocks in Wine comctl32 v6.

4. **Start Menu Panels**:
   - `[StartPanel.UserPane]`: `BgType = ImageFile`, `DefaultPaneSize = 0, 0, 380, 64`, `UserPicture = True`, `SizingMargins = 9, 9, 14, 14`.
   - `[StartPanel.Logoff]`: `BgType = ImageFile`, `DefaultPaneSize = 0, 0, 357, 28`.

5. **Explorer Bar Task Panes**:
   - `[ExplorerBar.SpecialGroupHead]`: `TextColor = 255 255 255`.
   - `[ExplorerBar.NormalGroupHead]`: `TextColor = 33 93 198`.
   - `[ExplorerBar.SpecialGroupCollapse]` / `[ExplorerBar.SpecialGroupExpand]`: Pointed to `Normal\SpecialGroupCollapse.bmp` and `SpecialGroupExpand.bmp`.



## Round 2 Polish Updates (2026-10-17)

Seven specific details were refined to match genuine Luna at a glance:

1. **Caption Font (`src/resources/fonts/font-substitutes.reg`)**:
   - Added `"Trebuchet MS"="DejaVu Sans"` to FontSubstitutes registry to ensure open environments without Microsoft core fonts fall back directly to DejaVu Sans Bold, matching the humanist sans metrics, letter spacing, and bold weight of Trebuchet MS.

2. **Caption Buttons (`REACTOS_CAPTIONBUTTON_BACKGROUND.svg`, `REACTOS_CLOSEBUTTON_BACKGROUND.svg`, `*SMALL.svg`)**:
   - Resized buttons to uniform 21x21 rounded pill geometry (`stroke="#ffffff"`, `stroke-width="1"`).
   - Redrew 2px bold white glyph bitmaps (`CLOSEBUTTON_GLYPH13.bmp`, `MINBUTTON_GLYPH13.bmp`, `MAXBUTTON_GLYPH13.bmp`, `RESTOREBUTTON_GLYPH13.bmp`).

3. **Start Button (`REACTOS_STARTBUTTON.svg`, `NormalReactOS.INI`)**:
   - Redrew Start button SVG (99x90, 30px pill height) with bright top specular highlight (`#c8ffa4` to transparent), dark border (`#0e4006`), and clean 32bpp alpha mask.
   - Configured `[Start::Button]` in `NormalReactOS.INI` with `Font = Trebuchet MS, 12, Bold, Italic`, `ShadowType = Single`, `ShadowColor = 0 55 12`, `TextShadowOffset = 1, 1`. The shell dynamically paints the Wine glass logo and text onto the pill.

4. **Start Menu Panels & Controls (`NORMAL_USERTILEBACKGROUND.svg`, `NORMAL_STARTUSERPANEL.svg`, `NORMAL_STARTPANELMOREPROGARROW.svg`, `NormalReactOS.INI`)**:
   - Authored `NORMAL_USERTILEBACKGROUND.svg` (55x55): 2.5px white rounded border with soft drop shadow.
   - Updated `NORMAL_STARTUSERPANEL.svg`: 2px vibrant orange accent strip (`#e8841c` / `#fca834`) across bottom edge.
   - Authored `NORMAL_STARTPANELMOREPROGARROW.svg` and `HOT.svg`: 16x16 32bpp alpha green forward triangle without magenta artifacts.
   - Configured `[StartPanel.UserPane]` with `Font = Trebuchet MS, 14, Bold` and drop shadow.
   - Adjusted `[StartPanel.Logoff]` height to 40px with royal blue gradient.

5. **Taskbar and Notification Tray (`REACTOS_TASKBANDBUTTONNOEDGE.svg`, `REACTOS_TASKBARTRAY.svg`)**:
   - Authored `REACTOS_TASKBANDBUTTONNOEDGE.svg`: State 4 (checked/active) in darker sunken blue (`#062470` to `#1242a4`) with 1px vibrant orange top highlight (`#f89a1c`).
   - Authored `REACTOS_TASKBARTRAY.svg` (25x26): lighter cyan-blue gradient (`#168ee4` to `#043e94`) with 1px bright cyan vertical highlight line (`#48c4ff`) on left edge. Configured `SizingMargins = 4, 4, 0, 0` in INI.

6. **Scroll Bars (`REACTOS_SCROLLARROWS.svg`, `REACTOS_SCROLLTHUMBVERTICAL.svg`, `REACTOS_SCROLLTHUMBGRIPPERVERTICAL.svg`, `REACTOS_SCROLLSHAFTVERTICAL.svg`)**:
   - Authored `REACTOS_SCROLLARROWS.svg` (15x256, 16 states): light blue-grey rounded buttons with dark navy blue arrow glyphs.
   - Authored `REACTOS_SCROLLTHUMBVERTICAL.svg` (15x88): light blue-grey rounded thumbs.
   - Authored `REACTOS_SCROLLTHUMBGRIPPERVERTICAL.svg` (6x40, 32bpp alpha): 3 horizontal gripper ridges.
   - Authored `REACTOS_SCROLLSHAFTVERTICAL.svg` (15x68): soft light blue channel track.
   - Wired `Transparent = True` and `TransparentColor = 255 0 255` in INI.

7. **My Computer Task Pane (`NormalReactOS.INI`, `NORMAL_NORMALGROUPBACKGROUND.svg`, `NORMAL_SPECIALGROUPBACKGROUND.svg`)**:
   - Configured `[ExplorerBar]` `GradientColor1 = 120 152 224`, `GradientColor2 = 84 112 198` for rich blue vertical background gradient.
   - Redrew group backgrounds as pure crisp white fill (`#ffffff`) with 1px soft blue border (`#b4ccf0`).
