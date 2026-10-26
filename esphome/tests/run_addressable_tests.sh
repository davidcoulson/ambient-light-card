#!/bin/sh
# Flash-rate test for the addressable and ht2d ring effects: at most 2 flashes per
# second in any strip segment and over the whole strip (WCAG 2.3.1), on 13, 36 and
# 48 LED strips and on the home-theater ring for each light's MAC. Needs python3 and
# a C++17 compiler. Each run simulates 30 minutes.
#
#   sh esphome/tests/run_addressable_tests.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

python3 "$here/addressable_cases.py" "$here/../common/accent_light_addressable_effects.yaml" \
  "$here/../common/accent_light_ht2d_effects.yaml" > "$out/addressable_extracted.h"
${CXX:-c++} -std=c++17 -O2 -w -I"$here/../common" -I"$out" \
  "$here/addressable_flash_test.cpp" -o "$out/addressable_flash_test"

for leds in 36 13 48; do
  echo "== strip of $leds LEDs"; "$out/addressable_flash_test" --leds $leds --seconds 1800
done
for mac in e018f8 e01994 e019d4 e05370; do
  echo "== ring, MAC $mac"; "$out/addressable_flash_test" --ring --mac $mac --seconds 1800
done
