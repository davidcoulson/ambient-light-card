# Ambient Light Card

A compact Home Assistant card for a room's ambient / accent lights, sized to fit half of a
small wall panel (it was built for an NSPanel Pro). Instead of an 80-item effect dropdown you get:

- **Effect tiles** – your favourite effects as small rounded tiles, each an animated colour field
  that shows how the effect *feels* (a rolling swell for Pacifica, folding curtains for Aurora, a
  flickering warm glow for a candle, a lub-dub for a heartbeat) rather than a picture of it. The
  running effect's tile animates at the room's current speed and intensity; the rest stay still.
  A **⋯** tile opens the full list.
- **Brightness, Speed and Intensity** – three four-step sliders with icons. Tap a step or drag.
- **A corner glow** – a soft live preview of the running effect, fading in from the top-right
  corner of the card.
- **On/off** – tap the round header icon, like the tile card. It fills with the light's colour
  when on.
- **A visual editor** – pick the light, helpers, name, icon and favourite effects from the
  dashboard editor, no YAML needed.

Speed and Intensity follow the WLED convention: 0–255 with **128 = normal**. They are stored in two
Home Assistant helpers per room, and the ESPHome effect library in [`esphome/`](esphome/) reads
them, so every light in the room changes together and stays in step.

> The card works with any light that has effects. The Speed and Intensity rows only do something
> if the lights read the helpers – the ESPHome library here does; other firmware will ignore them.

## Install

### HACS (custom repository)

1. HACS → ⋮ → **Custom repositories** → add `https://github.com/davidcoulson/ambient-light-card`,
   type **Dashboard**.
2. Install **Ambient Light Card**, then reload the browser.

### Manual

1. Copy `ambient-light-card.js` to `/config/www/`.
2. Settings → Dashboards → ⋮ → **Resources** → add `/local/ambient-light-card.js` as a
   **JavaScript module**. Add `?v=2`, `?v=3`… to the URL when you update the file so panels
   don't keep a cached copy.

## Set up the room helpers

Create two **Number** helpers per room (Settings → Devices & services → Helpers → Create helper →
Number):

| Setting | Value |
| --- | --- |
| Name | `<Room> Accent Speed` and `<Room> Accent Intensity` |
| Minimum / Maximum / Step | 0 / 255 / 1 |
| Display mode | Slider |

Then set both to **128**. The entity ids must be `input_number.<room>_accent_speed` and
`input_number.<room>_accent_intensity`, where `<room>` is the light's ESPHome `area` substitution
in lower case with spaces as underscores (`Home Theater` → `home_theater`). That is how each light
finds its room's helpers without any per-device config.

## Configure the card

Add the card from the dashboard editor (search for *Ambient Light Card*) and fill in the form, or
use YAML:

```yaml
type: custom:ambient-light-card
entity: light.home_theater_accent_lights          # the room's light group
speed: input_number.home_theater_accent_speed
intensity: input_number.home_theater_accent_intensity
name: Ambient Lights
icon: mdi:television-ambient-light
favorites:                                        # optional, up to 11 tiles, in this order
  - 2D Pacifica
  - 2D Aurora (Solar Storm)
  - Candle Flicker
  - Ember Ring
  - Heartbeat Pulse
```

| Option | Required | Description |
| --- | --- | --- |
| `entity` | yes | Light or light group. Control the room's group so every light changes together. |
| `speed` | no | `input_number` 0–255. Omit to hide the Speed row. |
| `intensity` | no | `input_number` 0–255. Omit to hide the Intensity row. |
| `name` | no | Header text. Default *Ambient lights*. |
| `icon` | no | Header icon. |
| `favorites` | no | Effect names shown as tiles, up to 11. Default: one each of Pacifica, Aurora, Candle, Hearth/Ember and Heartbeat that the light has. |
| `effects` | no | Limit the **⋯** list. Default: the light's full effect list, minus `Calibrate:` effects. |

The card uses `ha-card`, so `card_mod` styling works as for any built-in card (the NSPanel setup
this was built for uses a blurred, semi-transparent glass style). The slider knobs, active icons and
selected tile use your theme's primary colour; set `--ambient-light-card-accent` to change it. The
header icon takes the light's own colour, like the tile card.

### The sliders

| Row | Steps | Sets |
| --- | --- | --- |
| Brightness | 20 / 50 / 80 / 100 % | `light.turn_on` `brightness_pct` |
| Speed | tortoise / walk / car / rocket = 64 / 128 / 192 / 255 | ½× / 1× / 2× / 4× |
| Intensity | 64 / 128 / 192 / 255 | calmer / normal / more / most |

The Intensity icons follow the running effect: waves for the ocean effects, flames for the fire
effects, weather (sun → storm) for everything else.

### Effect tiles

Every effect gets a tile. The design is chosen from the effect's name:

| Family | Matches (in the name) | Tile |
| --- | --- | --- |
| Ocean | pacifica, tide, shore, caustics, aquarium | layered swells with foam where waves cross; lagoon / storm / deep variants recoloured |
| Aurora | aurora, northern | folding green-to-violet curtains; solar storm / pastel / red sky variants |
| Candle | candle | warm glow rising from the bottom, flickering, with the odd gutter |
| Fire | fire, ember, hearth | redder, churning heat with flares |
| Heartbeat | heartbeat | lub-dub pulse from the centre |
| Metronome | metronome | sharp warm flash on the beat, alternating sides |
| Twinkles | twinkle, fairy, firefly, lullaby, sparkle, stars, milky way, bioluminescence, confetti, swarm | points of light fading in and out |
| Colour cycle | rainbow, color loop, color drift, pinwheel, unison random | hue sweeping across |
| Blobs | plasma, lava, metaball, noise, clouds, black hole, flood | slow morphing fields |
| Storm / strobe | lightning, strobe, police, tv simulator, flicker (not candles) | dark with flashes (police alternates red / blue) |
| Movers | chase, comet, meteor, scanner, sinelon, lighthouse, radar, pac-man, wave, … | a streak with a trail |
| Ripples | ripple, shockwave, shore break, bubbles, rainfall, fireworks | rings spreading from the middle |
| Sky | sunrise, sunset, moon | slow sky gradient |
| Rhythm | polyrhythm, downbeat, clock, chime, game of life | bars pulsing at their own rates |
| Soft | fog, soft glow, breathing, gentle, whisper, hush | slow, low-contrast breathing |
| Anything else | – | a gentle two-colour drift, colours picked from the effect's name |

## The lights: ESPHome effect library

[`esphome/`](esphome/) has the effect library the card was designed around – around 80 effects for
PWM RGB / RGBWW lights and addressable rings, all phase-locked to the wall clock so a room of
lights moves as one without talking to each other, plus room-map 2D effects and the Speed /
Intensity import. See [`esphome/README.md`](esphome/README.md).

Good sync needs every light on the same time source – see
[`docs/time-sync.md`](docs/time-sync.md) for running chrony on Home Assistant and pointing the
lights at it.

## Credits

- Pacifica after Mark Kriegsman's FastLED Pacifica.
- Speed / Intensity 0–255 with 128 as normal, after WLED.

## License

MIT – see [LICENSE](LICENSE).
