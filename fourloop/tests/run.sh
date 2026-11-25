#!/bin/sh
# Host tests for 4loop.pack's glue (ASan/UBSan), plus a compile of every pack source with open-field's own
# flags (-Wall -Werror) for a 32-bit target, which is what the Blackfin build will see.
set -e
cd "$(dirname "$0")/.."
OUT=${OUT:-/tmp/fourloop-pack-test}; mkdir -p "$OUT"
FLAGS="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc"
g++ $FLAGS -c -x c++ src/fourloop_rt.cpp -DFL_RT_TEST -o "$OUT/rt.o"
g++ $FLAGS -Wall tests/host_test.cpp src/fourloop_glue.cpp src/fourloop_core.cpp "$OUT/rt.o" -o "$OUT/host_test"
"$OUT/host_test"
if [ -n "$OPEN_FIELD" ]; then
  PF="-O2 -std=gnu++11 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-common -nostdinc++ -Wall -Werror -Wno-unused-variable"
  for f in src/fourloop.cpp src/fourloop_glue.cpp src/fourloop_core.cpp src/fourloop_rt.cpp; do
    g++ -m32 $PF -I"$OPEN_FIELD/module" -fsyntax-only "$f"
  done
  echo "pack sources compile clean with open-field's flags (32-bit host)"
fi
