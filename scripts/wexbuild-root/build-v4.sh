set -eu
R=/root/verify-v4
L=/root/verify-v4-logs
mkdir -p $L
if [ ! -d $R ]; then
  git clone -q https://github.com/zcomputerwiz/WineExplorer $R
fi
cd $R
git fetch -q origin
git checkout -q -B master origin/master
git submodule update --init -q
cd wine
git fetch -q origin wineexplorer/11.19
git checkout -q 6d7ccf81a1f542c6959ba35abdee8ae359047baa
cd ..
echo "super $(git rev-parse --short HEAD) wine $(git -C wine rev-parse --short HEAD)" > $L/heads.txt
scripts/assemble.sh > $L/assemble.log 2>&1
mkdir -p build/wow64
cd build/wow64
../../wine/configure --enable-archs=i386,x86_64 > $L/configure.log 2>&1
echo configure $? >> $L/heads.txt
make -j16 > $L/make.log 2>&1
echo make $? >> $L/heads.txt
