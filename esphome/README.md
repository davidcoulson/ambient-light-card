# ESPHome effect library

The light-side half of the Ambient Light Card: about 80 effects for ESPHome lights, all driven by
the wall clock so every light in a room shows the same frame at the same moment – with no leader,
no messages between lights, and a light that reboots rejoins in step on its next frame.

Tested on ESP32-C3 PWM RGB+CW/WW bulbs and addressable rings (Lumary RGBAI, 36 or 48 LEDs;
13-LED Govee H60A1), ESPHome 2026.9.

## Files (`common/`)

| File | What it is |
| --- | --- |
| `light_effects_helpers.h` | The core: shared clock (`sync_seconds`, `sync_t`), deterministic dice (`sync_hash`, `sync_random`, `sync_noise`), lockstep simulation for particle effects, room Speed / Intensity (`fx_seconds`, `fx_t`, `fx_intensity`, `fx_swing`, `fx_accents`), output helpers. |
| `accent_light_nonaddressable_effects.yaml` | ~45 effects for a PWM RGB light (`platform: rgb`). |
| `white_effects.yaml` | Effects for the white channels of an RGBWW light (`platform: rgbww`). |
| `accent_light_addressable_effects.yaml` | ~37 effects for an addressable ring or strip. |
| `accent_light_2d_effects.yaml` | 29 room-scale 2D effects for PWM lights placed on a floor plan (waves crossing the room, a fire in the middle, ripples…). |
| `accent_light_ht2d_effects.yaml` | 2D effects for addressable rings, each LED placed on the room map. |
| `fx2d_render.h` | Shared 2D Pacifica and Aurora renderers. |
| `basement_map.h`, `home_theater_map.h` | **Example** room maps – replace with your own (see below). |
| `accent_fx_controls.yaml` | Imports the room's Speed and Intensity helpers from Home Assistant. |
| `halloween_effects.yaml` | Ten Halloween effects for a row of outdoor RGBCW bulbs along a front walk (`platform: rgbww`). |
| `halloween_weather.yaml` | Optional: live weather from Home Assistant for the Halloween effects. |
| `halloween_fx.h`, `front_yard_map.h` | Their maths, and an **example** yard map – replace with your own. |
| `ntp.yaml` | SNTP setup plus two health sensors: *NTP Sync* and *Last NTP Sync*. |

## Use it as a remote package (recommended)

ESPHome can pull the library straight from this repo – no copies to keep in sync. Pin `ref` to a
release tag so a change here never alters your lights until you bump it:

```yaml
substitutions:
  mac: "4e7dac"          # unique per light: names it, seeds its per-light offsets, finds it on the map
  area: "Basement"       # its room: picks input_number.basement_accent_speed / _intensity

packages:
  ambient:
    url: https://github.com/davidcoulson/ambient-light-card
    ref: v1.0.0
    files:
      - esphome/common/accent_fx_controls.yaml
      - esphome/common/accent_light_nonaddressable_effects.yaml
      # Room-map effects on your own map, kept in your config directory:
      - path: esphome/common/accent_light_2d_effects.yaml
        vars:
          room_map: common/my_room_map.h
```

Headers are named without a directory, so ESPHome finds them next to the package's files. A
header you keep locally (your room map) is found in your config directory first. Keep `ntp.yaml`
local too, or copy it: it is where your time servers go.

## Or copy it

Copy `common/` into your ESPHome config directory. Then in a device config:

```yaml
substitutions:
  mac: "4e7dac"          # unique per light: names it, seeds its per-light offsets, finds it on the map
  area: "Basement"       # its room: picks input_number.basement_accent_speed / _intensity
  ntp_server_1: 192.168.1.10   # see ../docs/time-sync.md

packages:
  ntp: !include common/ntp.yaml
  fx_controls: !include common/accent_fx_controls.yaml
  accent: !include common/accent_light_nonaddressable_effects.yaml
  # fx2d: !include common/accent_light_2d_effects.yaml   # lights on a room map
```

The effect files `!extend` a light with `id: accent_light`, and the PWM effects write to outputs
with these ids, so name yours to match:

| Light type | Light id | Output ids |
| --- | --- | --- |
| RGB | `accent_light` | `red_output`, `green_output`, `blue_output` |
| RGBWW (white effects) | `accent_light` | plus `warm_output`, `cold_output` |
| Addressable | `accent_light` | – (effects write the LED buffer) |

## Speed and Intensity

`accent_fx_controls.yaml` imports `input_number.<area>_accent_speed` and
`input_number.<area>_accent_intensity`, with `<area>` taken from the `area` substitution
(lower case, spaces → underscores). Both are 0–255 with **128 = normal**, the WLED convention:

- **Speed** multiplies each effect's clock – 64 steps doubles or halves it (0 = ¼×, 128 = 1×,
  255 ≈ 4×). Every light gets the same number, so the room stays in step; a change makes the effect
  jump to a new position. Clock, Hourly Chime and Wake-Up Alarm ignore it.
- **Intensity** means something per effect: wave height and foam for Pacifica, fold sharpness for
  Aurora, flicker depth and gusts for candles, beat sharpness for Heartbeat…
  `fx_swing()` and `fx_accents()` are the two common shapes; both are exactly 1 at 128.

A room with no helpers never sends a value and its lights stay at 128 – the look the effects have
without these controls.

## Room maps for the 2D effects

The 2D effects sample one room-wide scene at each light's position. The map is a table of
`{mac, x, y}` in `basement_map.h` – x west → east and y north → south, each 0–1 on its own
axis, read off a floor plan, with `ROOM_ASPECT` (depth ÷ width) correcting the squash. Each light
looks itself up by its `mac` substitution; a light not in the table still runs the effects, just
scattered rather than placed. To add a light, add a row and bump `ROOM_N`. The ring version,
`home_theater_map.h`, adds each ring's centre, rotation ("where is LED 0, in clock hours") and
direction, so every LED gets its own position. Two calibration effects – *Calibrate: LED 0 1 2*
and *Calibrate: north + east* – help you find and check a ring's rotation.

`fire_x` / `fire_y` (default 0.5 / 0.5) set where the fire sits for *2D Hearth* and
*2D Single Candle*.

## Halloween effects for outdoor bulbs

`halloween_effects.yaml` adds ten effects for RGBCW bulbs placed along the route visitors walk
from the street to the front door: *Jack-o'-Lantern Row*, *Lightning Over the House*,
*Witch's Cauldron*, *Blood Moon*, *Heartbeat of the House*, *Trick-or-Treat Swap*,
*Turning of the Season*, *Witching Hour*, *Graveyard Mist* and *Wind-Blown Candles*. They mix the colour and white channels in one frame (pumpkin
is warm white plus red), so they need an `rgbww` light with all five outputs.

Two rules hold for all of them, and the host tests check both:

- **A light never goes dark.** Every effect has a floor colour – a dim ember, a violet glow – and
  no channel drops under it, so a light is always at least ~3 % of full warm white.
- **Minimal flashing.** At most 1 flash per second per light and per group of neighbouring lights,
  counted the WCAG 2.3.1 way (the limit there is 3). Occasional events – a candle guttering, a
  bubble – take turns within a group so neighbours never fire in the same second. The room Speed
  helper can slow these effects but never speed them up, and Intensity is not used.

Three of them are made for running every evening of a month: *Turning of the Season* moves from
harvest gold on October 1 to pumpkin and violet on the 31st, *Witching Hour* moves from amber at
lights-on (an hour before sunset) through violet and teal to red at 11 pm, and *Graveyard Mist*
picks a new palette, direction and pace each night. They read the local date and hour, so they
use the unscaled clock and need the time component's timezone to be right; *Witching Hour* also
needs `sun_latitude` / `sun_longitude` for its sunset (the default is New York City).

Opt a light in from its device file, next to its own package. A light that does not include the
file is unchanged:

```yaml
substitutions:
  mac: "a1b2c3"
  area: "Outdoors"       # one area for the whole set, so they share one Speed helper
  yard_light: "G2"       # this light's row in the yard map

packages:
  light: !include common/athom-light.yaml
  halloween:
    url: https://github.com/davidcoulson/ambient-light-card
    ref: <release tag>
    files:
      - path: esphome/common/halloween_effects.yaml
        vars:
          yard_map: common/my_front_yard_map.h   # your copy of front_yard_map.h
          sun_latitude: "<your latitude>"            # e.g. "40.7"
          sun_longitude: "<your longitude>"          # e.g. "-74.0"
      - esphome/common/halloween_weather.yaml        # optional
```

**Daylight lift.** Outdoor lights usually come on before dark, when a dim floor or a slow mist
is invisible. While it is light out, every Halloween effect gets a fixed extra amount of each
frame's own colour – up to 0.25 on its brightest channel, within the power cap – fading out
between the sun at +6° and −4°. The amount added doesn't follow the frame's ups and downs, so
flicker and pulses keep their night-time swing and no flashes are added; floors only rise.
Ambient light comes from the sun (`sun_latitude` / `sun_longitude`) or, with
`halloween_weather.yaml`, from measured light (`wx_lux`, `wx_lux2`: none under 5 lx, full above
400 lx), which ends the day early on an overcast evening. It is one call, `hw_lift()` in
`hw_finish()`, plus the 10 s interval that sets `g_hw_amb`.

`halloween_weather.yaml` reads a weather station through Home Assistant (the defaults are
`sensor.tempest_*` names; set your station's entities with the `wx_*` substitutions, in the units
HA reports in US units: mph, degrees, in/h, °F). Real gusts bend *Wind-Blown Candles* in turn
from the windward side; the wind steers which way *Turning of the Season* rolls and *Graveyard
Mist* drifts; rain, cold and air near the dew point tint the candles. Readings reach each bulb a
moment apart, so they only set slow parameters – rounded, latched per cycle or eased over ~10 s –
and never time a beat. A reading that is unavailable or unknown, or a lost API connection, falls
back to calm defaults. Without the package everything still compiles and runs, as on a still, dry
night.

The yard map is a table of `{name, group, x, y, path}` in feet: the group says which part a light
plays (S – the street bed, G – the driveway wall, D – the front door), x/y place it, and `path` is
its distance from the street along the walk. A light finds its row by its `yard_light` name rather
than its MAC, so a bulb can be replaced without editing the map. A light that is not on the map
still runs every effect, as a lone driveway light. *Blood Moon* uses x for its eclipse sweep; the
other three use the groups.

### Keeping them off every other light

Give the outdoor bulbs their own wrapper next to their family package, and have only their device
files include it. Indoor lights keep including the plain family package and never see a Halloween
effect or a weather sensor:

```yaml
# common/athom-outdoor.yaml - the yard bulbs, and only them
substitutions:
  halloween_ref: <the same tag athom-light.yaml pins for ambient-light-card>
  sun_latitude: "<your latitude>"
  sun_longitude: "<your longitude>"

packages:
  light: !include athom-light.yaml
  halloween:
    url: https://github.com/davidcoulson/ambient-light-card
    ref: ${halloween_ref}
    files:
      - path: esphome/common/halloween_effects.yaml
        vars:
          sun_latitude: ${sun_latitude}
          sun_longitude: ${sun_longitude}
      - path: esphome/common/halloween_weather.yaml
        vars:
          wx_wind: sensor.<station>_wind_speed_average
          # ... the other wx_* entities
```

```yaml
# a yard bulb's device file
substitutions:
  mac: "a1b2c3"
  area: "Outdoors"
  yard_light: "G2"

packages:
  - !include common/athom-outdoor.yaml
```

**Pin both packages to the same ref.** `halloween_fx.h` uses `light_effects_helpers.h`, but it
doesn't bring its own: ESPHome copies every package's includes into one flat `src/` folder, and
the copy a device gets is the one its light package lists – `athom-light.yaml`'s
ambient-light-card ref, not the Halloween package's. If two packages listed the same header, the
last one merged would silently win. So a Halloween package pinned at one ref compiles against
the helpers of another. To make that safe, `light_effects_helpers.h` now carries
`LIGHT_EFFECTS_HELPERS_API` (bumped whenever a helper is added, removed or changes its result),
and `halloween_fx.h` refuses to compile against any other level. That covers both a helper it
needs that the light's ref doesn't have, and a helper whose behaviour changed under it. The
v1.2.0–v1.2.2 helpers have no marker and count as level 1, so they work with this release.
The simplest way to keep the rule is one substitution for the ref, used by both packages.

### Tests

`tests/run_halloween_tests.sh` builds `tests/halloween_test.cpp` against the real headers and
checks, on every light of the example map: same time and light → same frame; the floor holds;
flashes stay at or under 1 per second per light and per group – at 8 pm, at lights-on, and in a
simulated storm for the weather-driven effects; an unmapped light still runs; Speed slows but never
speeds up; `hw_out()` scales by brightness like `out_rgb()`; the cap and floors both hold; an
evening keeps its date past midnight and sunset lands within two minutes; the weather falls back
to calm on unknown readings or a lost API; and the daylight lift only raises channels, stays in
the cap, adds no flashes and follows the sun or measured light. Parity covers night, storm and
full-lift frames. It also checks the helpers guard: `halloween_fx.h` compiles against unmarked
v1.2.x helpers and fails against any other API level.

`tests/effect_list.py` reads `esphome config` output and lists a device's light effects and any
Halloween parts it pulls in. Use it to check that an indoor light is untouched (`--no-halloween`)
and that its effect list matches a saved one (`--compare`). With node
installed it then compares the C++ frame by frame against `tests/halloween_reference.js`, the
same maths in JavaScript that drives the effects' preview page.

## Keeping lights in step

Everything keys off the clock, so the lights need to agree on the time. Point them all at the same
local NTP server – [`../docs/time-sync.md`](../docs/time-sync.md) shows how with chrony on Home
Assistant. The *NTP Sync* sensor goes off if a light has not synced for 15 minutes; if a room drifts
apart, check that first (a firewall blocking NTP between VLANs is the usual cause).

Hard-won rules the code follows, worth knowing if you write your own effects:

- **Never cast epoch time to float.** A float cannot hold ~1.8e9 s usefully; `(float) T * k`
  freezes an effect. Fold in double first (`sync_fold`, `fx_wrap`) and only then go to float.
- **No per-light state or dice.** An effect that integrates its own motion (`t += dt * speed`) or
  calls `random_uint32()` drifts apart however good the clocks are. Use a closed form of the clock
  (`fx_wobble`), shared dice (`sync_hash`, `fx_gust`), or a `Lockstep` simulation.
- **Addressable effects can't bypass brightness.** ESPHome applies brightness and gamma to every
  LED; pre-apply inverse gamma if you need exact levels, and never add your own brightness on top.
- **`esphome logs` timestamps are the host's**, not the device's – don't measure clock agreement
  with them.
