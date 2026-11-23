# C&C3 Kane's Wrath framerate unlocker

Play Command & Conquer 3: Kane's Wrath at your monitor's refresh rate (144,
165 fps and up) instead of 30, with the game speed unchanged and units moving
and animating smoothly. It also raises the zoom-out limit and adds a turbo key
for skirmish.

## Install

You need Kane's Wrath with the **4K Add-On** installed (it brings the 1.03
patch and the 1.3 game exe this is built for). On any other version it does
nothing and the game runs as normal.

1. Download `kwfps-<version>.zip` from Releases (or build it, below).
2. Open the game folder (Steam: right-click Kane's Wrath > Manage > Browse
   local files) and go into `RetailExe\1.3`.
3. Copy `dinput8.dll` and `kwfps.ini` there. With DXVK, copy `dxvk.conf` too
   (or add its line to yours): it's worth up to 2x in big fights.

After a game, `kwfps.log` in the same folder should say "hooks installed" and
"pacing on". To uninstall, delete the two files. Settings are in `kwfps.ini`
(below). Replays recorded with it play back normally; online multiplayer
hasn't been tested.

## How it works

It's a 32-bit `dinput8.dll` proxy that sits next to `RetailExe\1.3\cnc3ep1.dat`.
It forwards DirectInput to the system DLL, and only inside the game:

- **Pacing** (`[pacing]`): the stock engine runs a fixed number of simulation
  sub-steps per drawn frame, so game speed is tied to frame rate (30 fps stock).
  Pacing replaces `GameEngine::update` with a version that runs sub-steps on a
  real-time clock (6 per logic frame, 15 logic frames a second) and draws every
  frame with a continuous interpolation value. Game speed stays correct at any
  frame rate, and replays stay in sync.
- **Interpolation window fix**: units only interpolate if they moved in the last
  6 client frames (3 logic frames at stock 30 fps). At higher rates that cut
  interpolation short, and units jumped when they stopped. The window is scaled
  to the client rate.
- **Interpolation push fix**: the same 6-frame cut-off guards the position
  history push at the start of each logic frame. Units move in sub-steps 3 and
  5, ~7 client frames earlier at 162 fps, so the push was skipped and units
  jumped back and forth (227 jumps in 3,700 traced frames before, 1 after).
- **Animation clock**: model animations advanced by a fixed 1000/maxfps ms
  (an int, 6 at 162) per drawn frame. With pacing they advance by the game time
  each frame covers (1000 ms a second, whatever the frame rate).
- **Stream effect fix**: beam/flame streams added a segment per drawn frame and
  overflowed their vertex buffer at high fps (crash at 0x829369). Segments are
  emitted at the stock 30 a second, with a hard cap as a safety net.
- **Engine lock**: the engine's global recursive lock used a kernel mutex, so
  every lock and unlock was a kernel call. It uses a critical section instead
  (the bridge fight in a test replay: lowest 73 → 98 fps). `[debug] FastLock = 0`
  turns it off.
- **Camera** (`[camera] MaxHeight`): higher zoom-out ceiling, overriding map values.
- **Turbo** (`[keys] TurboKey`, F11): simulation flat out, drawing 1 frame in 16.
  Live matches only (replay playback paces itself to real time).
- **Income multiplier** (`[cheats] MoneyMult`): replays must be played back with
  the same value.
- **Timing probe**: one line a second in `kwfps.log` (rewritten each launch) (fps, per-part frame times,
  per-sub-step logic times), plus `SPIKE` lines for slow frames.

Every hook checks the bytes it replaces first. On a mismatch it logs and leaves
that part alone.

## Files

- `src/kwfps.c`: the DLL. Build with `./build.sh` (needs `gcc-mingw-w64-i686`);
  `./package.sh` makes the release zip.
- `test/loadtest.c`: console load test (no dialogs; never test with rundll32).
- `bench/` (my test rig, WSL-specific): `run.sh <name> <dxvk|reshade> [replay]`: plays a replay muted with the
  camera following the fighting, stops at the end, and keeps the log and an
  end-screen screenshot in `bench/logs/`.
- `bench/summary.py <logs>`: per-run averages, percentiles and spike causes.
- `package.sh`: builds `dist/kwfps-<version>.zip` to share (DLL, `share/kwfps.ini`,
  `share/README.txt`). It needs the 4K Add-On's 1.3 exe; on anything else the
  vtable check fails and the DLL patches nothing.

## kwfps.ini

```ini
[probe]
SpikeMs = 25

[camera]
MaxHeight = 500        ; stock 300; 0 leaves the game alone

[cheats]
MoneyMult = 0          ; 0 leaves the game alone

[keys]
TurboKey = 122         ; F11

[pacing]
Enabled = 1
ClientRate = 162       ; client frames a second the engine assumes; auto = refresh - 3 (120 Hz+) or refresh
FrameCap = 162         ; 0 = uncapped; auto = ClientRate
Trace = 0              ; frames of interpolation trace to log

[debug]
PushFix = 1            ; interpolation push window fix
AnimFix = 1            ; animation clock follows game time (pacing only)
PosTrace = 0           ; frames of one moving unit's drawn position to log (POS lines)
Profile = 0            ; sample the main thread; log PROF lines for seconds under this fps
```

`bench/posjumps.py <logs>` counts jumps in POS traces; `bench/window.py <log> [lf from to]`
summarises a stretch of a run.

## DXVK

With DXVK, set `d3d9.cachedWriteOnlyBuffers = True` in `dxvk.conf`. The game reads
back from write-only vertex buffers while building effect quads (0x62142a); in
uncached memory those reads stalled big fights at ~32 fps. With the option the
same fight bottoms out at ~78 fps (draw 20.8 → 8.8 ms).
