# kwfps

Timing probe (and, later, frame pacing) for Command & Conquer 3: Kane's Wrath.

Builds a 32-bit `dinput8.dll` proxy that sits next to `RetailExe\1.3\cnc3ep1.dat`.
It forwards DirectInput to the system DLL and, only inside the game, swaps a few
engine vtable slots to time each part of a frame. Results go to `kwfps.log`
beside the DLL: one line a second, plus a `SPIKE` line for any frame slower
than `SpikeMs` in `kwfps.ini`.

Build: `./build.sh` (needs `gcc-mingw-w64-i686`).
