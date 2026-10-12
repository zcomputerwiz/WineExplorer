#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Derive BIOS/EFI boot-safety fixtures from the interactive ISO."""
from pathlib import Path
import re,subprocess,sys,tempfile
iso=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wex-safety-iso-') as temporary:
    root=Path(temporary)
    grub=root/'grub.cfg'
    subprocess.run(['xorriso','-osirrox','on','-indev',str(iso),'-extract','/boot/grub/grub.cfg',str(grub)],check=True)
    text=grub.read_text()
    text,count=re.subn(r'(?m)^set default=.*$', 'set default=2',text)
    if count!=1:raise SystemExit('Expected one GRUB default setting')
    expected='wexunattend=/setup/unattend.sif'
    if expected not in text:raise SystemExit('Unexpected unattended kernel argument')
    for scenario in ('missing-answer','wrong-target'):
        config=root/(scenario+'.cfg')
        content=text
        extra=[]
        if scenario=='wrong-target':
            answer=root/'wrong-target.sif'
            answer.write_text('[Unattended]\nTargetDisk=/dev/codex-missing-disk\nRepartition=Yes\n')
            content=content.replace(expected,'wexunattend=/run/live/medium/setup/codex-bad-target.sif')
            extra=['-map',str(answer),'/setup/codex-bad-target.sif']
        config.write_text(content)
        destination=out/('wineexplorer-'+scenario+'.iso')
        if destination.exists():raise SystemExit(f'Refusing to append to existing ISO: {destination}')
        subprocess.run(['xorriso','-indev',str(iso),'-outdev',str(destination),'-boot_image','any','replay',
                        '-map',str(config),'/boot/grub/grub.cfg',*extra,'-commit','-end'],check=True)
        print('Created',destination)
