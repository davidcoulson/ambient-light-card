#!/usr/bin/env python3
"""List a device's light effects from `esphome config` output, and check isolation.

    esphome config my-light.yaml | python3 effect_list.py [--no-halloween] [--compare saved.txt]

Prints one effect name per line (in the order ESPHome validated them), then a
summary. --no-halloween fails if the config pulls in any Halloween effect,
Halloween header or weather sensor - for indoor lights, which must never get
them. --compare fails unless the effect list matches a saved list exactly.
Reads only names and include/sensor ids, never other config values. Input
that isn't a validated device config with a light (empty, or an error from a
failed `esphome config`) is a failure, never a pass.
"""
import re
import sys

HALLOWEEN_EFFECTS = {
    f"Halloween - {n}" for n in (
        "Jack-o'-Lantern Row", "Lightning Over the House", "Witch's Cauldron", "Blood Moon",
        "Heartbeat of the House", "Trick-or-Treat Swap", "Turning of the Season", "Witching Hour",
        "Graveyard Mist", "Wind-Blown Candles", "Spooky Island", "Pumpkin Patch",
    )
} | {
    f"Halloween - Fast {n}" for n in (
        "Jack-o'-Lantern Row", "Lightning Over the House", "Witch's Cauldron", "Blood Moon",
        "Heartbeat of the House", "Trick-or-Treat Swap", "Graveyard Mist", "Wind-Blown Candles",
    )
}
HALLOWEEN_MARKERS = re.compile(r"halloween_fx\.h|front_yard_map\.h|\bhw_wx_[a-z0-9_]+|hw_frame\(")


def effect_names(text):
    names, in_effects, indent = [], False, 0
    for line in text.splitlines():
        stripped = line.lstrip(" ")
        lead = len(line) - len(stripped)
        if re.match(r"effects:\s*$", stripped):
            in_effects, indent = True, lead
            continue
        if in_effects:
            if stripped and lead <= indent and not stripped.startswith("- "):
                in_effects = False
                continue
            m = re.match(r"(?:- )?name:\s*(.+?)\s*$", stripped)
            if m and lead <= indent + 6:
                names.append(m.group(1).strip("'\""))
    return names


def main(argv):
    text = sys.stdin.read()
    # Refuse anything that isn't a validated device config, so a failed
    # `esphome config` upstream can't pass as "no Halloween parts".
    if not re.search(r"^esphome:", text, re.M) or not re.search(r"^light:", text, re.M):
        print("FAIL: no validated ESPHome config with a light on stdin (did `esphome config` fail?)")
        return 2
    names = effect_names(text)
    for n in names:
        print(n)
    halloween = [n for n in names if n in HALLOWEEN_EFFECTS]
    markers = sorted(set(HALLOWEEN_MARKERS.findall(text)))
    print(f"-- {len(names)} effects; Halloween effects: {len(halloween)}; Halloween markers: {', '.join(markers) or 'none'}")
    ok = True
    if "--no-halloween" in argv and (halloween or markers):
        print("FAIL: this config pulls in Halloween parts")
        ok = False
    if "--compare" in argv:
        saved = [l.rstrip("\n") for l in open(argv[argv.index("--compare") + 1]) if l.strip() and not l.startswith("--")]
        if saved != names:
            print(f"FAIL: effect list differs from the saved one ({len(saved)} saved, {len(names)} now)")
            for n in sorted(set(saved) ^ set(names)):
                print("  differs:", n)
            ok = False
        else:
            print("effect list matches the saved one")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
