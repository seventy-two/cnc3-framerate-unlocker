kwfps 1.0: high frame rate for C&C3 Kane's Wrath
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
3. Copy dinput8.dll and kwfps.ini into that folder.
4. Play.

To check it's working, open kwfps.log in the same folder after playing. It
should say "hooks installed" and "pacing on".

Settings are in kwfps.ini (frame rate, zoom-out, turbo key).

Tips
- A G-Sync or FreeSync monitor gives the smoothest result.
- Big battles are limited by the CPU: expect dips to around 100 fps in
  huge late-game fights. The stock game drops further at those moments.

Uninstall
Delete dinput8.dll and kwfps.ini (and kwfps.log) from RetailExe\1.3.

Not tested in online multiplayer.
