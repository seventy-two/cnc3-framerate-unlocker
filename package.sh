#!/bin/sh
# Build the shareable zip: dist/kwfps-<version>.zip with the DLL, a default
# kwfps.ini and install notes (Windows line endings for Notepad).
set -e
cd "$(dirname "$0")"
./build.sh
v=$(sed -n 's/^#define KWFPS_VERSION "\(.*\)"/\1/p' src/kwfps.c)
rm -rf dist/stage && mkdir -p dist/stage
cp build/dinput8.dll dist/stage/
for f in kwfps.ini README.txt; do sed 's/$/\r/' share/$f > dist/stage/$f; done
rm -f dist/kwfps-$v.zip
python3 -c "import sys,zipfile; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); [z.write('dist/stage/'+f,f) for f in sys.argv[2:]]" dist/kwfps-$v.zip dinput8.dll kwfps.ini README.txt
rm -rf dist/stage
echo dist/kwfps-$v.zip
