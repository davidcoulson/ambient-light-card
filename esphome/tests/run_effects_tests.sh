#!/bin/sh
# Host tests for the effect library. Needs python3 and a C++17 compiler; the
# card test also needs node.
#
#   sh esphome/tests/run_effects_tests.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
common="$here/../common"
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

# The effects under test, pulled out of the YAML as they are. Two copies of a
# stateful effect act as two lights.
python3 "$here/extract_lambda.py" "$common/accent_light_addressable_effects.yaml" \
  "fireworks=Fireworks Burst" \
  "pacifica_a=Pacifica" "pacifica_b=Pacifica" \
  "lagoon_a=Pacifica (Calm Lagoon)" "lagoon_b=Pacifica (Calm Lagoon)" \
  "storm_a=Pacifica (Storm)" "storm_b=Pacifica (Storm)" \
  "deep_a=Pacifica (Deep Current)" "deep_b=Pacifica (Deep Current)" > "$out/addressable.h"
python3 "$here/extract_lambda.py" "$common/accent_light_nonaddressable_effects.yaml" \
  "polyrhythm=Polyrhythm" > "$out/pwm.h"
cat "$out/addressable.h" "$out/pwm.h" > "$out/effects_extracted.h"

${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
  -Wno-unused-but-set-variable -I"$common" -I"$out" "$here/effects_test.cpp" -o "$out/effects_test"
"$out/effects_test"

# Every plain lambda effect: at most 3 flashes/s per light and per group, and the
# brightness floors of effects_flash_floors.h. Forks one child per light, so it takes minutes.
{ python3 "$here/extract_lambda.py" --all rgb "$common/accent_light_nonaddressable_effects.yaml"
  python3 "$here/extract_lambda.py" --all w "$common/white_effects.yaml"
  python3 "$here/extract_lambda.py" --all d "$common/accent_light_2d_effects.yaml"; } > "$out/effects_flash_extracted.h"
${CXX:-c++} -std=c++17 -O2 -w -I"$common" -I"$out" -I"$here" "$here/effects_flash_test.cpp" -o "$out/effects_flash_test"
"$out/effects_flash_test"

# No unsigned draw may have a constant taken off it before it is made signed:
# (hash % 200) - 100 on a uint32_t wraps to ~4.3e9 instead of going negative.
if grep -nE '(rand|hash)[a-z0-9_]*\([^()]*\) *% *[0-9]+\) *- *[0-9]' "$common"/*.yaml "$common"/*.h | grep -v '(int) *(' ; then
  echo "FAIL an unsigned draw has a constant subtracted before it is cast to a signed type"; exit 1
fi
echo "lint: no unsigned draw goes negative by wrapping"

if command -v node >/dev/null 2>&1; then
  node "$here/../../tests/card_test.js"
else
  echo "node not found: skipping the card test"
fi
