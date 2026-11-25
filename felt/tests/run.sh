#!/bin/sh
# Host tests: the FELT core against the OG engine, then felt.pack's engine against the core.
# usage: sh tests/run.sh <open-field repo> [felt.msb]   (32-bit build: needs gcc -m32 / g++ -m32)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
bank=${2:-"$here/../felt-field-v1/bank/felt.msb"}
out=${TMPDIR:-/tmp}/felt-pack-test; mkdir -p "$out"
san="-fsanitize=address,undefined -fno-sanitize-recover=all"
flags="-O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-const-variable"
cc -std=c99 $flags $san -c "$here/tests/og/og_ref.c" -o "$out/og.o" -w
cc -std=c99 $flags $san -Werror -Wconversion -Wno-sign-conversion -c "$here/core/ms_core.c" -o "$out/ms.o"
cc -std=c99 $flags $san "$here/tests/test_core.c" "$out/ms.o" "$out/og.o" -lm -o "$out/test_core"
"$out/test_core" "$bank"
cc -std=c99 $flags $san "$here/tests/wash_ab.c" -o "$out/wash_ab"
"$out/wash_ab"
c++ -m32 -std=c++11 $flags -DOP1F_HOST -I"$of/module" -I"$here" "$here/tests/host_test.cpp" -lm -o "$out/host_test"
"$out/host_test" "$bank"
