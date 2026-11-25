#!/bin/sh
# Host test of pendulum.pack (32-bit, needs g++ -m32). usage: sh tests/run.sh <open-field repo> [out dir]
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
of=${1:?the open-field repo, for module/build_r6e_card.h}
out=${2:-${TMPDIR:-/tmp}/pendulum-test}; mkdir -p "$out"
c++ -m32 -std=c++11 -O1 -g -Wall -Wextra -Wno-unused-function -fsanitize=address,undefined -fno-sanitize-recover=all \
  -DOP1F_HOST -I"$of/module" "$here/tests/host_test.cpp" -lm -o "$out/host_test"
"$out/host_test" "$out"
