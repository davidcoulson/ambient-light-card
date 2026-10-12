#!/bin/sh
# Host tests for the Halloween effects. Needs a C++17 compiler; the parity
# step also needs node.
#
#   sh esphome/tests/run_halloween_tests.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -I"$here/../common" \
  "$here/halloween_test.cpp" -o "$out/halloween_test"
"$out/halloween_test"

if command -v node >/dev/null 2>&1; then
  "$out/halloween_test" --dump | node "$here/halloween_parity.js"
else
  echo "node not found: skipping the parity check against halloween_reference.js"
fi
