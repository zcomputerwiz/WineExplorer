#!/bin/bash
set -euo pipefail
SHA=${1:?sha}
D=/root/WE-bisect-$SHA
rm -rf $D
git clone -q --no-hardlinks /root/WE-prefetch $D
cd $D
git submodule update --init --reference /root/WE-prefetch/wine wine 2>&1 | tail -1 || true
git -C wine fetch -q /root/WE-prefetch/wine 'refs/heads/*:refs/remotes/pf/*' 2>&1 | tail -1 || true
git -C wine checkout -q $SHA
git -C wine log --oneline -1
bash scripts/assemble.sh > /root/bisect-$SHA-assemble.log 2>&1
mkdir -p build/wow64 && cd build/wow64
../../wine/configure --enable-archs=i386,x86_64 --prefix=/usr/local > /root/bisect-$SHA-configure.log 2>&1
make -j16 > /root/bisect-$SHA-make.log 2>&1
make install DESTDIR=/root/bisect-stage-$SHA > /root/bisect-$SHA-install.log 2>&1
echo BUILT $SHA
ls /root/bisect-stage-$SHA/usr/local
