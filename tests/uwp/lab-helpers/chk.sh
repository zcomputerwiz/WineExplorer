cd /home/debian/uwp-u1-20261111/build/candidates/u11-2/build/include
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
for t in IAsyncOperation_HSTRING IAsyncOperation_IVector_HSTRING IAsyncOperation_IBuffer IAsyncOperation_BasicProperties IAsyncOperation_StorageFile IAsyncOperation_IStorageItem IAsyncOperation_IVectorView_StorageFile IAsyncOperation_IVectorView_StorageFolder IAsyncOperation_IVectorView_IStorageItem IAsyncOperation_IRandomAccessStream IAsyncAction IVector_HSTRING IIterable_HSTRING IAsyncOperation_IRandomAccessStreamWithContentType; do
  printf '%-48s' $t; grep -l "^#define $t " *.h 2>/dev/null | tr '\n' ' '; echo
done
true
