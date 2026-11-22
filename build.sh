#!/bin/sh
# Cross-compile the 32-bit dinput8.dll proxy.
set -e
cd "$(dirname "$0")"
mkdir -p build
nice -n 19 i686-w64-mingw32-gcc -O2 -Wall -Wextra -shared -static-libgcc \
  -o build/dinput8.dll src/kwfps.c -Wl,--kill-at -Wl,--enable-stdcall-fixup
