#pragma once

// ---------------------------------------------------------------------------
// The media room, as the ring 2D effects see it. Mirrors basement_map.h: each
// Govee ring finds its own row by MAC suffix, so the four device files carry
// no coordinates. Move a ring here, not in its device file.
//
// Unlike a basement light, a ring is 13 LEDs. Each LED gets its own point in
// the room - the ring's centre plus an offset around a circle - so a wave can
// sweep across a ring as it passes instead of switching the whole ring at once.
//
// x runs west -> east, y north -> south, each 0..1 on its own axis, measured
// to the room's inner walls on the plan (its bounding box: the angled
// north-east corner and the south-east notch are ignored). Letters are the
// plan's labels.
// ---------------------------------------------------------------------------

#include <strings.h>
#include "light_effects_helpers.h"

// Plan interior ~683 px wide by ~545 px deep. Same convention as the basement:
// ht_led_xy() returns y pre-scaled, so one unit is the same real distance on
// both axes (x 0..1, y 0..HT_ASPECT).
constexpr float HT_ASPECT = 0.798f;

// Radius of each ring's footprint IN THE FIELD, in room units. Deliberately
// larger than the real fitting: at true size a ring spans so little of the room
// that all 13 LEDs would sample nearly one point and light as a block. 0.12
// gives each ring a visible slice of a passing wave while the four footprints
// stay clear of each other (nearest centres are ~0.38 apart).
constexpr float HT_RING_FIELD_R = 0.12f;

struct HtRing {
  const char *mac;
  float x, y;       // 0..1 each axis, y NOT yet aspect-scaled
  float clock0;     // clock position LED 0 points to: 12 = north wall, 3 = east
  int8_t dir;       // +1 if LED numbers run clockwise ON THE PLAN, -1 if not
};

constexpr int HT_N = 4;
constexpr HtRing HT_MAP[HT_N] = {
    {"e018f8", 0.144f, 0.239f, 5.0f, 1},    // A  north-west
    {"e019d4", 0.144f, 0.712f, 7.0f, 1},    // B  south-west
    {"e01994", 0.668f, 0.239f, 7.0f, 1},    // C  north-east
    {"e05370", 0.668f, 0.712f, 10.923f, 1}, // D  south-east (10 + one LED; see below)
};
// clock0 / dir read off the fittings on 2026-09-21 with the "Calibrate: LED 0
// 1 2" effect: red (LED 0) and green (LED 1) positions, judged by the walls so
// that looking up at a ceiling ring did not mirror them. All four number
// clockwise; the rotations differ because the fittings were hung at different
// angles. To re-check, run that effect again and compare. Clock precision is
// 30 degrees, about one LED on a 13-LED ring.
//
// D was read as 10 o'clock, but "Calibrate: north + east" showed its red one
// LED clockwise of north. Nudged by exactly one LED spacing (12/13 hour), which
// moves the LED picked for north from 2 to 1; the other three checked out.

// This ring's row in HT_MAP, or -1 if its MAC is not on the map.
inline int ht_ring_self(const char *mac) {
  static int idx = -2;
  if (idx == -2) {
    idx = -1;
    for (int i = 0; i < HT_N; i++)
      if (strcasecmp(HT_MAP[i].mac, mac) == 0) {
        idx = i;
        break;
      }
  }
  return idx;
}

// Where LED `i` of `n` on this ring sits, in room units (x 0..1, y 0..HT_ASPECT).
// Clock positions are on the plan (12 = north), so y grows southward: LED 0 at
// 12 o'clock is straight up the page. An unmapped ring still runs, placed by a
// hash of its MAC.
inline void ht_led_xy(const char *mac, int i, int n, float &x, float &y) {
  float cx, cy, clock0 = 12.0f;
  int dir = 1;
  int k = ht_ring_self(mac);
  if (k >= 0) {
    cx = HT_MAP[k].x;
    cy = HT_MAP[k].y * HT_ASPECT;
    clock0 = HT_MAP[k].clock0;
    dir = HT_MAP[k].dir;
  } else {
    uint32_t h = mac_hash32(mac);
    cx = (h & 0xFFFFFFu) / 16777216.0f;
    cy = ((h >> 8) ^ 0x9E3779B9u) % 16777216u / 16777216.0f * HT_ASPECT;
  }
  // Bearing clockwise from north, in degrees, then to a screen-space angle.
  float bearing = clock0 * 30.0f + dir * 360.0f * (float) i / (float) (n > 0 ? n : 1);
  float a = bearing * 0.017453293f;
  x = cx + HT_RING_FIELD_R * sinf(a);   // east component
  y = cy - HT_RING_FIELD_R * cosf(a);   // north is -y on the plan
}

// Colour for one ring LED. ESPHome passes every addressable write through the
// light's gamma (2.8 here) and scales it by the brightness slider; an effect
// cannot bypass that - the correction is a protected member of the light. So
// pre-apply the INVERSE gamma: after ESPHome's curve the LED shows the intended
// linear value, matching the basement at 100 %. The slider still dims the ring
// through ESPHome; these effects use it only for character (calm vs lively), so
// nothing is dimmed twice. Returns bytes so this header needs nothing from
// ESPHome: the caller does it[i] = Color(R, G, B).
template<typename L>
inline void ht_ring_rgb8(L *light, float r, float g, float b, uint8_t &R, uint8_t &G, uint8_t &B) {
  static float last_gamma = -1.0f;
  static uint8_t lut[256];
  float gm = light->get_gamma_correct();
  if (gm != last_gamma) {
    last_gamma = gm;
    float inv = gm > 0.0f ? 1.0f / gm : 1.0f;
    for (int k = 0; k < 256; k++)
      lut[k] = (uint8_t) lroundf(255.0f * powf(k / 255.0f, inv));
  }
  R = lut[(int) lroundf(clamp01(r) * 255.0f)];
  G = lut[(int) lroundf(clamp01(g) * 255.0f)];
  B = lut[(int) lroundf(clamp01(b) * 255.0f)];
}
