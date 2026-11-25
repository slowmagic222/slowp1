#!/bin/sh
# Host tests: the core's TINE character against the OG TINE 0.2 engine, the FELT path against FELT v16 (unchanged),
# then roads.pack's engine against the core.
# usage: sh tests/run.sh <open-field repo> [roads.msb] [felt.msb]   (32-bit build: needs gcc -m32 / g++ -m32)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
bank=${2:-"$here/bank/roads.msb"}
felt=${3:-}
out=${TMPDIR:-/tmp}/roads-pack-test; mkdir -p "$out"
san="-fsanitize=address,undefined -fno-sanitize-recover=all"
flags="-O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable"
cc -std=c99 $flags $san -Werror -Wconversion -Wno-sign-conversion -c "$here/core/ms_core.c" -o "$out/ms.o"
cc -std=c99 $flags $san -c "$here/tests/og-tine/og_ref.c" -o "$out/og_tine.o" -w
cc -std=c99 $flags $san "$here/tests/test_tine.c" "$out/ms.o" "$out/og_tine.o" -lm -o "$out/test_tine"
"$out/test_tine" "$here/bank/roads-rr.msb"
if [ -n "$felt" ]; then
  cc -std=c99 $flags $san -c "$here/tests/og/og_ref.c" -o "$out/og.o" -w
  cc -std=c99 $flags $san "$here/tests/test_core.c" "$out/ms.o" "$out/og.o" -lm -o "$out/test_core"
  "$out/test_core" "$felt"
fi
cc -std=c99 $flags $san "$here/tests/wash_ab.c" -o "$out/wash_ab"
"$out/wash_ab"
c++ -m32 -std=c++11 $flags -DOP1F_HOST -I"$of/module" -I"$here" "$here/tests/host_test.cpp" -lm -o "$out/host_test"
"$out/host_test" "$bank"
"$out/host_test" "$here/bank/roads-rr.msb" | tail -1
