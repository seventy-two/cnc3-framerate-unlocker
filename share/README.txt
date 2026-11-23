kwfps 1.1: high frame rate for C&C3 Kane's Wrath
==================================================

What it does
- Runs the game at your monitor's refresh rate (e.g. 144 or 165 fps instead
  of 30) with the game speed unchanged, and units moving and animating
  smoothly.
- Lets you zoom out further.
- F11 toggles turbo (fast-forward) in skirmish.
Replays recorded with it play back normally.

You need
- Kane's Wrath with the 4K Add-On installed (it brings the 1.03 patch and
  the 1.3 game exe this works with). On the plain 1.02 game this does
  nothing: the game runs as normal.

Install
1. Open the game folder, e.g. for Steam:
   Steam > Library > right-click Kane's Wrath > Manage > Browse local files
2. Go into RetailExe\1.3
3. Copy dinput8.dll and kwfps.ini into that folder. If you use DXVK, copy
   dxvk.conf too (or add its line to your own dxvk.conf).
4. Play.

To check it's working, open kwfps.log in the same folder after playing. It
should say "hooks installed" and "pacing on".

Settings are in kwfps.ini (frame rate, zoom-out, turbo key).

Tips
- A G-Sync or FreeSync monitor gives the smoothest result.
- Big battles are limited by one CPU core: expect short dips to around
  100 fps in the biggest fights. The stock game drops further there.
- DXVK (github.com/doitsujin/dxvk, the 32-bit d3d9.dll) runs this game
  faster than native Direct3D 9; use it with the included dxvk.conf.

Uninstall
Delete dinput8.dll, kwfps.ini, kwfps.log (and dxvk.conf if you copied it)
from RetailExe\1.3.

Not tested in online multiplayer.
