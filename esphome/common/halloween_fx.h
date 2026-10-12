#pragma once

// ---------------------------------------------------------------------------
// Halloween effects for outdoor RGBCW bulbs (halloween_effects.yaml).
//
// Each effect is a pure function of the shared clock and of which light this
// is: hw_*(T, self) -> five PWM levels. Nothing is stored between frames and
// nothing is sent between lights, so a row of bulbs renders one scene, and a
// bulb that reboots is back in step on its next frame - the same contract as
// every other effect in this library.
//
// Two rules hold for every effect here, and tests/halloween_test.cpp checks
// both on every bulb of the example map:
//
//   1. A light never goes dark. Each effect has a floor colour (HW_EMBER and
//      friends below) and no channel ever drops under it, so a light is always
//      at least ~3 % of full warm white. People walk past these at night.
//   2. Minimal flashing. At most 1 flash per second per bulb and per group of
//      neighbouring bulbs, counted the WCAG 2.3.1 way (a flash is a pair of
//      opposing changes of >= 10 % luminance). The 2.3.1 limit is 3; these
//      aim far below it, preferring slow swells, fades and drifts.
//
// Rule 2 is also why these effects use hw_seconds() rather than fx_seconds():
// the room Speed helper can slow them down but never speed them up, so a
// slider at 255 cannot quadruple a flash rate. Intensity is not used.
//
// The maths is mirrored in tests/halloween_reference.js, which also drives
// the preview page; tests/halloween_parity.js checks the two agree.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdint>
#include <strings.h>
#include "light_effects_helpers.h"

// Five PWM levels, 0..1, at 100 % brightness.
struct HwColor {
  float r, g, b, ww, cw;
};

// Which part a light plays. S: the low lights in the street bed, G: the wall
// along the driveway, D: the pair at the front door.
enum : uint8_t { YARD_S = 0, YARD_G = 1, YARD_D = 2 };

// One row of a yard map (see front_yard_map.h). Feet; x west -> east, y north
// -> south (toward the street); path_ft is the distance from the street along
// the route visitors walk.
struct YardLight {
  const char *name;
  uint8_t group;
  float x_ft, y_ft, path_ft;
};

// A light as the effects see it: its row, its group, its place within the
// group (for turn-taking) and a fixed per-light offset from the shared dice.
struct HwSelf {
  uint32_t i;        // row index; salts this light's own draws
  uint8_t group;     // YARD_S / YARD_G / YARD_D
  uint8_t k, n;      // index within its group, size of the group
  float x_ft, y_ft, path_ft;
  float off;         // 0..1, spreads the scattered effects
  bool mapped;
};

// Find `name` (the device's yard_light substitution) in a map. A light that is
// not on the map still runs every effect: it plays a driveway light on its
// own, with its offset hashed from its MAC suffix.
inline HwSelf hw_lookup(const YardLight *map, int count, const char *name, const char *mac) {
  HwSelf s{};
  int row = -1;
  for (int r = 0; name != nullptr && r < count; r++) {
    if (strcasecmp(map[r].name, name) == 0) { row = r; break; }
  }
  if (row >= 0) {
    s.i = (uint32_t) row;
    s.group = map[row].group;
    s.x_ft = map[row].x_ft;
    s.y_ft = map[row].y_ft;
    s.path_ft = map[row].path_ft;
    for (int r = 0; r < count; r++) {
      if (map[r].group != s.group) continue;
      if (r < row) s.k++;
      s.n++;
    }
    s.off = sync_random(s.i + 11, 977);
    s.mapped = true;
    return s;
  }
  uint32_t h = 2166136261u;
  for (const char *p = mac; p != nullptr && *p; ++p) { h ^= (uint8_t) *p; h *= 16777619u; }
  s.i = 1000u + (h % 100000u);
  s.group = YARD_G;
  s.k = 0;
  s.n = 1;
  s.off = sync_random(h, 977);
  s.mapped = false;
  return s;
}

// The effect clock: the shared wall clock, slowed (never sped up) by the room
// Speed helper. See rule 2 above.
inline double hw_seconds() {
  double t = sync_seconds();
  float s = fx_speed();
  return s < 1.0f ? t * (double) s : t;
}

// ---- colour helpers ----
inline float hw_ease(float f) { return f * f * (3.0f - 2.0f * f); }
inline HwColor hw_scale(const HwColor &c, float k) { return {c.r * k, c.g * k, c.b * k, c.ww * k, c.cw * k}; }
inline HwColor hw_add(const HwColor &a, const HwColor &c) { return {a.r + c.r, a.g + c.g, a.b + c.b, a.ww + c.ww, a.cw + c.cw}; }
inline HwColor hw_mix(const HwColor &a, const HwColor &c, float t) {
  return {a.r + (c.r - a.r) * t, a.g + (c.g - a.g) * t, a.b + (c.b - a.b) * t, a.ww + (c.ww - a.ww) * t,
          a.cw + (c.cw - a.cw) * t};
}

// The channel-sum cap. Writing outputs directly bypasses color_interlock and
// constant_brightness, which normally keep an RGBCW bulb inside its power
// budget; an enclosed outdoor fitting makes that matter. 1.0 = one channel at
// full.
constexpr float HW_CAP = 1.3f;

// Cap, then hold every channel at or above the floor (rule 1), then clamp.
inline HwColor hw_finish(const HwColor &c, const HwColor &floor) {
  float s = c.r + c.g + c.b + c.ww + c.cw;
  float k = s > HW_CAP ? HW_CAP / s : 1.0f;
  return {clamp01(fmaxf(c.r * k, floor.r)), clamp01(fmaxf(c.g * k, floor.g)), clamp01(fmaxf(c.b * k, floor.b)),
          clamp01(fmaxf(c.ww * k, floor.ww)), clamp01(fmaxf(c.cw * k, floor.cw))};
}

// Write a frame to the five outputs, scaled by the brightness slider the same
// way out_rgb() / out_white() are. Floors therefore hold at any brightness
// above zero.
template<typename L, typename O>
inline void hw_out(L *light, O *ro, O *go, O *bo, O *wo, O *co, const HwColor &c) {
  float scale = fx_brightness_scale(light);
  ro->set_level(c.r * scale);
  go->set_level(c.g * scale);
  bo->set_level(c.b * scale);
  wo->set_level(c.ww * scale);
  co->set_level(c.cw * scale);
}

// Turn-taking within a group. The group shares one slot of `period` seconds,
// split into one window per light, so an occasional event (a gutter, a bubble)
// on one light never lands in the same second as its neighbour's - which is
// what kept three driveway candles under the flash limit together.
struct HwTurn {
  float w;          // seconds into the group's slot
  uint32_t slot;
  float at;         // when this light's event starts, within the slot
};
inline HwTurn hw_group_turn(double T, const HwSelf &b, float period, float reserve, uint32_t salt) {
  HwTurn r;
  double tg = T + (double) (sync_random(b.group, 991) * period);
  sync_slot(tg, period, r.slot, r.w);
  float sub = period / (float) b.n;
  r.at = (float) b.k * sub + sync_random(r.slot, salt + b.i) * (sub - reserve);
  return r;
}

// ---- palette and floors ----        r      g      b      ww     cw
constexpr HwColor HW_PUMPKIN      = {0.45f, 0.08f, 0.00f, 0.50f, 0.00f};
constexpr HwColor HW_EMBER        = {0.10f, 0.01f, 0.00f, 0.05f, 0.00f};  // floor: Jack-o'-Lantern Row
constexpr HwColor HW_VIOLET       = {0.50f, 0.00f, 1.00f, 0.00f, 0.08f};
constexpr HwColor HW_STORM_FLOOR  = {0.05f, 0.00f, 0.10f, 0.00f, 0.02f};  // floor: Lightning Over the House
constexpr HwColor HW_STRIKE       = {0.00f, 0.00f, 0.00f, 0.25f, 1.00f};
constexpr HwColor HW_POISON       = {0.10f, 0.90f, 0.00f, 0.00f, 0.00f};
constexpr HwColor HW_BREW_VIOLET  = {0.60f, 0.00f, 1.00f, 0.00f, 0.12f};
constexpr HwColor HW_LIME         = {0.40f, 1.00f, 0.00f, 0.00f, 0.15f};
constexpr HwColor HW_BREW_FLOOR   = {0.03f, 0.05f, 0.05f, 0.00f, 0.02f};  // floor: Witch's Cauldron
constexpr HwColor HW_MOON_WHITE   = {0.00f, 0.00f, 0.00f, 0.50f, 0.30f};
constexpr HwColor HW_COPPER       = {0.80f, 0.06f, 0.00f, 0.05f, 0.00f};
constexpr HwColor HW_MOON_FLOOR   = {0.03f, 0.00f, 0.00f, 0.05f, 0.00f};  // floor: Blood Moon

// Pumpkin at a flame level, redder as it dims.
inline HwColor hw_pumpkin(float lvl) {
  float warm = 0.6f + 0.4f * lvl;
  return {HW_PUMPKIN.r * lvl, HW_PUMPKIN.g * lvl * warm, 0.0f, HW_PUMPKIN.ww * lvl * warm, 0.0f};
}

// ---------------------------------------------------------------------------
// Jack-o'-Lantern Row - a row of carved pumpkins. A gentle two-layer flame,
// scattered per light, whose swing stays under the 10 % flash threshold; once
// every 7 s each light gutters, slowly, taking turns within its group. The
// door pair burns a little brighter. `steady` suppresses the gutters (for a
// scene that wants a clean start from it).
// ---------------------------------------------------------------------------
inline HwColor hw_jack(double T, const HwSelf &b, bool steady = false) {
  double tj = T + (double) (b.off * 7.0f);
  float slow = sync_noise(tj, 0.45f, b.i + 1);
  float fast = sync_noise(tj, 0.11f, b.i + 21);
  float lvl = 0.66f + 0.10f * (slow - 0.5f) + 0.03f * (fast - 0.5f);

  HwTurn gt = hw_group_turn(T, b, 7.0f, 1.6f, 50);
  float len = 0.45f + sync_random(gt.slot, b.i + 60) * 0.35f;
  if (!steady && gt.w >= gt.at && gt.w < gt.at + len) {
    float u = gt.w - gt.at;
    float e = fminf(clamp01(u / 0.25f), clamp01((len - u) / 0.30f));
    lvl = lvl + (0.36f - lvl) * hw_ease(e);
  }
  if (b.group == YARD_D)
    lvl *= 1.1f;
  return hw_finish(hw_pumpkin(lvl), HW_EMBER);
}

// ---------------------------------------------------------------------------
// Lightning Over the House - a violet night, and once per 11 s slot the whole
// house lights white at the same instant. One strike, never a flicker burst:
// a fast rise, a short hold and a slow fall from the cold-white channel.
// About one strike in three is a soft distant glow. The street bed peaks at
// 40 % so drivers never get a full flash at the kerb.
// ---------------------------------------------------------------------------
inline HwColor hw_lightning(double T, const HwSelf &b) {
  float t = sync_fold(T);
  uint32_t slot;
  float w;
  sync_slot(T, 11.0f, slot, w);

  HwColor base = hw_scale(HW_VIOLET, 0.14f * (0.85f + 0.15f * sinf(t * 0.3f)));
  float at = 0.8f + sync_random(slot, 1) * 8.0f;
  bool distant = sync_random(slot, 2) < 0.35f;
  float peak = distant ? 0.35f : 0.75f + 0.25f * sync_random(slot, 30);
  float rise = distant ? 0.25f : 0.06f;
  float hold = distant ? 0.10f : 0.05f;
  float fall = distant ? 2.5f : 4.0f;

  float L = 0.0f;
  if (w >= at) {
    float u = w - at;
    L = u < rise ? peak * (u / rise) : u < rise + hold ? peak : peak * expf(-(u - rise - hold) * fall);
  }
  if (b.group == YARD_S)
    L *= 0.4f;
  return hw_finish(hw_add(base, hw_scale(HW_STRIKE, L)), HW_STORM_FLOOR);
}

// ---------------------------------------------------------------------------
// Witch's Cauldron - every light bubbles between poison green and violet on
// its own slow phase; once every 4.2 s each light lifts with a lime bubble,
// taking turns within its group.
// ---------------------------------------------------------------------------
inline HwColor hw_cauldron(double T, const HwSelf &b) {
  float t = sync_fold(T + (double) (b.off * 20.0f));
  float m = 0.5f + 0.5f * sinf(t * 0.21f + sinf(t * 0.13f) * 2.0f);
  HwColor col = hw_scale(hw_mix(HW_POISON, HW_BREW_VIOLET, m), 0.55f);

  HwTurn gt = hw_group_turn(T, b, 4.2f, 1.0f, 3);
  float w = gt.w - gt.at;
  float env = w < 0.0f ? 0.0f : w < 0.4f ? hw_ease(w / 0.4f) : expf(-(w - 0.4f) * 4.0f);
  return hw_finish(hw_add(col, hw_scale(HW_LIME, env * 0.28f)), HW_BREW_FLOOR);
}

// ---------------------------------------------------------------------------
// Blood Moon - moon white; then an eclipse shadow sweeps across from the west
// (the street side) and turns everything copper red, holds, and clears. A
// four-minute cycle; the shadow reaches the door about 20 s after the street.
// ---------------------------------------------------------------------------
inline HwColor hw_moon(double T, const HwSelf &b) {
  double cd = fmod(T - (double) ((b.x_ft + 25.0f) / 3.0f), 240.0);
  if (cd < 0)
    cd += 240.0;
  float c = (float) cd, k;
  if (c < 50.0f) k = 0.0f;
  else if (c < 90.0f) k = hw_ease((c - 50.0f) / 40.0f);
  else if (c < 170.0f) k = 1.0f;
  else if (c < 210.0f) k = 1.0f - hw_ease((c - 170.0f) / 40.0f);
  else k = 0.0f;

  HwColor col = hw_mix(hw_scale(HW_MOON_WHITE, 0.75f), hw_scale(HW_COPPER, 0.5f), k);
  if (k == 1.0f)
    col = hw_scale(col, 0.85f + 0.15f * sinf(c * 0.5f));
  return hw_finish(col, HW_MOON_FLOOR);
}
