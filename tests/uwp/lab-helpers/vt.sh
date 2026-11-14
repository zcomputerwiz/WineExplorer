h=/home/debian/uwp-u1-20261111/build/candidates/u11-2/build/include/windows.foundation.h
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
for t in __FIMap_2_HSTRING_IInspectableVtbl __FIObservableMap_2_HSTRING_IInspectableVtbl __FIIterable_1___FIKeyValuePair_2_HSTRING_IInspectableVtbl __FIIterator_1___FIKeyValuePair_2_HSTRING_IInspectableVtbl __FIKeyValuePair_2_HSTRING_IInspectableVtbl __FIMapView_2_HSTRING_IInspectableVtbl; do
  n=$(grep -n "^typedef struct $t" $h | head -1 | cut -d: -f1)
  echo "== $t line $n"
  sed -n "$((n+1)),$((n+60))p" $h | sed -n '/BEGIN_INTERFACE/,/END_INTERFACE/p' | grep -E 'STDMETHODCALLTYPE|\*[A-Za-z_]*\)\(' | grep -vE 'QueryInterface|AddRef|Release|GetIids|GetRuntimeClassName|GetTrustLevel' | cut -c1-210
done
