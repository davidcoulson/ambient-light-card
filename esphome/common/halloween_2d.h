#pragma once

// ---------------------------------------------------------------------------
// Halloween effects for a mapped room of RGB lights (halloween_2d_effects.yaml).
//
// The outdoor effects (halloween_fx.h) play along a row of RGBCW bulbs; these
// play across a room, using each light's place on the room map the way the
// library's 2D effects do. Same contract as everything else here: each effect
// is a pure function of the shared clock and of where the light is, so the
// whole room renders one scene with nothing sent between lights.
//
// The two Halloween rules hold here too, and tests/halloween_test.cpp checks
// them on every light of the example basement map:
//
//   1. A light never goes dark: each effect has a floor colour, and no frame
//      is under 3 % of full warm white's luminance.
//   2. Minimal flashing: at most 1 flash per second on any light, and at most
//      1 per second in the room's average light, so a strike or a heartbeat
//      never flashes the whole room faster than that. Things that MOVE (a
//      ghost, a bat, a gust) brighten one patch while another dims; they are
//      held to the per-light rule, not counted as the room flashing.
//
// Colours are RGB only (ww = cw = 0): these are for RGB accent rings. They go
// out through out_rgb(), so the brightness slider dims them like any other
// effect. Unlike the outdoor effects there is no power cap and no daylight
// lift: a room's lights are indoors and on their own supplies.
//
// Room Speed and Intensity apply as they do outdoors (hz_render() takes the
// clocks from hw_t_now()): Speed scales the animation clock, Intensity scales
// the movement and never the floor.
// ---------------------------------------------------------------------------

#include "halloween_fx.h"

// A light as these effects see it: its place in true room units (x 0..1,
// y 0..h, as light_xy() gives it), the room's depth h, and a per-light offset.
struct HzSelf {
  float x, y, h;
  uint32_t i;     // map row; salts this light's own draws
  float off;      // 0..1, spreads the per-light flicker
  bool mapped;
};

// `row` is the light's index on the room map (room_self()), -1 if it is not
// on it; an unmapped light still runs, at the scattered place light_xy() gave
// it, salted by its MAC.
inline HzSelf hz_at(float x, float y, float h, int row, const char *mac) {
  HzSelf s{x, y, h, 0, 0.0f, row >= 0};
  s.i = row >= 0 ? (uint32_t) row : 1000u + mac_hash32(mac) % 100000u;
  s.off = sync_random(s.i + 11, 977);
  return s;
}

// Floor, then clamp. No cap: see the header.
inline HwColor hz_finish(const HwColor &c, const HwColor &floor) {
  return {clamp01(fmaxf(c.r, floor.r)), clamp01(fmaxf(c.g, floor.g)), clamp01(fmaxf(c.b, floor.b)), 0.0f, 0.0f};
}

// Every 2D Halloween lambda draws its frame through this, on the shared clock.
template<typename F> inline HwColor hz_render(F fx) { return fx(hw_t_now(sync_seconds())); }

inline float hz_dist(const HzSelf &s, float px, float py) { return hypotf(s.x - px, s.y - py); }
// Distance from the room's centre.
inline float hz_centre(const HzSelf &s) { return hz_dist(s, 0.5f, 0.5f * s.h); }
// Position along a heading through the room's centre, about -0.57 .. 0.57.
inline float hz_along(const HzSelf &s, float th) { return (s.x - 0.5f) * cosf(th) + (s.y - 0.5f * s.h) * sinf(th); }

//                                     r      g      b
constexpr HwColor HZ_VIOLET       = {0.50f, 0.00f, 1.00f, 0, 0};
constexpr HwColor HZ_STORM_FLOOR  = {0.10f, 0.01f, 0.22f, 0, 0};
constexpr HwColor HZ_STRIKE       = {0.85f, 0.85f, 1.00f, 0, 0};
constexpr HwColor HZ_EMBER        = {0.20f, 0.02f, 0.00f, 0, 0};   // floor: Pumpkin Patch
constexpr HwColor HZ_POISON       = {0.15f, 1.00f, 0.05f, 0, 0};
constexpr HwColor HZ_BREW_VIOLET  = {0.60f, 0.00f, 1.00f, 0, 0};
constexpr HwColor HZ_LIME         = {0.50f, 1.00f, 0.00f, 0, 0};
constexpr HwColor HZ_BREW_FLOOR   = {0.05f, 0.12f, 0.08f, 0, 0};
constexpr HwColor HZ_GHOST        = {0.55f, 0.75f, 0.85f, 0, 0};
constexpr HwColor HZ_NIGHT_FLOOR  = {0.10f, 0.02f, 0.25f, 0, 0};   // floor: Ghosts, Mist, Vortex
constexpr HwColor HZ_MOON_WHITE   = {0.75f, 0.75f, 0.70f, 0, 0};
constexpr HwColor HZ_COPPER       = {0.90f, 0.12f, 0.00f, 0, 0};
constexpr HwColor HZ_MOON_FLOOR   = {0.15f, 0.02f, 0.01f, 0, 0};
constexpr HwColor HZ_BLOOD        = {1.00f, 0.00f, 0.03f, 0, 0};
constexpr HwColor HZ_BLOOD_FLOOR  = {0.17f, 0.00f, 0.005f, 0, 0};
constexpr HwColor HZ_TREAT_ORANGE = {0.65f, 0.16f, 0.00f, 0, 0};   // about the luminance of the purple,
constexpr HwColor HZ_TREAT_PURPLE = {0.60f, 0.00f, 1.00f, 0, 0};   // so a swap is a hue change
constexpr HwColor HZ_TREAT_FLOOR  = {0.10f, 0.02f, 0.10f, 0, 0};
constexpr HwColor HZ_DUSK_AMBER   = {0.90f, 0.28f, 0.00f, 0, 0};
constexpr HwColor HZ_DUSK_VIOLET  = {0.30f, 0.02f, 0.55f, 0, 0};
constexpr HwColor HZ_DUSK_FLOOR   = {0.12f, 0.02f, 0.15f, 0, 0};
constexpr HwColor HZ_VORTEX_GREEN = {0.10f, 0.60f, 0.08f, 0, 0};
constexpr HwColor HZ_CANDLE       = {1.00f, 0.38f, 0.04f, 0, 0};
constexpr HwColor HZ_CANDLE_FLOOR = {0.22f, 0.07f, 0.01f, 0, 0};   // floor: Candlelight, Candle Chase
constexpr HwColor HZ_MIST_SETS[3][2] = {
    {{0.00f, 0.35f, 0.45f, 0, 0}, {0.45f, 0.42f, 0.35f, 0, 0}},   // teal and bone
    {{0.05f, 0.45f, 0.10f, 0, 0}, {0.35f, 0.35f, 0.10f, 0, 0}},   // moss and lichen
    {{0.35f, 0.00f, 0.60f, 0, 0}, {0.30f, 0.28f, 0.32f, 0, 0}},   // dusk violet and ash
};

// ---------------------------------------------------------------------------
// Lightning Storm - violet cloud drifting over the room, and once per 11 s
// slot a strike lands somewhere in it: brightest where it lands, rolling out
// across the room in under a tenth of a second, then a slow fall. One strike
// in three is a soft distant glow over everything. Intensity scales strikes.
// ---------------------------------------------------------------------------
inline HwColor hz_lightning(const HwT &t, const HzSelf &s) {
  float cloud = sync_noise2(s.x * 2.5 + t.A * 0.04 + 50.0, s.y * 2.5 + 50.0, 81);
  HwColor base = hw_scale(HZ_VIOLET, 0.25f + 0.12f * cloud);

  uint32_t slot;
  float w;
  sync_slot(t.A, 11.0f, slot, w);
  float at = 0.8f + sync_random(slot, 1) * 8.0f;
  bool distant = sync_random(slot, 2) < 0.35f;
  float dd = hz_dist(s, sync_random(slot, 3), sync_random(slot, 4) * s.h);
  float peak = distant ? 0.30f : 0.70f + 0.30f * sync_random(slot, 30);
  float near = distant ? 1.0f : 0.40f + 0.60f * expf(-dd * dd / 0.06f);
  float rise = distant ? 0.25f : 0.06f, hold = distant ? 0.10f : 0.05f, fall = distant ? 2.5f : 4.0f;

  // Fast enough that every light has risen before the first one falls: the
  // room sees one flash, not a ripple of them.
  float u = w - at - dd / 12.0f, L = 0.0f;
  if (u >= 0.0f)
    L = u < rise ? peak * (u / rise) : u < rise + hold ? peak : peak * expf(-(u - rise - hold) * fall);
  return hz_finish(hw_add(base, hw_scale(HZ_STRIKE, L * near * t.d)), HZ_STORM_FLOOR);
}

// ---------------------------------------------------------------------------
// Pumpkin Patch - a carved pumpkin at every light, each flame on its own
// noise, and every 12 s a draught crosses the room on a heading of its own:
// each flame ducks as it passes. The duck stays under the flash threshold at
// normal Intensity. Intensity scales the flicker and the duck.
// ---------------------------------------------------------------------------
inline HwColor hz_pumpkin(float lvl) {
  float warm = 0.6f + 0.4f * lvl;
  return {lvl, 0.28f * lvl * warm, 0.0f, 0, 0};
}
inline HwColor hz_pumpkins(const HwT &t, const HzSelf &s) {
  double tj = t.A + (double) (s.off * 7.0f);
  float slow = sync_noise(tj, 0.45f, s.i + 1), fast = sync_noise(tj, 0.11f, s.i + 21);
  float lvl = 0.66f + t.d * (0.10f * (slow - 0.5f) + 0.03f * (fast - 0.5f));

  uint32_t slot;
  float w;
  sync_slot(t.A, 12.0f, slot, w);
  float g = -0.65f + (w - 2.0f) * 0.25f - hz_along(s, sync_random(slot, 5) * 6.2831853f);
  lvl += (0.46f - lvl) * expf(-g * g / 0.0128f) * t.d;   // a front 0.08 wide, at 0.25 room/s
  return hz_finish(hz_pumpkin(lvl), HZ_EMBER);
}

// ---------------------------------------------------------------------------
// Witch's Cauldron - a pot at the room's centre. The brew swirls poison green
// and violet, brightest at the pot, and every 4.2 s a bubble bursts and its
// lime surge spreads out across the room, fading as it goes. Intensity scales
// the surges.
// ---------------------------------------------------------------------------
inline HwColor hz_cauldron(const HwT &t, const HzSelf &s) {
  float dd = hz_centre(s);
  float m = sync_noise2(s.x * 3.0 + t.A * 0.05 + 20.0, s.y * 3.0 + t.A * 0.03 + 20.0, 33);
  HwColor col = hw_scale(hw_mix(HZ_POISON, HZ_BREW_VIOLET, hw_ease(m)), 0.45f + 0.25f * expf(-dd * dd / 0.04f));

  uint32_t slot;
  float w;
  sync_slot(t.A, 4.2f, slot, w);
  float u = w - dd / 0.5f;
  float env = u < 0.0f ? 0.0f : u < 0.6f ? hw_ease(u / 0.6f) : expf(-(u - 0.6f) * 2.5f);
  float amp = 0.30f * (1.0f - 0.6f * fminf(1.0f, dd / 0.55f));
  return hz_finish(hw_add(col, hw_scale(HZ_LIME, env * amp * t.d)), HZ_BREW_FLOOR);
}

// ---------------------------------------------------------------------------
// Ghosts - a dim violet room with three pale ghosts drifting about it,
// pausing, thinning and thickening as they go. Intensity scales the ghosts.
// ---------------------------------------------------------------------------
inline HwColor hz_ghosts(const HwT &t, const HzSelf &s) {
  HwColor c = hw_scale(HZ_VIOLET, 0.22f + 0.05f * sync_noise2(s.x * 2.0 + t.A * 0.02 + 5.0, s.y * 2.0 + 5.0, 41));
  float g = 0.0f;
  for (int k = 0; k < 3; k++) {
    float gx = 0.05f + 0.90f * sync_noise(t.A, 7.0f + 2.0f * k, 300 + k * 7);
    float gy = (0.05f + 0.90f * sync_noise(t.A, 8.5f + 1.5f * k, 400 + k * 7)) * s.h;
    float dd = hz_dist(s, gx, gy);
    float body = 0.6f + 0.4f * sync_noise(t.A, 3.0f, 500 + k);
    g = fmaxf(g, body * expf(-dd * dd / 0.0242f));   // sigma 0.11
  }
  return hz_finish(hw_add(c, hw_scale(HZ_GHOST, 0.6f * g * t.d)), HZ_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Blood Moon - moonlight, then the eclipse shadow arrives from the west wall
// on a curved edge, takes 30 s to cross the room and turns it copper red; it
// holds, pulsing slowly, and clears the same way. A four-minute cycle.
// Intensity scales the pulse.
// ---------------------------------------------------------------------------
inline HwColor hz_moon(const HwT &t, const HzSelf &s) {
  float edge = hz_dist(s, -0.6f, 0.5f * s.h) - 0.6f;   // 0 at the west wall, about 1 at the east
  double cd = fmod(t.A - (double) (edge * 30.0f), 240.0);
  if (cd < 0)
    cd += 240.0;
  float c = (float) cd, k;
  if (c < 50.0f) k = 0.0f;
  else if (c < 90.0f) k = hw_ease((c - 50.0f) / 40.0f);
  else if (c < 170.0f) k = 1.0f;
  else if (c < 210.0f) k = 1.0f - hw_ease((c - 170.0f) / 40.0f);
  else k = 0.0f;
  HwColor col = hw_mix(hw_scale(HZ_MOON_WHITE, 0.75f), hw_scale(HZ_COPPER, 0.6f), k);
  return hz_finish(hw_scale(col, 1.0f - 0.15f * k * (0.5f - 0.5f * sinf(c * 0.5f)) * t.d), HZ_MOON_FLOOR);
}

// ---------------------------------------------------------------------------
// Heartbeat - a lub-dub every 1.5 s from the room's centre, the beat
// rippling out to the walls in about half a second, weaker as it goes.
// Intensity scales the beat.
// ---------------------------------------------------------------------------
inline HwColor hz_heart(const HwT &t, const HzSelf &s) {
  float dd = hz_centre(s);
  uint32_t slot;
  float w;
  sync_slot(t.A - (double) (dd / 1.2f), 1.5f, slot, w);
  float lvl = 0.15f + t.d * (0.80f * expf(-6.0f * w) + (w > 0.35f ? 0.25f * expf(-6.0f * (w - 0.35f)) : 0.0f));
  float amp = 1.0f - 0.45f * fminf(1.0f, dd / 0.56f);
  return hz_finish(hw_scale(HZ_BLOOD, clamp01(lvl * amp)), HZ_BLOOD_FLOOR);
}

// ---------------------------------------------------------------------------
// Candlelight - every light is a candle on its own flicker: a slow sway and a
// quicker tremble, scattered per light so no two flames agree. Meant for a few
// lights (wall sconces). Intensity scales the flicker.
// ---------------------------------------------------------------------------
inline float hz_candle_lvl(const HwT &t, const HzSelf &s, float k) {
  double tj = t.A + (double) (s.off * 9.0f);
  float slow = sync_noise(tj, 0.55f, s.i + 61), fast = sync_noise(tj, 1.6f, s.i + 71);
  return 0.62f + t.d * k * (0.20f * (slow - 0.5f) + 0.07f * (fast - 0.5f));
}
inline HwColor hz_candles(const HwT &t, const HzSelf &s) {
  return hz_finish(hw_scale(HZ_CANDLE, clamp01(hz_candle_lvl(t, s, 1.0f))), HZ_CANDLE_FLOOR);
}

// ---------------------------------------------------------------------------
// Candle Chase - the candles again, quieter, and every 8 s a slow swell of
// light travels the length of the row, left to right, then right to left on
// the next pass: each flame brightens as it arrives and settles as it leaves.
// Intensity scales the flicker and the swell.
// ---------------------------------------------------------------------------
inline HwColor hz_chase(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 8.0f, slot, w);
  float p = -0.25f + 1.5f * (w / 8.0f);              // swell position, off the left edge to off the right
  if (slot & 1u) p = 1.0f - p;
  float dx = s.x - p;
  float lvl = hz_candle_lvl(t, s, 0.6f) + 0.30f * t.d * expf(-dx * dx / 0.03f);
  return hz_finish(hw_scale(HZ_CANDLE, clamp01(lvl)), HZ_CANDLE_FLOOR);
}

// ---------------------------------------------------------------------------
// Graveyard Mist - two banks of fog drifting across the room at their own
// speeds. The colours are the night's (teal and bone, moss and lichen, or
// dusk violet and ash), the same dice as the outdoor Graveyard Mist uses.
// Intensity scales the fog's contrast.
// ---------------------------------------------------------------------------
inline HwColor hz_mist(const HwT &t, const HzSelf &s) {
  const HwColor *set = HZ_MIST_SETS[(int) (sync_random((uint32_t) hw_evening(t.T).night, 70) * 3.0f)];
  float a = sync_noise2(s.x * 2.0 + t.A * 0.020 + 10.0, s.y * 2.0 + t.A * 0.006 + 10.0, 71);
  float b = sync_noise2(s.x * 4.5 + t.A * 0.045 + 30.0, s.y * 4.5 + 30.0, 72);
  float d = 0.5f + (0.65f * a + 0.35f * b - 0.5f) * 1.6f * t.d;
  return hz_finish(hw_scale(hw_mix(set[0], set[1], hw_ease(clamp01(d))), 0.9f), HZ_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Trick-or-Treat - the room is all orange or all purple, and every 8 s the
// other colour sweeps in across it from a new direction. The two are about
// equally bright, so a sweep is a change of hue, not a flash. Intensity
// scales how far each light swings.
// ---------------------------------------------------------------------------
inline HwColor hz_swap(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 8.0f, slot, w);
  float at = 2.0f + (hz_along(s, sync_random(slot, 6) * 6.2831853f) + 0.6f) * 3.0f;
  float k = hw_ease(clamp01((w - at) / 1.2f));
  bool odd = (slot & 1u) != 0;
  const HwColor &from = odd ? HZ_TREAT_PURPLE : HZ_TREAT_ORANGE, &to = odd ? HZ_TREAT_ORANGE : HZ_TREAT_PURPLE;
  return hz_finish(hw_scale(hw_mix(from, to, 0.5f + (k - 0.5f) * t.d), 0.85f), HZ_TREAT_FLOOR);
}

// ---------------------------------------------------------------------------
// Bats at Dusk - an amber sky low in the south fading to violet in the north,
// and bats crossing it: two per 5 s, each on its own heading, wobbling as it
// flies and darkening the lights it passes over. Intensity scales the shadow.
// ---------------------------------------------------------------------------
inline HwColor hz_bats(const HwT &t, const HzSelf &s) {
  HwColor sky = hw_mix(HZ_DUSK_VIOLET, HZ_DUSK_AMBER, hw_ease(clamp01(s.y / s.h)));
  sky = hw_scale(sky, 0.85f + 0.10f * sync_noise2(s.x * 2.0 + t.A * 0.03 + 3.0, s.y * 2.0 + 3.0, 91));
  float shade = 0.0f;
  for (int k = 0; k < 2; k++) {
    uint32_t slot;
    float w;
    sync_slot(t.A + 2.5 * k, 5.0f, slot, w);
    float th = sync_random(slot, 100 + 10 * k) * 6.2831853f, ux = cosf(th), uy = sinf(th);
    float side = (sync_random(slot, 101 + 10 * k) - 0.5f) * 0.5f + 0.05f * sinf(w * 9.0f + (float) k);
    float along = -0.75f + w * 0.35f;
    float dd = hz_dist(s, 0.5f + ux * along - uy * side, 0.5f * s.h + uy * along + ux * side);
    shade = fmaxf(shade, expf(-dd * dd / 0.0098f));   // sigma 0.07
  }
  return hz_finish(hw_scale(sky, 1.0f - 0.5f * shade * t.d), HZ_DUSK_FLOOR);
}

// ---------------------------------------------------------------------------
// Witching Vortex - two spiral arms of violet and green turning slowly about
// the room's centre, its eye glowing brighter. Intensity scales the arms'
// contrast.
// ---------------------------------------------------------------------------
inline HwColor hz_vortex(const HwT &t, const HzSelf &s) {
  float dx = s.x - 0.5f, dy = s.y - 0.5f * s.h, r = hypotf(dx, dy);
  float rot = (float) (fmod(t.A, 12.566370614359172) * 0.5);   // a full turn of colour every 12.6 s
  float m = 0.5f + 0.5f * t.d * sinf(2.0f * atan2f(dy, dx) + 9.0f * r - rot);
  HwColor col = hw_scale(hw_mix(HZ_VIOLET, HZ_VORTEX_GREEN, clamp01(m)), 0.45f + 0.35f * expf(-r * r / 0.03f));
  return hz_finish(col, HZ_NIGHT_FLOOR);
}
