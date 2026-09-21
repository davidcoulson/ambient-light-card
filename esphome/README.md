# ESPHome effect library

The light-side half of the Ambient Light Card: about 80 effects for ESPHome lights, all driven by
the wall clock so every light in a room shows the same frame at the same moment – with no leader,
no messages between lights, and a light that reboots rejoins in step on its next frame.

Tested on ESP32-C3 PWM RGB+CW/WW bulbs and 48-LED addressable rings, ESPHome 2026.9.

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
