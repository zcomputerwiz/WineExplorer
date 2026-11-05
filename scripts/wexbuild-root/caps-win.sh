#!/bin/bash
export DA=0 FX=0 MAT=0
for t in win10 win11; do
  echo "== $t"
  bash /root/p13-parts.sh $t "Z:\\root\\themes-local\\$t.msstyles" 2>&1 | tr -s ' ' | sed 's/ part /\n part /g' | grep -E '^ part (15|17|18|21):' | cut -c1-45
done
