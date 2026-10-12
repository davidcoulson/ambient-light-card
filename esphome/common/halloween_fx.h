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
#include <ctime>
#include <strings.h>

// The shared helpers come from the LIGHT's package, not this one: ESPHome copies
// every package's includes into one flat src/ folder, and this package does not
// list light_effects_helpers.h, so the copy here is whatever ref the light's
// own ambient-light-card package is pinned at (athom-light.yaml, for example).
// Rule: pin both packages to the same ambient-light-card ref. This guard turns
// a mismatch in the helpers' API level into a compile error rather than lights
// that quietly differ from the tests and the preview.
#if defined(__has_include)
#if !__has_include("light_effects_helpers.h")
#error "halloween_effects.yaml needs light_effects_helpers.h: include accent_light_nonaddressable_effects.yaml or white_effects.yaml from ambient-light-card in the light's package"
#endif
#endif
#include "light_effects_helpers.h"
#ifdef LIGHT_EFFECTS_HELPERS_API
#define HW_HELPERS_API_FOUND LIGHT_EFFECTS_HELPERS_API
#else
#define HW_HELPERS_API_FOUND 1   // v1.2.0 - v1.2.2 had no marker; their helpers are level 1
#endif
#define HW_HELPERS_API_EXPECTED 1
#if HW_HELPERS_API_FOUND != HW_HELPERS_API_EXPECTED
#error "light_effects_helpers.h is at a different API level than halloween_fx.h expects: pin the light's ambient-light-card package and the Halloween package to the same ref"
#endif

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
  float gx_ft, gy_ft;   // the centre of its group
  float door_ft;     // distance from the front door
  float off;         // 0..1, spreads the scattered effects
  bool mapped;
};

// Find `name` (the device's yard_light substitution) in a map. A light that is
// not on the map still runs every effect: it plays a driveway light on its
// own, with its offset hashed from its MAC suffix.
inline HwSelf hw_lookup(const YardLight *map, int count, const char *name, const char *mac, float door_x = 0.0f,
                        float door_y = 0.0f) {
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
    s.door_ft = hypotf(s.x_ft - door_x, s.y_ft - door_y);
    for (int r = 0; r < count; r++) {
      if (map[r].group != s.group) continue;
      if (r < row) s.k++;
      s.n++;
      s.gx_ft += map[r].x_ft;
      s.gy_ft += map[r].y_ft;
    }
    s.gx_ft /= s.n;
    s.gy_ft /= s.n;
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

// Cap, hold every channel at or above the floor (rule 1) without breaking the
// cap, then clamp.
inline HwColor hw_finish_base(const HwColor &c, const HwColor &floor) {
  float s = c.r + c.g + c.b + c.ww + c.cw;
  float k = s > HW_CAP ? HW_CAP / s : 1.0f;
  HwColor o = {fmaxf(c.r * k, floor.r), fmaxf(c.g * k, floor.g), fmaxf(c.b * k, floor.b), fmaxf(c.ww * k, floor.ww),
               fmaxf(c.cw * k, floor.cw)};
  // Raising channels to the floor can push a capped frame back over the cap;
  // take the excess out of what sits above the floor, so both hold.
  float so = o.r + o.g + o.b + o.ww + o.cw, sf = floor.r + floor.g + floor.b + floor.ww + floor.cw;
  if (so > HW_CAP && so > sf) {
    float k2 = (HW_CAP - sf) / (so - sf);
    o = {floor.r + (o.r - floor.r) * k2, floor.g + (o.g - floor.g) * k2, floor.b + (o.b - floor.b) * k2,
         floor.ww + (o.ww - floor.ww) * k2, floor.cw + (o.cw - floor.cw) * k2};
  }
  return {clamp01(o.r), clamp01(o.g), clamp01(o.b), clamp01(o.ww), clamp01(o.cw)};
}

// ---------------------------------------------------------------------------
// The daylight lift. These lights come on an hour before sunset, when a dim
// floor or a slow mist is invisible. While it is light out, each frame gets a
// fixed extra amount of its OWN colour: the brightest channel gains up to
// HW_LIFT, the hue is kept, and the power cap still holds. Because the amount
// added does not follow the frame's ups and downs, flicker and pulses keep
// exactly their night-time swing, so the lift adds no flashes; floors only
// rise. g_hw_amb (0 dark .. 1 daylight) is set every 10 s by
// halloween_effects.yaml, from measured light when halloween_weather.yaml has
// a lux reading, else from the sun's elevation. After dark it is 0 and every
// effect runs exactly as designed.
// ---------------------------------------------------------------------------
constexpr float HW_LIFT = 0.25f;
inline float g_hw_amb = 0.0f;
inline HwColor hw_lift(const HwColor &c, float amb) {
  if (!(amb > 0.0f)) return c;
  float m = fmaxf(fmaxf(fmaxf(c.r, c.g), fmaxf(c.b, c.ww)), c.cw), s = c.r + c.g + c.b + c.ww + c.cw;
  if (m <= 0.0f) return c;
  // stay inside the power cap, and keep the brightest channel at or under full:
  // an output clamps anything above 1.0, which would clip a pulse and shift its hue
  float L = fminf(amb * HW_LIFT, fmaxf(0.0f, fminf((HW_CAP / s - 1.0f) * m, 1.0f - m)));
  return hw_scale(c, 1.0f + L / m);
}

// Every effect's last step: cap, floor, and the daylight lift.
inline HwColor hw_finish(const HwColor &c, const HwColor &floor) { return hw_lift(hw_finish_base(c, floor), g_hw_amb); }

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
inline HwColor hw_jack_base(double T, const HwSelf &b, bool steady) {
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
  return hw_finish_base(hw_pumpkin(lvl), HW_EMBER);
}
inline HwColor hw_jack(double T, const HwSelf &b, bool steady = false) { return hw_lift(hw_jack_base(T, b, steady), g_hw_amb); }

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

// ===========================================================================
// Round 2: Heartbeat of the House, Trick-or-Treat Swap, Turning of the Season,
// Witching Hour, Graveyard Mist and Wind-Blown Candles.
//
// Season, Witching Hour and Mist follow the calendar and the local clock, so
// they take the UNSCALED shared clock (sync_seconds()), like the library's
// Clock effect: a Speed-scaled clock would land on the wrong date. Anything
// that multiplies epoch time stays in double, mirrored in
// tests/halloween_reference.js.
// ===========================================================================

//                                      r      g      b      ww     cw
constexpr HwColor HW_BLOOD         = {1.00f, 0.00f, 0.03f, 0.00f, 0.00f};
constexpr HwColor HW_BLOOD_FLOOR   = {0.17f, 0.00f, 0.005f, 0.00f, 0.00f};  // floor: Heartbeat
constexpr HwColor HW_TREAT_ORANGE  = {0.60f, 0.10f, 0.00f, 0.10f, 0.00f};   // same luminance as purple,
constexpr HwColor HW_TREAT_PURPLE  = {0.60f, 0.00f, 1.00f, 0.00f, 0.08f};   // so a swap is a hue change
constexpr HwColor HW_TREAT_FLOOR   = {0.10f, 0.00f, 0.05f, 0.02f, 0.00f};   // floor: Trick-or-Treat Swap
constexpr HwColor HW_NIGHT_FLOOR   = {0.00f, 0.00f, 0.08f, 0.00f, 0.10f};   // floor: Witching Hour, Mist
constexpr HwColor HW_HARVEST_GOLD  = {0.40f, 0.20f, 0.00f, 0.55f, 0.00f};
constexpr HwColor HW_RUST          = {0.60f, 0.10f, 0.00f, 0.20f, 0.00f};
constexpr HwColor HW_HALLOW_ORANGE = {0.45f, 0.08f, 0.00f, 0.50f, 0.00f};
constexpr HwColor HW_HALLOW_VIOLET = {0.50f, 0.00f, 1.00f, 0.00f, 0.10f};
constexpr HwColor HW_DUSK_AMBER    = {0.35f, 0.10f, 0.00f, 0.50f, 0.05f};
constexpr HwColor HW_DEEP_VIOLET   = {0.45f, 0.00f, 0.90f, 0.00f, 0.10f};
constexpr HwColor HW_EERIE_TEAL    = {0.00f, 0.50f, 0.40f, 0.00f, 0.30f};
constexpr HwColor HW_LATE_RED      = {0.60f, 0.02f, 0.00f, 0.04f, 0.00f};
constexpr HwColor HW_MIST_SETS[3][2] = {
    {{0.00f, 0.15f, 0.35f, 0.00f, 0.15f}, {0.05f, 0.05f, 0.05f, 0.25f, 0.30f}},   // teal and bone
    {{0.02f, 0.30f, 0.08f, 0.00f, 0.12f}, {0.06f, 0.10f, 0.02f, 0.30f, 0.15f}},   // moss and lichen
    {{0.30f, 0.00f, 0.45f, 0.00f, 0.10f}, {0.06f, 0.04f, 0.06f, 0.20f, 0.30f}},   // dusk violet and ash
};

// 0 at the west end of the house front, 1 at the east.
inline float hw_across(const HwSelf &b) { return (b.x_ft + 23.0f) / 65.0f; }

// ---- calendar and sun ----

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil).
inline int32_t hw_days_from_civil(int32_t y, int32_t m, int32_t d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  int32_t yoe = y - era * 400;
  int32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// The local evening a moment belongs to: an evening keeps its date until noon
// the next day. night = its day number (seeds that night's dice), hour = local
// hours from that date's midnight (25.5 = 1:30 am), oct = the October date
// (1 before October, 31 after). Local time comes from the time component's
// timezone, via sync_localtime().
struct HwEvening {
  int32_t night;
  double hour;
  int oct;
};
inline HwEvening hw_evening(double T) {
  struct tm lt;
  sync_localtime(T, lt);
  double hour = lt.tm_hour + lt.tm_min / 60.0 + (lt.tm_sec + (T - floor(T))) / 3600.0;
  if (lt.tm_hour < 12) {
    hour += 24.0;
    sync_localtime(floor(T) - 86400.0, lt);
  }
  HwEvening e;
  e.night = hw_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  e.oct = lt.tm_mon == 9 ? lt.tm_mday : lt.tm_mon < 9 ? 1 : 31;
  e.hour = hour;
  return e;
}

// Solar elevation in degrees at epoch T, from low-precision almanac formulae
// (about a minute on sunset). Double throughout.
inline double hw_sun_elevation(double T, double lat, double lon) {
  const double R = M_PI / 180.0, n = (T - 946728000.0) / 86400.0;
  double L = fmod(280.460 + 0.9856474 * n, 360.0), g = fmod(357.528 + 0.9856003 * n, 360.0) * R;
  double lam = (L + 1.915 * sin(g) + 0.020 * sin(2.0 * g)) * R, eps = (23.439 - 0.0000004 * n) * R;
  double ra = atan2(cos(eps) * sin(lam), cos(lam)), dec = asin(sin(eps) * sin(lam));
  double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0), ha = ((gmst + lon / 15.0) * 15.0) * R - ra;
  return asin(sin(lat * R) * sin(dec) + cos(lat * R) * cos(dec) * cos(ha)) / R;
}

// Local clock hour of sunset (sun at -0.833 deg) on T's local date: a
// bisection from 15:00 over seven hours, worked out once per date.
inline double hw_sunset_hour(double T, double lat, double lon) {
  static int32_t memo_key = INT32_MIN;
  static double memo = 0.0;
  struct tm lt;
  sync_localtime(T, lt);
  int32_t key = hw_days_from_civil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  if (key != memo_key) {
    struct tm at = lt;
    at.tm_hour = 15;
    at.tm_min = 0;
    at.tm_sec = 0;
    at.tm_isdst = -1;
    double lo = (double) mktime(&at), hi = lo + 7.0 * 3600.0;
    for (int i = 0; i < 30; i++) {
      double mid = (lo + hi) / 2.0;
      if (hw_sun_elevation(mid, lat, lon) > -0.833) lo = mid;
      else hi = mid;
    }
    struct tm st;
    sync_localtime(floor(lo), st);
    memo = st.tm_hour + st.tm_min / 60.0 + st.tm_sec / 3600.0;
    memo_key = key;
  }
  return memo;
}

// Colour keyframes along one axis (positions ascending), eased between.
struct HwKey {
  double pos;
  HwColor c;
};
inline HwColor hw_interp(const HwKey *k, int n, double s) {
  if (s <= k[0].pos) return k[0].c;
  for (int i = 1; i < n; i++)
    if (s <= k[i].pos) return hw_mix(k[i - 1].c, k[i].c, hw_ease((float) ((s - k[i - 1].pos) / (k[i].pos - k[i - 1].pos))));
  return k[n - 1].c;
}

// ---- weather (halloween_weather.yaml) ----

// What the effects see of the weather, in the units Home Assistant reports
// for a WeatherFlow Tempest in US units: mph, degrees, in/h, degrees F. `on`
// is true while there is a valid wind reading from Home Assistant. Without the
// weather package these calm defaults hold, and every effect runs as if the
// night were still and dry.
struct HwWeather {
  bool on;
  float wind_mph, gust_mph, lull_mph, dir_deg, rain_inh, temp_f, dew_f;
  float lux;   // measured light, NaN = none (the daylight lift then follows the sun)
};
constexpr HwWeather HW_WX_CALM = {false, 0.0f, 0.0f, 0.0f, 270.0f, 0.0f, 50.0f, 35.0f, NAN};

// ---------------------------------------------------------------------------
// Weather moves on the shared clock, never on arrival.
//
// A reading reaches each bulb a moment apart (and a rebooted bulb gets HA's
// current state when it reconnects), so applying it on arrival would let
// neighbours run on different weather. Instead each reading is kept with its
// arrival time, and at every whole minute of the shared clock each bulb
// promotes the readings it has held for at least HW_WX_SETTLE seconds. Every
// bulb therefore switches to the same values at the same instant. Rain,
// temperature, dew point and light then ease in from the previous minute's
// values over HW_WX_EASE seconds, and effects that move with the wind
// crossfade from the previous minute's wind - both as functions of the shared
// clock, so neighbours compute the same frame.
//
// What this does not cover, and costs at most one minute: a reading whose
// arrival straddles the settle cutoff across bulbs, and a bulb that has just
// booted (it starts from HA's current state). The state is advanced every
// second by hw_wx_tick() whether or not a weather effect is running, so
// selecting an effect mid-minute changes nothing.
// ---------------------------------------------------------------------------
constexpr double HW_WX_PERIOD = 60.0, HW_WX_SETTLE = 5.0, HW_WX_EASE = 10.0;
enum HwWxField : uint8_t { HW_WX_WIND, HW_WX_GUST, HW_WX_LULL, HW_WX_DIR, HW_WX_RAIN, HW_WX_TEMP, HW_WX_DEW, HW_WX_LUX,
                           HW_WX_LUX2, HW_WX_N };
struct HwWxReading {
  float v = NAN;
  double at = -1e18;   // shared-clock arrival time
};
struct HwWxState {
  HwWxReading in[HW_WX_N];                 // the newest reading received from Home Assistant
  HwWxReading ready[HW_WX_N];              // a settled reading held for the next minute while a newer one waits
  float staged[HW_WX_N] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};   // promoted at the last minute
  int64_t minute = INT64_MIN;              // the shared-clock minute `cur` belongs to
  HwWeather prev = HW_WX_CALM, cur = HW_WX_CALM;
};
inline HwWxState g_hw_wxs;

inline void hw_wx_advance(double T);
// A reading from Home Assistant (halloween_weather.yaml's sensors). It first
// brings the timeline up to its own arrival time, so a reading already due for
// promotion is promoted whatever order this bulb's callbacks happen to run in;
// and if the reading it replaces has already settled for the next minute,
// that one is held back for it rather than lost.
inline void hw_wx_in(HwWxField f, float v, double T) {
  hw_wx_advance(T);
  HwWxState &st = g_hw_wxs;
  double cutoff = (double) (st.minute + 1) * HW_WX_PERIOD - HW_WX_SETTLE;
  if (st.in[f].at <= cutoff && T > cutoff) st.ready[f] = st.in[f];
  st.in[f] = {v, T};
}

inline HwWeather hw_wx_from(const float *s) {
  auto or_calm = [](float v, float calm) { return std::isnan(v) ? calm : v; };
  HwWeather w = HW_WX_CALM;
  w.on = !std::isnan(s[HW_WX_WIND]);
  w.wind_mph = or_calm(s[HW_WX_WIND], HW_WX_CALM.wind_mph);
  w.gust_mph = or_calm(s[HW_WX_GUST], HW_WX_CALM.gust_mph);
  w.lull_mph = or_calm(s[HW_WX_LULL], HW_WX_CALM.lull_mph);
  w.dir_deg = or_calm(s[HW_WX_DIR], HW_WX_CALM.dir_deg);
  w.rain_inh = or_calm(s[HW_WX_RAIN], HW_WX_CALM.rain_inh);
  w.temp_f = or_calm(s[HW_WX_TEMP], HW_WX_CALM.temp_f);
  w.dew_f = or_calm(s[HW_WX_DEW], HW_WX_CALM.dew_f);
  float a = s[HW_WX_LUX], b = s[HW_WX_LUX2];   // the higher wins, so a shaded sensor can't end the day early
  w.lux = std::isnan(a) ? b : std::isnan(b) ? a : fmaxf(a, b);
  return w;
}

// Promote readings at each new shared-clock minute. Only ever moves forward.
inline void hw_wx_advance(double T) {
  HwWxState &st = g_hw_wxs;
  int64_t m = (int64_t) floor(T / HW_WX_PERIOD);
  if (st.minute != INT64_MIN && m <= st.minute) return;
  bool fresh = st.minute == INT64_MIN || m != st.minute + 1;   // just booted, or a gap: no history to blend from
  double tb = (double) m * HW_WX_PERIOD;
  for (int f = 0; f < HW_WX_N; f++) {
    // the newest reading that had settled by this minute's cutoff
    if (fresh || st.in[f].at <= tb - HW_WX_SETTLE) st.staged[f] = st.in[f].v;
    else if (st.ready[f].at <= tb - HW_WX_SETTLE && st.ready[f].at > -1e17) st.staged[f] = st.ready[f].v;
    st.ready[f] = HwWxReading();
  }
  HwWeather now = hw_wx_from(st.staged);
  st.prev = fresh ? now : st.cur;
  st.cur = now;
  st.minute = m;
}

// The weather at T: this minute's values, the slow ones eased in from the
// previous minute's. Effects that read the weather take the UNSCALED clock.
inline HwWeather hw_wx_at(double T) {
  hw_wx_advance(T);
  const HwWxState &st = g_hw_wxs;
  HwWeather w = st.cur;
  float k = hw_ease(clamp01((float) ((T - (double) st.minute * HW_WX_PERIOD) / HW_WX_EASE)));
  w.rain_inh = st.prev.rain_inh + (st.cur.rain_inh - st.prev.rain_inh) * k;
  w.temp_f = st.prev.temp_f + (st.cur.temp_f - st.prev.temp_f) * k;
  w.dew_f = st.prev.dew_f + (st.cur.dew_f - st.prev.dew_f) * k;
  if (!std::isnan(st.prev.lux) && !std::isnan(st.cur.lux)) w.lux = st.prev.lux + (st.cur.lux - st.prev.lux) * k;
  return w;
}
// The previous minute's weather, for crossfades and for gusts that began before the minute turned.
inline const HwWeather &hw_wx_prev(double T) {
  hw_wx_advance(T);
  return g_hw_wxs.prev;
}
// Seconds since this minute's weather took effect.
inline double hw_wx_since(double T) {
  hw_wx_advance(T);
  return T - (double) g_hw_wxs.minute * HW_WX_PERIOD;
}

// Called once a second by halloween_weather.yaml with the unscaled clock and
// whether Home Assistant (a client subscribed to states, not just any API
// client) is connected. Losing HA drops every reading, timestamped at the
// loss, so the weather goes calm at the next minute and stays calm after a
// reconnect until HA sends fresh states (it does so on subscribe).
inline void hw_wx_tick(bool ha_connected, double T) {
  if (!ha_connected)
    for (int f = 0; f < HW_WX_N; f++)
      if (!std::isnan(g_hw_wxs.in[f].v)) hw_wx_in((HwWxField) f, NAN, T);
  hw_wx_advance(T);
}

// Fix the weather outright (tests, or a device with no live station): every
// reading counts as long settled.
inline void hw_wx_set(const HwWeather &w) {
  g_hw_wxs = HwWxState();
  float v[HW_WX_N] = {w.on ? w.wind_mph : NAN, w.gust_mph, w.lull_mph, w.dir_deg, w.rain_inh, w.temp_f, w.dew_f, w.lux, NAN};
  for (int f = 0; f < HW_WX_N; f++) g_hw_wxs.in[f] = {v[f], -1e18};
}

// Round half up, the same on every bulb and in the reference.
inline float hw_q(float v, float step) { return floorf(v / step + 0.5f) * step; }
inline float hw_wind_avg(float mph) { return fminf(30.0f, hw_q(mph, 1.0f)); }
struct HwVec {
  float x, y;
};
// The direction the wind blows TOWARD, on the map (x east, y south), from the
// direction it comes from, rounded to 22.5 degrees.
inline HwVec hw_wind_to(float dir_deg) {
  float a = (hw_q(dir_deg, 22.5f) + 180.0f) * (float) M_PI / 180.0f;
  return {sinf(a), -cosf(a)};
}

// The weather layer: a cooler cast on cold nights (fully at 35 F), a pale fog
// within 5 F of the dew point, a sheen and a slow wet shimmer in rain. Slow by
// construction; wind never changes brightness here.
inline HwColor hw_wx_layer(HwColor c, double T, const HwSelf &b) {
  const HwWeather w = hw_wx_at(T);
  float m = fmaxf(fmaxf(fmaxf(c.r, c.g), fmaxf(c.b, c.ww)), c.cw);
  float cold = clamp01((55.0f - hw_q(w.temp_f, 1.0f)) / 20.0f) * 0.35f;
  c = hw_mix(c, {c.r * 0.75f, c.g, c.b + 0.10f * m, c.ww * 0.6f, c.cw + 0.25f * c.ww + 0.05f * m}, cold);
  float fog = clamp01((5.0f - (hw_q(w.temp_f, 1.0f) - hw_q(w.dew_f, 1.0f))) / 5.0f) * 0.45f;
  float avg = (c.r + c.g + c.b) / 3.0f;
  c = hw_mix(c, {avg, avg, avg, c.ww + avg * 0.2f, c.cw + avg * 0.2f}, fog);
  float wet = clamp01(hw_q(w.rain_inh, 0.01f) / 0.3f);
  if (wet > 0.0f) {
    c = hw_add(c, {0.0f, 0.0f, 0.03f * wet, 0.0f, 0.06f * wet});
    c = hw_scale(c, 1.0f + 0.06f * wet * (sync_noise(T + (double) (b.off * 5.0f), 1.3f, b.i + 70) - 0.5f) * 2.0f);
  }
  return c;
}

// ---------------------------------------------------------------------------
// Heartbeat of the House - a slow red pulse that starts at the front door and
// reaches the street a moment later, like a pulse in an artery. 40 bpm; the
// second beat stays under the flash threshold. Floor: a dim red.
// ---------------------------------------------------------------------------
inline HwColor hw_heart(double T, const HwSelf &b) {
  uint32_t slot;
  float w;
  sync_slot(T - (double) (b.door_ft / 180.0f), 1.5f, slot, w);
  float lvl = 0.15f + 0.80f * expf(-6.0f * w) + (w > 0.35f ? 0.25f * expf(-6.0f * (w - 0.35f)) : 0.0f);
  float amp = 1.0f - 0.35f * fminf(1.0f, b.door_ft / 80.0f);
  return hw_finish(hw_scale(HW_BLOOD, clamp01(lvl * amp)), HW_BLOOD_FLOOR);
}

// ---------------------------------------------------------------------------
// Trick-or-Treat Swap - neighbours alternate orange and purple and trade
// colours every 8 s with a 2.5 s crossfade. The two colours carry the same
// luminance, so a swap is a change of hue, not a flash.
// ---------------------------------------------------------------------------
inline HwColor hw_swap(double T, const HwSelf &b) {
  uint32_t slot;
  float w;
  sync_slot(T, 8.0f, slot, w);
  bool parity = ((slot + b.i) & 1u) != 0;
  const HwColor &from = parity ? HW_TREAT_PURPLE : HW_TREAT_ORANGE, &to = parity ? HW_TREAT_ORANGE : HW_TREAT_PURPLE;
  float k = w > 5.5f ? hw_ease((w - 5.5f) / 2.5f) : 0.0f;
  return hw_finish(hw_scale(hw_mix(from, to, k), 0.8f), HW_TREAT_FLOOR);
}

// ---------------------------------------------------------------------------
// Turning of the Season - the month is the arc. October 1 is harvest gold and
// rust; by the 31st it has turned to pumpkin and violet. Each evening that
// date's two colours roll across the front of the house in a 5-7 minute wave,
// downwind when the weather is on, a nightly dice roll otherwise.
// Takes the unscaled clock.
// ---------------------------------------------------------------------------
inline float hw_season_wave(double T, double period, double dir, const HwSelf &b) {
  double ph = fmod(T / period + dir * (double) hw_across(b) * 0.6, 1.0);
  if (ph < 0) ph += 1.0;
  return 0.5f - 0.5f * cosf((float) ph * 6.2832f);
}
inline HwColor hw_season(double T, const HwSelf &b) {
  HwEvening e = hw_evening(T);
  float p = (e.oct - 1) / 30.0f;
  HwColor c1 = hw_mix(HW_HARVEST_GOLD, HW_HALLOW_ORANGE, p), c2 = hw_mix(HW_RUST, HW_HALLOW_VIOLET, p * p);
  uint32_t night = (uint32_t) e.night;
  double dice = sync_random(night, 80) < 0.5f ? 1.0 : -1.0;
  const HwWeather now = hw_wx_at(T), &before = hw_wx_prev(T);
  double dir = now.on ? (hw_wind_to(now.dir_deg).x >= 0.0f ? 1.0 : -1.0) : dice;
  double dir_before = before.on ? (hw_wind_to(before.dir_deg).x >= 0.0f ? 1.0 : -1.0) : dice;
  double period = 300.0 + 120.0 * (double) sync_random(night, 81);
  float w = hw_season_wave(T, period, dir, b);
  if (dir_before != dir) {   // the wind turned this minute: crossfade the wave's direction over HW_WX_EASE
    float k = hw_ease(clamp01((float) (hw_wx_since(T) / HW_WX_EASE)));
    w = hw_season_wave(T, period, dir_before, b) + (w - hw_season_wave(T, period, dir_before, b)) * k;
  }
  return hw_finish(hw_scale(hw_mix(c1, c2, hw_ease(w)), 0.85f), HW_EMBER);
}

// ---------------------------------------------------------------------------
// Witching Hour - the evening is the arc, from lights-on (an hour before
// sunset) to 11 pm: amber through daylight and sunset, deep violet an hour
// after, eerie teal in the middle of the dark, a low red by 11. On each
// quarter hour a 24 s swell crosses the house from west to east.
// Takes the unscaled clock and the place (sun_latitude / sun_longitude).
// ---------------------------------------------------------------------------
inline HwColor hw_witching(double T, const HwSelf &b, double lat, double lon) {
  HwEvening e = hw_evening(T);
  double sh = hw_sunset_hour(T, lat, lon), mid = (sh + 1.25 + 23.0) / 2.0;
  const HwKey keys[5] = {{sh - 1.0, HW_DUSK_AMBER}, {sh + 0.25, HW_DUSK_AMBER}, {sh + 1.25, HW_DEEP_VIOLET},
                         {mid, HW_EERIE_TEAL}, {23.0, HW_LATE_RED}};
  HwColor col = hw_interp(keys, 5, e.hour);
  float shimmer = 0.88f + 0.12f * sync_noise2(b.x_ft / 30.0 + 500.0 + T * 0.01, b.y_ft / 30.0 + 500.0, 33);
  double qs = fmod(e.hour * 4.0, 1.0) * 900.0 - (double) hw_across(b) * 12.0;
  float chime = qs >= 0.0 && qs < 24.0 ? sinf((float) (M_PI * qs / 24.0)) : 0.0f;
  return hw_finish(hw_scale(col, shimmer * (0.75f + 0.25f * chime)), HW_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Graveyard Mist - one slow 2D mist field drifting across the yard, teal and
// bone, moss and lichen, or violet and ash. Each night picks its palette,
// direction and pace. With the weather on it drifts with the real wind: one
// field per weather minute, moving with that minute's wind, crossfaded from
// the previous minute's over the first 15 s - no state of its own, so a bulb
// that starts the effect mid-minute shows the same mist as its neighbours.
// Takes the unscaled clock.
// ---------------------------------------------------------------------------
inline float hw_mist_field(const HwSelf &b, uint32_t slot, double w, float wind_mph, float dir_deg) {
  double v = (0.5 + (double) hw_wind_avg(wind_mph) * 0.08) / 22.0;
  HwVec wt = hw_wind_to(dir_deg);
  return sync_noise2(b.x_ft / 22.0 + 1e4 + (double) sync_random(slot, 73) * 5000.0 - wt.x * v * w,
                     b.y_ft / 22.0 + 1e4 + (double) sync_random(slot, 74) * 5000.0 - wt.y * v * w, 31);
}
inline float hw_mist_still(double T, const HwSelf &b, uint32_t night) {
  double a = (double) sync_random(night, 71) * 6.2832, sp = 0.012 + 0.012 * (double) sync_random(night, 72);
  return sync_noise2(b.x_ft / 22.0 + 1e7 + fmod(T * sp * cos(a), 1e6), b.y_ft / 22.0 + 1e7 + fmod(T * sp * sin(a), 1e6), 31);
}
inline HwColor hw_mist(double T, const HwSelf &b) {
  HwEvening e = hw_evening(T);
  uint32_t night = (uint32_t) e.night;
  const HwColor *set = HW_MIST_SETS[(int) (sync_random(night, 70) * 3.0f)];
  const HwWeather now = hw_wx_at(T), &before = hw_wx_prev(T);
  uint32_t slot;
  float w;
  sync_slot(T, (float) HW_WX_PERIOD, slot, w);   // one field per weather minute
  float d = now.on ? hw_mist_field(b, slot, w, now.wind_mph, now.dir_deg) : hw_mist_still(T, b, night);
  if (w < 15.0f) {
    float d0 = before.on ? hw_mist_field(b, slot - 1, (double) w + HW_WX_PERIOD, before.wind_mph, before.dir_deg)
                         : hw_mist_still(T, b, night);
    d = d0 + (d - d0) * hw_ease(w / 15.0f);
  }
  return hw_finish(hw_scale(hw_mix(set[0], set[1], hw_ease(d)), 0.9f), HW_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Wind-Blown Candles - Jack-o'-Lantern Row's flame without its dice gutters:
// gusts from the real wind roll through the yard from the windward side at
// 30 ft/s and bend each group of flames in turn, at most one gust per 5 s
// slot. Still
// air, or no weather package: steady flames. Runs through the weather layer.
// Follows real gusts, so it takes the unscaled clock (5 s slots line up with
// the weather minutes).
// ---------------------------------------------------------------------------
// How far a gust bends this light's flame at T (0..0.28): from the current
// 5 s slot (back = 0) or the one before (back = 1). Uses the weather that held
// when that slot began - so a gust that started before the minute turned
// finishes as it began - with the slot's start worked out in double from its
// index, so a slot starting exactly on the minute is never misjudged.
inline float hw_gust_bend(double T, const HwSelf &b, int back) {
  double s0 = floor(T / 5.0) - back, slot_start = s0 * 5.0;
  uint32_t slot = sync_u32(s0);
  float w = (float) (T - slot_start);
  const HwWeather wx = slot_start < (double) g_hw_wxs.minute * HW_WX_PERIOD ? hw_wx_prev(T) : hw_wx_at(T);
  float wind = hw_wind_avg(wx.wind_mph);
  float gusty = clamp01((hw_q(wx.gust_mph, 1.0f) - hw_q(wx.lull_mph, 1.0f)) / 15.0f + wind / 30.0f);
  if (!(sync_random(slot, 131) < gusty)) return 0.0f;
  HwVec wt = hw_wind_to(wx.dir_deg);
  float at = 0.5f + sync_random(slot, 130) * 2.0f;
  // the front reaches a whole group at once (its centre), so neighbours bend
  // together rather than a few tenths apart - which would add up to flashes
  float u = w - at - (b.gx_ft * wt.x + b.gy_ft * wt.y + 80.0f) / 30.0f;
  float bend = 0.28f * clamp01(wind / 15.0f + 0.3f);
  if (u >= 0.0f && u < 0.4f) return bend * hw_ease(u / 0.4f);
  if (u >= 0.4f && u < 1.3f) return bend * (1.0f - hw_ease((u - 0.4f) / 0.9f));
  return 0.0f;
}
inline HwColor hw_windcandles(double T, const HwSelf &b) {
  hw_wx_advance(T);
  HwColor base = hw_jack_base(T, b, true);   // lifted once, at the end
  // A gust's front takes up to ~5 s to cross the yard, so the previous slot's
  // gust may still be reaching downwind lights.
  float bend = fmaxf(hw_gust_bend(T, b, 0), hw_gust_bend(T, b, 1));
  return hw_finish(hw_wx_layer(hw_scale(base, 1.0f - bend), T, b), HW_EMBER);
}

// ---- ambient light for the daylight lift ----
// 0 when the sun is 4 deg or more below the horizon, 1 when it is 6 deg up.
inline float hw_ambient_from_sun(double elevation) {
  float a = clamp01((float) ((elevation + 4.0) / 10.0));
  return a * a * (3.0f - 2.0f * a);
}
// 0 under 5 lx, 1 above 400 lx, on a log scale. Measured light ends the day
// early on an overcast evening, which the sun's angle can't know.
inline float hw_ambient_from_lux(float lux) {
  float a = clamp01((log10f(fmaxf(lux, 0.01f)) - 0.7f) / 1.9f);
  return a * a * (3.0f - 2.0f * a);
}
// Called every second by halloween_effects.yaml with the unscaled clock. The
// sun is taken at the start of each 10 s bucket, so bulbs that tick at
// different moments within it agree; measured light comes eased through the
// weather timeline.
inline void hw_ambient_tick(double T, double lat, double lon) {
  float lux = hw_wx_at(T).lux;
  g_hw_amb = std::isnan(lux) ? hw_ambient_from_sun(hw_sun_elevation(floor(T / 10.0) * 10.0, lat, lon))
                             : hw_ambient_from_lux(lux);
}

