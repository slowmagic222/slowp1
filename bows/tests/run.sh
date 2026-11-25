#!/bin/sh
# Host test of bows.pack: the engine compiled for the PC (32-bit, ASan/UBSan) against the bank, then the takes read
# for pitch, clicks and level. usage: sh tests/run.sh <open-field repo> [bows.msb]   (needs g++ -m32 and python3 scipy)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
bank=${2:-"$here/bank/bows.msb"}
out=${TMPDIR:-/tmp}/bows-pack-test; mkdir -p "$out"; rm -f "$out"/*.wav
for f in bows.cpp bows_rt.cpp; do
  g++ -m32 -std=gnu++11 -O2 -ffreestanding -Wall -Werror -Wno-unused-variable -DOP1F_HOST -I"$of/module" -I"$here" -c "$here/$f" -o "$out/$f.o"
done
g++ -m32 -std=gnu++11 -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
  -DOP1F_HOST -I"$of/module" -I"$here" "$here/tests/host_test.cpp" -lm -o "$out/host_test"
"$out/host_test" "$bank" "$out"
python3 -I "$here/tests/check.py" "$out"
