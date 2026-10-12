#!/bin/sh
# Host tests for the Halloween effects. Needs a C++17 compiler; the parity
# step also needs node.
#
#   sh esphome/tests/run_halloween_tests.sh
set -e
export TZ=America/New_York      # the calendar effects read local time; the C++ test pins the same zone
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -I"$here/../common" \
  "$here/halloween_test.cpp" -o "$out/halloween_test"

# Lockstep: no Halloween effect may run on a Speed-scaled clock (a Speed change
# lands on each bulb at a different moment and would jump their phases).
if grep -nE 'hw_seconds|fx_seconds|fx_t\(|fx_ms|fx_millis' "$here/../common/halloween_effects.yaml" "$here/../common/halloween_fx.h"; then
  echo "FAIL a Halloween effect uses a Speed-scaled clock"; exit 1
fi
echo "lockstep: every Halloween effect runs on the unscaled shared clock"

# The helpers guard: halloween_fx.h must compile against unmarked v1.2.x
# helpers (API level 1) and refuse any other level.
printf '#include "front_yard_map.h"\nint main() { return 0; }\n' > "$out/guard.cpp"
mkdir -p "$out/v12" "$out/v2"
grep -v 'define LIGHT_EFFECTS_HELPERS_API' "$here/../common/light_effects_helpers.h" > "$out/v12/light_effects_helpers.h"
sed 's/define LIGHT_EFFECTS_HELPERS_API 1/define LIGHT_EFFECTS_HELPERS_API 2/' "$here/../common/light_effects_helpers.h" > "$out/v2/light_effects_helpers.h"
# a quoted include looks next to the including file first, as it would in ESPHome's flat src/
for d in v12 v2; do cp "$here/../common/halloween_fx.h" "$here/../common/front_yard_map.h" "$out/$d/"; done
stubs='-include cstdint -Dmillis()=0u -Dmicros()=0u'
if ${CXX:-c++} -std=c++17 -fsyntax-only $stubs -I"$out/v12" "$out/guard.cpp" 2>"$out/g1.txt"; then
  echo "helpers guard: unmarked v1.2.x helpers accepted"
else
  echo "FAIL helpers guard: unmarked v1.2.x helpers rejected"; cat "$out/g1.txt"; exit 1
fi
if ${CXX:-c++} -std=c++17 -fsyntax-only $stubs -I"$out/v2" "$out/guard.cpp" 2>"$out/g2.txt"; then
  echo "FAIL helpers guard: a different API level compiled"; exit 1
elif grep -q "different API level" "$out/g2.txt"; then
  echo "helpers guard: a different API level is a compile error"
else
  echo "FAIL helpers guard: failed for another reason"; cat "$out/g2.txt"; exit 1
fi
"$out/halloween_test"

if command -v node >/dev/null 2>&1; then
  # Two steps, not a pipe: sh only reports a pipe's last command, so a crashed
  # dump would hide behind a passing parity check.
  "$out/halloween_test" --dump > "$out/dump.csv"
  node "$here/halloween_parity.js" < "$out/dump.csv"
else
  echo "node not found: skipping the parity check against halloween_reference.js"
fi
