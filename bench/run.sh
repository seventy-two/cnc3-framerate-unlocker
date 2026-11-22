#!/bin/sh
# run.sh <name> <dxvk|reshade> [replay]: play a replay muted with the camera
# following the fighting, stop when it ends, and keep kwfps.log as
# bench/logs/<name>.log. Needs the game closed first.
set -e
name=$1; renderer=$2; replay=${3:-kwbench2.KWReplay}
R="/mnt/g/SteamLibrary/steamapps/common/Command and Conquer 3 - Kane's Wrath/RetailExe/1.3"
L="$R/kwfps.log"
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$here/logs"
cp "$R/d3d9-$renderer.dll" "$R/d3d9.dll"
rm -f "$L"
cd /tmp
"/mnt/c/Program Files (x86)/Steam/steam.exe" -applaunch 24810 -noaudio -noshellmap -replayGame "$replay" &
# wait for the replay to start simulating (logic frame > 0)
until grep -v SPIKE "$L" 2>/dev/null | tail -1 | grep -q "lf [1-9]"; do sleep 2; done
echo "$(date +%T) replay running"
python3 /root/tools/kw/follow.py 3600 15 > /tmp/kwfollow.log 2>&1 &
fpid=$!
# the replay is over when the logic frame counter drops back to 0 or the log stops
until grep -v SPIKE "$L" | tail -1 | grep -qE "lf 0\s*$" || [ $(( $(date +%s) - $(stat -c %Y "$L") )) -gt 20 ]; do sleep 3; done
kill $fpid 2>/dev/null || true
echo "$(date +%T) replay finished"
taskkill.exe /IM cnc3ep1.dat /F >/dev/null
sleep 2
cp "$L" "$here/logs/$name.log"
echo "saved $here/logs/$name.log"
