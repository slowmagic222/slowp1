#!/bin/sh
# Renders pendulum/demo.gif: the page simulated over a scripted performance, quiet stretches cut.
# usage: sh tests/demo.sh <open-field repo> [work dir]   (needs g++ -m32, ffmpeg, gifsicle, python3)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
w=${2:-${TMPDIR:-/tmp}/pendulum-demo}; rm -rf "$w"; mkdir -p "$w/frames"
c++ -m32 -std=c++11 -O2 -DOP1F_HOST -I"$of/module" "$here/tests/demo_sim.cpp" -lm -o "$w/demo_sim"
"$w/demo_sim" "$w/frames" > /dev/null
# keep a frame when at least 4 notes fire within half a second of it
python3 -I - "$w/frames" <<'PY'
import sys
d = sys.argv[1]; n = [int(l.split()[0]) for l in open(d + "/activity.txt")]
keep = [i for i in range(len(n)) if i < 6 or sum(n[max(0, i - 7):i + 8]) >= 4]
open(d + "/keep.txt", "w").write("".join("file 'f%04d.ppm'\nduration 0.0666667\n" % i for i in keep) + "file 'f%04d.ppm'\n" % keep[-1])
PY
cd "$w/frames"
ffmpeg -loglevel error -y -f concat -i keep.txt -vf "fps=15,lutrgb=r='if(lt(val,14),0,val)':g='if(lt(val,14),0,val)':b='if(lt(val,14),0,val)',split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" -loop 0 "$w/raw.gif"
gifsicle -O3 --lossy=80 "$w/raw.gif" -o "$here/demo.gif"
