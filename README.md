# kwfps

High frame rate and quality-of-life DLL for Command & Conquer 3: Kane's Wrath (Steam).

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
- **Camera** (`[camera] MaxHeight`): higher zoom-out ceiling, overriding map values.
- **Turbo** (`[keys] TurboKey`, F11): simulation flat out, drawing 1 frame in 16.
  Live matches only (replay playback paces itself to real time).
- **Income multiplier** (`[cheats] MoneyMult`): replays must be played back with
  the same value.
- **Timing probe**: one line a second in `kwfps.log` (fps, per-part frame times,
  per-sub-step logic times), plus `SPIKE` lines for slow frames.

Every hook checks the bytes it replaces first. On a mismatch it logs and leaves
that part alone.

## Files

- `src/kwfps.c`: the DLL. Build with `./build.sh` (needs `gcc-mingw-w64-i686`).
- `test/loadtest.c`: console load test (no dialogs; never test with rundll32).
- `bench/run.sh <name> <dxvk|reshade> [replay]`: plays a replay muted with the
  camera following the fighting, stops at the end, and keeps the log and an
  end-screen screenshot in `bench/logs/`.
- `bench/summary.py <logs>`: per-run averages, percentiles and spike causes.

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
ClientRate = 162       ; client frames a second the engine assumes; match FrameCap
FrameCap = 162         ; 0 = uncapped; a little under the refresh rate for G-Sync
Trace = 0              ; frames of interpolation trace to log
```
