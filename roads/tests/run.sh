#!/bin/sh
# Host tests: the block wash, then roads.pack's engine against its sample core.
# usage: sh tests/run.sh <open-field repo> [roads.msb]   (32-bit build: needs gcc -m32 / g++ -m32)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
bank=${2:-"$here/bank/roads.msb"}
out=${TMPDIR:-/tmp}/roads-pack-test; mkdir -p "$out"
san="-fsanitize=address,undefined -fno-sanitize-recover=all"
flags="-O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable"
cc -std=c99 $flags $san -Werror -Wconversion -Wno-sign-conversion -c "$here/core/ms_core.c" -o "$out/ms.o"
cc -std=c99 $flags $san "$here/tests/wash_ab.c" -o "$out/wash_ab"
"$out/wash_ab"
c++ -m32 -std=c++11 $flags -DOP1F_HOST -I"$of/module" -I"$here" "$here/tests/host_test.cpp" -lm -o "$out/host_test"
"$out/host_test" "$bank"
"$out/host_test" "$here/bank/roads-rr.msb" | tail -1
