#!/bin/sh
# partial.sh <name> <pacing 0|1> <stop lf> [replay]: play a replay on DXVK,
# sampling GPU load every second, and stop once the logic frame reaches <stop lf>.
name=$1; pacing=$2; stop=$3; replay=${4:-kwbench3.KWReplay}
R="/mnt/g/SteamLibrary/steamapps/common/Command and Conquer 3 - Kane's Wrath/RetailExe/1.3"
L="$R/kwfps.log"
here=$(cd "$(dirname "$0")" && pwd)
while tasklist.exe 2>/dev/null | grep -qi "cnc3ep1.dat"; do sleep 1; done
sed -i "s/^Enabled = [01]/Enabled = $pacing/" "$R/kwfps.ini"
# MAXH=<height> sets the zoom ceiling; ZOOMOUT=1 keeps the camera fully zoomed out
[ -n "$MAXH" ] && sed -i "s/^MaxHeight = .*/MaxHeight = $MAXH/" "$R/kwfps.ini"
cp "$R/d3d9-dxvk.dll" "$R/d3d9.dll"
rm -f "$L"
cd /tmp
"/mnt/c/Program Files (x86)/Steam/steam.exe" -applaunch 24810 -noaudio -noshellmap -replayGame "$replay" &
until grep -v SPIKE "$L" 2>/dev/null | tail -1 | grep -q "lf [1-9]"; do sleep 2; done
echo "$(date +%T) $name running"
# CAM="x y" (screen pixels on the minimap) holds the camera there instead of following the fighting
if [ -n "$CAM" ]; then
  Wc="$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')\\kwtest"
  (while :; do powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$Wc\\kw-click.ps1" -x ${CAM% *} -y ${CAM#* } >/dev/null 2>&1; sleep 10; done) &
else
  python3 /root/tools/kw/follow.py 3600 15 > /tmp/kwfollow.log 2>&1 &
fi
fpid=$!
nvidia-smi --query-gpu=timestamp,utilization.gpu,clocks.gr --format=csv,noheader -l 1 > "$here/logs/$name-gpu.csv" &
gpid=$!
W="$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')\\kwtest"
if [ "$ZOOMOUT" = 1 ]; then
  (while :; do powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$W\\kw-wheel.ps1" -x 2560 -y 1300 -n -15 >/dev/null 2>&1; sleep 20; done) &
  zpid=$!
fi
until [ "$(grep " fps " "$L" | grep -v SPIKE | tail -1 | grep -o 'lf [0-9]*' | cut -d' ' -f2)" -ge "$stop" ] 2>/dev/null || ! tasklist.exe 2>/dev/null | grep -qi "cnc3ep1.dat"; do sleep 3; done
kill $fpid $gpid $zpid 2>/dev/null
taskkill.exe /IM cnc3ep1.dat /F >/dev/null
sleep 2
cp "$L" "$here/logs/$name.log"
echo "$(date +%T) saved $name"
