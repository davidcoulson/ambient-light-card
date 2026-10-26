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
//   2. Under the flash threshold: at most 2 flashes per second (WCAG 2.3.1 allows 3)
//      on any light and in the room's average light, so a strike or a
//      heartbeat never flashes the whole room faster than that. Things that
//      MOVE (a ghost, a bat, a gust) brighten one patch while another dims;
//      they are held to the per-light rule, not counted as the room flashing.
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
constexpr HwColor HZ_BEAM         = {0.80f, 0.88f, 1.00f, 0, 0};
constexpr HwColor HZ_BEAM_FLOOR   = {0.06f, 0.07f, 0.20f, 0, 0};   // floor: Searchlight
constexpr HwColor HZ_HAUNT_RING   = {0.45f, 0.95f, 0.85f, 0, 0};
constexpr HwColor HZ_FOG_DARK     = {0.10f, 0.30f, 0.12f, 0, 0};
constexpr HwColor HZ_FOG_LIGHT    = {0.60f, 0.85f, 0.60f, 0, 0};
constexpr HwColor HZ_FOG_FLOOR    = {0.06f, 0.09f, 0.12f, 0, 0};   // floor: Rolling Fog
constexpr HwColor HZ_AFTERGLOW    = {0.70f, 0.40f, 1.00f, 0, 0};
constexpr HwColor HZ_POSSESSED[3] = {{0.08f, 0.30f, 0.06f, 0, 0}, {0.60f, 0.00f, 1.00f, 0, 0}, {1.00f, 0.00f, 0.05f, 0, 0}};   // near-equal luminance
constexpr HwColor HZ_SHUDDER      = {0.60f, 0.60f, 0.70f, 0, 0};
constexpr HwColor HZ_POSSESS_FLOOR= {0.08f, 0.04f, 0.10f, 0, 0};   // floor: Possessed Room
constexpr HwColor HZ_VINE         = {0.12f, 0.55f, 0.08f, 0, 0};
constexpr HwColor HZ_VINE_TIP     = {0.60f, 1.00f, 0.30f, 0, 0};
constexpr HwColor HZ_MOSS_FLOOR   = {0.04f, 0.10f, 0.05f, 0, 0};   // floor: Creeping Vines
constexpr HwColor HZ_WISP         = {0.30f, 0.90f, 0.80f, 0, 0};
constexpr HwColor HZ_BOG_FLOOR    = {0.04f, 0.05f, 0.16f, 0, 0};   // floor: Will-o'-the-Wisps
constexpr HwColor HZ_MIST_SETS[3][2] = {
    {{0.00f, 0.35f, 0.45f, 0, 0}, {0.45f, 0.42f, 0.35f, 0, 0}},   // teal and bone
    {{0.05f, 0.45f, 0.10f, 0, 0}, {0.35f, 0.35f, 0.10f, 0, 0}},   // moss and lichen
    {{0.35f, 0.00f, 0.60f, 0, 0}, {0.30f, 0.28f, 0.32f, 0, 0}},   // dusk violet and ash
};

// ---------------------------------------------------------------------------
// Lightning Storm - violet cloud drifting over the room, and once per 7 s
// slot a strike lands somewhere in it: brightest where it lands, rolling out
// across the room in under a tenth of a second, often a second stroke on its
// heels, then a quick fall and a violet glow. One strike in three is a soft
// distant glow over everything. Intensity scales strikes.
// ---------------------------------------------------------------------------
inline HwColor hz_lightning(const HwT &t, const HzSelf &s) {
  float cloud = sync_noise2(s.x * 2.5 + t.A * 0.08 + 50.0, s.y * 2.5 + 50.0, 81);
  HwColor base = hw_scale(HZ_VIOLET, 0.25f + 0.12f * cloud);

  uint32_t slot;
  float w;
  sync_slot(t.A, 7.0f, slot, w);
  float at = 0.6f + sync_random(slot, 1) * 4.5f;
  bool distant = sync_random(slot, 2) < 0.30f;
  float dd = hz_dist(s, sync_random(slot, 3), sync_random(slot, 4) * s.h);
  float peak = distant ? 0.45f : 0.80f + 0.20f * sync_random(slot, 30);
  float near = distant ? 1.0f : 0.40f + 0.60f * expf(-dd * dd / 0.06f);
  float rise = distant ? 0.20f : 0.04f, hold = distant ? 0.10f : 0.04f, fall = distant ? 2.5f : 5.0f;
  int strokes = distant ? 1 : 1 + (int) (sync_random(slot, 31) * 2.0f);

  // Fast enough that every light has risen before the first one falls: the
  // room sees one flash, not a ripple of them.
  float L = 0.0f;
  for (int k = 0; k < strokes; k++) {
    float u = w - at - dd / 12.0f - k * 0.22f, p = k ? peak * 0.85f : peak;
    if (u >= 0.0f)
      L = fmaxf(L, u < rise ? p * (u / rise) : u < rise + hold ? p : p * expf(-(u - rise - hold) * fall));
  }
  float glow = distant || w < at ? 0.0f : 0.5f * expf(-(w - at) * 1.5f);
  HwColor c = hw_add(base, hw_scale(HZ_VIOLET, glow * t.d));
  return hz_finish(hw_add(c, hw_scale(HZ_STRIKE, L * near * t.d)), HZ_STORM_FLOOR);
}

// ---------------------------------------------------------------------------
// Pumpkin Patch - a carved pumpkin at every light, each flame on its own
// noise, and every 6 s a draught crosses the room on a heading of its own:
// each flame ducks as it passes. Now and then one flame licks up bright on
// its own. Intensity scales the flicker, the duck and the licks.
// ---------------------------------------------------------------------------
inline HwColor hz_pumpkin(float lvl) {
  float warm = 0.6f + 0.4f * lvl;
  return {lvl, 0.28f * lvl * warm, 0.0f, 0, 0};
}
// One light's own occasional event: a window of `len` seconds somewhere in a
// slot of `period`, the slot phase spread per light so neighbours rarely
// agree. Returns seconds since the event began (negative before it).
inline float hz_own_event(const HwT &t, const HzSelf &s, float period, float len, uint32_t salt) {
  uint32_t slot;
  float w;
  sync_slot(t.A + (double) (s.off * period), period, slot, w);
  return w - sync_random(slot, salt + s.i) * (period - len);
}
inline HwColor hz_pumpkins(const HwT &t, const HzSelf &s) {
  double tj = t.A + (double) (s.off * 7.0f);
  float slow = sync_noise(tj, 0.30f, s.i + 1), fast = sync_noise(tj, 0.07f, s.i + 21);
  float lvl = 0.66f + t.d * (0.14f * (slow - 0.5f) + 0.05f * (fast - 0.5f));

  uint32_t slot;
  float w;
  sync_slot(t.A, 6.0f, slot, w);
  float g = -0.65f + (w - 1.0f) * 0.45f - hz_along(s, sync_random(slot, 5) * 6.2831853f);
  lvl += (0.40f - lvl) * expf(-g * g / 0.0128f) * t.d;   // a front 0.08 wide, at 0.45 room/s

  float u = hz_own_event(t, s, 5.0f, 0.5f, 190);
  if (u >= 0.0f && u < 0.5f) {
    float e = fminf(clamp01(u / 0.10f), clamp01((0.5f - u) / 0.35f));
    lvl += (0.98f - lvl) * 0.8f * hw_ease(e) * t.d;
  }
  return hz_finish(hz_pumpkin(lvl), HZ_EMBER);
}

// ---------------------------------------------------------------------------
// Witch's Cauldron - a pot at the room's centre. The brew swirls poison green
// and violet, boiling, brightest at the pot, and every 2.2 s a bubble bursts
// and its lime surge spreads out across the room, fading as it goes.
// Intensity scales the boil and the surges.
// ---------------------------------------------------------------------------
inline HwColor hz_cauldron(const HwT &t, const HzSelf &s) {
  float dd = hz_centre(s);
  float m = sync_noise2(s.x * 3.0 + t.A * 0.12 + 20.0, s.y * 3.0 + t.A * 0.08 + 20.0, 33);
  float boil = 1.0f + 0.25f * t.d * (sync_noise(t.A + (double) (s.off * 20.0f), 0.25f, s.i + 41) - 0.5f) * 2.0f;
  HwColor col = hw_scale(hw_mix(HZ_POISON, HZ_BREW_VIOLET, hw_ease(m)), (0.45f + 0.25f * expf(-dd * dd / 0.04f)) * boil);

  uint32_t slot;
  float w;
  sync_slot(t.A, 2.2f, slot, w);
  float u = w - dd / 0.8f;
  float env = u < 0.0f ? 0.0f : u < 0.25f ? hw_ease(u / 0.25f) : expf(-(u - 0.25f) * 4.0f);
  float amp = 0.45f * (1.0f - 0.6f * fminf(1.0f, dd / 0.55f));
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
    float gx = 0.05f + 0.90f * sync_noise(t.A, 3.5f + 1.0f * k, 300 + k * 7);
    float gy = (0.05f + 0.90f * sync_noise(t.A, 4.0f + 0.8f * k, 400 + k * 7)) * s.h;
    float dd = hz_dist(s, gx, gy);
    float body = 0.55f + 0.45f * sync_noise(t.A, 1.5f, 500 + k);
    g = fmaxf(g, body * expf(-dd * dd / 0.0242f));   // sigma 0.11
  }
  return hz_finish(hw_add(c, hw_scale(HZ_GHOST, 0.75f * g * t.d)), HZ_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Blood Moon - moonlight, then the eclipse shadow arrives from the west wall
// on a curved edge, takes 15 s to cross the room and turns it copper red; it
// holds, throbbing, and clears the same way, clouds scudding over the
// moonlight between. A two-minute cycle. Intensity scales the throb and the
// clouds.
// ---------------------------------------------------------------------------
inline HwColor hz_moon(const HwT &t, const HzSelf &s) {
  float edge = hz_dist(s, -0.6f, 0.5f * s.h) - 0.6f;   // 0 at the west wall, about 1 at the east
  double cd = fmod(t.A - (double) (edge * 15.0f), 120.0);
  if (cd < 0)
    cd += 120.0;
  float c = (float) cd, k;
  if (c < 25.0f) k = 0.0f;
  else if (c < 45.0f) k = hw_ease((c - 25.0f) / 20.0f);
  else if (c < 85.0f) k = 1.0f;
  else if (c < 105.0f) k = 1.0f - hw_ease((c - 85.0f) / 20.0f);
  else k = 0.0f;
  HwColor col = hw_mix(hw_scale(HZ_MOON_WHITE, 0.75f), hw_scale(HZ_COPPER, 0.65f), k);
  float throb = 1.0f - 0.25f * k * (0.5f - 0.5f * sinf(c * 2.0f)) * t.d;
  float cloud = 1.0f - 0.20f * t.d * (1.0f - k) * sync_noise2(s.x * 2.0 + t.A * 0.15 + 90.0, s.y * 2.0 + 90.0, 91);
  return hz_finish(hw_scale(col, throb * cloud), HZ_MOON_FLOOR);
}

// ---------------------------------------------------------------------------
// Heartbeat - a lub-dub every second from the room's centre, the beat
// rippling out to the walls in about a third of a second, a little weaker as
// it goes. Intensity scales the beat.
// ---------------------------------------------------------------------------
inline HwColor hz_heart(const HwT &t, const HzSelf &s) {
  float dd = hz_centre(s);
  uint32_t slot;
  float w;
  sync_slot(t.A - (double) (dd / 1.6f), 1.0f, slot, w);
  float lvl = 0.12f + t.d * (0.85f * expf(-7.0f * w) + (w > 0.30f ? 0.45f * expf(-7.0f * (w - 0.30f)) : 0.0f));
  float amp = 1.0f - 0.30f * fminf(1.0f, dd / 0.56f);
  return hz_finish(hw_scale(HZ_BLOOD, clamp01(lvl * amp)), HZ_BLOOD_FLOOR);
}

// ---------------------------------------------------------------------------
// Candlelight - every light is a candle on its own flicker: a slow sway and a
// quicker tremble, scattered per light so no two flames agree. Meant for a few
// lights (wall sconces). Intensity scales the flicker.
// ---------------------------------------------------------------------------
inline float hz_candle_lvl(const HwT &t, const HzSelf &s, float k) {
  double tj = t.A + (double) (s.off * 9.0f);
  float slow = sync_noise(tj, 0.8f, s.i + 61), fast = sync_noise(tj, 2.5f, s.i + 71);
  // A draught: now and then one flame leaps, well past what the flash rule allows.
  float flare = fmaxf(0.0f, sync_noise(tj, 0.5f, s.i + 81) - 0.58f) * 2.4f;
  return 0.50f + t.d * k * (1.30f * (slow - 0.5f) + 0.60f * (fast - 0.5f) + 1.1f * flare);
}
inline float hz_candles_lvl(const HwT &t, const HzSelf &s) { return clamp01(hz_candle_lvl(t, s, 1.0f)); }
inline HwColor hz_candles(const HwT &t, const HzSelf &s) {
  return hz_finish(hw_scale(HZ_CANDLE, hz_candles_lvl(t, s)), HZ_CANDLE_FLOOR);
}

// ---------------------------------------------------------------------------
// Candle Chase - the candles again, quieter, and every 5 s a swell of light
// travels the length of the row, left to right, then right to left on the
// next pass: each flame brightens as it arrives and settles as it leaves.
// Intensity scales the flicker and the swell.
// ---------------------------------------------------------------------------
inline float hz_chase_lvl(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 5.0f, slot, w);
  float p = -0.25f + 1.5f * (w / 5.0f);              // swell position, off the left edge to off the right
  if (slot & 1u) p = 1.0f - p;
  float dx = s.x - p;
  return clamp01(hz_candle_lvl(t, s, 0.6f) + 0.40f * t.d * expf(-dx * dx / 0.04f));
}
inline HwColor hz_chase(const HwT &t, const HzSelf &s) {
  return hz_finish(hw_scale(HZ_CANDLE, hz_chase_lvl(t, s)), HZ_CANDLE_FLOOR);
}

// A flame on the white channels instead of orange RGB: all warm white, with a
// little cold mixed into the bright moments, so a flare burns whiter. The
// level never falls below 5%.
inline void hz_candle_white(float lvl, float &ww, float &cw) {
  lvl = fmaxf(lvl, 0.05f);
  ww = lvl;
  cw = 0.35f * lvl * lvl;
}

// ---------------------------------------------------------------------------
// Graveyard Mist - two banks of fog drifting across the room at their own
// speeds. The colours are the night's (teal and bone, moss and lichen, or
// dusk violet and ash), the same dice as the outdoor Graveyard Mist uses.
// Intensity scales the fog's contrast.
// ---------------------------------------------------------------------------
inline HwColor hz_mist(const HwT &t, const HzSelf &s) {
  const HwColor *set = HZ_MIST_SETS[(int) (sync_random((uint32_t) hw_evening(t.T).night, 70) * 3.0f)];
  float a = sync_noise2(s.x * 2.0 + t.A * 0.060 + 10.0, s.y * 2.0 + t.A * 0.018 + 10.0, 71);
  float b = sync_noise2(s.x * 4.5 + t.A * 0.120 + 30.0, s.y * 4.5 + 30.0, 72);
  float d = 0.5f + (0.65f * a + 0.35f * b - 0.5f) * 1.6f * t.d;
  return hz_finish(hw_scale(hw_mix(set[0], set[1], hw_ease(clamp01(d))), 0.9f), HZ_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Trick-or-Treat - the room is all orange or all purple, and every 4 s the
// other colour sweeps in across it from a new direction, each light popping
// brighter as the sweep passes. The two are about equally bright, so the
// sweep itself is a change of hue, not a flash. Intensity scales how far each
// light swings and the pop.
// ---------------------------------------------------------------------------
inline HwColor hz_swap(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 4.0f, slot, w);
  float at = 1.0f + (hz_along(s, sync_random(slot, 6) * 6.2831853f) + 0.6f) * 1.5f;
  float k = hw_ease(clamp01((w - at) / 0.6f)), pop = 4.0f * k * (1.0f - k);
  bool odd = (slot & 1u) != 0;
  const HwColor &from = odd ? HZ_TREAT_PURPLE : HZ_TREAT_ORANGE, &to = odd ? HZ_TREAT_ORANGE : HZ_TREAT_PURPLE;
  return hz_finish(hw_scale(hw_mix(from, to, 0.5f + (k - 0.5f) * t.d), 0.75f + 0.25f * pop * t.d), HZ_TREAT_FLOOR);
}

// ---------------------------------------------------------------------------
// Bats at Dusk - an amber sky low in the south fading to violet in the north,
// and bats crossing it: three per 3 s, each on its own heading, wobbling as
// it flies and darkening the lights it passes over. Intensity scales the
// shadow.
// ---------------------------------------------------------------------------
inline HwColor hz_bats(const HwT &t, const HzSelf &s) {
  HwColor sky = hw_mix(HZ_DUSK_VIOLET, HZ_DUSK_AMBER, hw_ease(clamp01(s.y / s.h)));
  sky = hw_scale(sky, 0.85f + 0.10f * sync_noise2(s.x * 2.0 + t.A * 0.08 + 3.0, s.y * 2.0 + 3.0, 91));
  float shade = 0.0f;
  for (int k = 0; k < 3; k++) {
    uint32_t slot;
    float w;
    sync_slot(t.A + 1.0 * k, 3.0f, slot, w);
    float th = sync_random(slot, 100 + 10 * k) * 6.2831853f, ux = cosf(th), uy = sinf(th);
    float side = (sync_random(slot, 101 + 10 * k) - 0.5f) * 0.5f + 0.06f * sinf(w * 14.0f + (float) k);
    float along = -0.75f + w * 0.6f;
    float dd = hz_dist(s, 0.5f + ux * along - uy * side, 0.5f * s.h + uy * along + ux * side);
    shade = fmaxf(shade, expf(-dd * dd / 0.0098f));   // sigma 0.07
  }
  return hz_finish(hw_scale(sky, 1.0f - 0.6f * shade * t.d), HZ_DUSK_FLOOR);
}

// ---------------------------------------------------------------------------
// Witching Vortex - two spiral arms of violet and green turning about the
// room's centre, its eye glowing and pulsing brighter. Intensity scales the
// arms' contrast and the pulse.
// ---------------------------------------------------------------------------
inline HwColor hz_vortex(const HwT &t, const HzSelf &s) {
  float dx = s.x - 0.5f, dy = s.y - 0.5f * s.h, r = hypotf(dx, dy);
  float rot = (float) (fmod(t.A, 4.1887902047863905) * 1.5);   // a full turn of colour every 4.2 s
  float m = 0.5f + 0.5f * t.d * sinf(2.0f * atan2f(dy, dx) + 11.0f * r - rot);
  float eye = 1.0f + 0.15f * t.d * sinf((float) fmod(t.A, 6.283185307179586) * 3.0f);
  HwColor col = hw_scale(hw_mix(HZ_VIOLET, HZ_VORTEX_GREEN, clamp01(m)), 0.45f + 0.35f * eye * expf(-r * r / 0.03f));
  return hz_finish(col, HZ_NIGHT_FLOOR);
}

// ---------------------------------------------------------------------------
// Searchlight - two cold beams from beyond the south wall sweep the room on
// their own swings, and a light brightens hard as a beam passes over it.
// Intensity scales the beams.
// ---------------------------------------------------------------------------
inline HwColor hz_search(const HwT &t, const HzSelf &s) {
  float ang = atan2f(s.x - 0.5f, -(s.y - (s.h + 0.25f)));   // from straight north, as seen from the source
  // Fold at the swing period itself (7 s, 10 s), so the sine's argument wraps at 2 pi and the beam never jumps.
  float a1 = 0.9f * sinf((float) fmod(t.A, 7.0) * (6.2831853f / 7.0f));
  float a2 = 0.9f * sinf((float) fmod(t.A, 10.0) * (6.2831853f / 10.0f) + 2.0f);
  float d1 = ang - a1, d2 = ang - a2;
  float beam = fmaxf(expf(-d1 * d1 / 0.05f), 0.8f * expf(-d2 * d2 / 0.05f));
  HwColor base = hw_scale(HZ_BEAM_FLOOR, 1.5f);
  return hz_finish(hw_add(base, hw_scale(HZ_BEAM, 0.85f * beam * t.d)), HZ_BEAM_FLOOR);
}

// ---------------------------------------------------------------------------
// Haunting - a dim violet room; every 5 s a ghost appears somewhere in it with
// a pale glow, and a ring of cold light races out from that spot across the
// room, thinning as it goes. Intensity scales the glow and the ring.
// ---------------------------------------------------------------------------
inline HwColor hz_haunt(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 5.0f, slot, w);
  float dd = hz_dist(s, 0.1f + 0.8f * sync_random(slot, 120), (0.1f + 0.8f * sync_random(slot, 121)) * s.h);
  float r = 0.5f * w, e = dd - r;
  float ring = expf(-e * e / 0.02f) * expf(-r * 1.0f);   // sigma 0.1
  float core = expf(-dd * dd / 0.02f) * expf(-w * 1.5f);
  HwColor c = hw_scale(HZ_VIOLET, 0.20f);
  c = hw_add(c, hw_scale(HZ_GHOST, 0.75f * core * t.d));
  return hz_finish(hw_add(c, hw_scale(HZ_HAUNT_RING, 0.65f * ring * t.d)), HZ_NIGHT_FLOOR);
}


// ---------------------------------------------------------------------------
// Rolling Fog - every 10 s a bank of green-white fog rolls in from a new
// direction: a dense bright front leads, a thinner swirling body follows, and
// it drains out the far side before the next bank. Intensity scales the fog.
// ---------------------------------------------------------------------------
inline HwColor hz_fog(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 10.0f, slot, w);
  float f = -0.8f + w * 0.26f - hz_along(s, sync_random(slot, 140) * 6.2831853f);   // front, past this light when > 0
  float front = expf(-f * f / 0.012f);
  float swirl = 0.8f + 0.4f * sync_noise2(s.x * 3.0 + t.A * 0.2 + 70.0, s.y * 3.0 + 70.0, 73);
  float body = clamp01(f / 0.35f) * (1.0f - clamp01((f - 0.9f) / 0.3f)) * swirl;
  float d = clamp01(0.35f * body + 0.85f * front) * t.d;
  HwColor fog = hw_scale(hw_mix(HZ_FOG_DARK, HZ_FOG_LIGHT, d), 0.2f + 0.8f * d);
  return hz_finish(hw_add(hw_scale(HZ_FOG_FLOOR, 1.5f), fog), HZ_FOG_FLOOR);
}

// ---------------------------------------------------------------------------
// Travelling Bolt - the storm cloud again, and every 5 s a bolt enters at
// one wall and zigzags across the room like a fuse burning, each light
// flaring cold white as it arrives and dimming to a violet afterglow behind
// it; two bolts in five come as a pair. A new path each time. Intensity
// scales the bolt.
// ---------------------------------------------------------------------------
inline HwColor hz_bolt(const HwT &t, const HzSelf &s) {
  float cloud = sync_noise2(s.x * 2.5 + t.A * 0.08 + 60.0, s.y * 2.5 + 60.0, 82);
  HwColor base = hw_scale(HZ_VIOLET, 0.25f + 0.10f * cloud);

  uint32_t slot;
  float w;
  sync_slot(t.A, 5.0f, slot, w);
  float th = sync_random(slot, 150) * 6.2831853f;
  float a = hz_along(s, th), side = hz_along(s, th + 1.5707963f);
  float zig = 0.14f * sinf(a * 30.0f + (float) (slot % 97));
  float near = expf(-(side - zig) * (side - zig) / 0.012f);
  int strokes = sync_random(slot, 151) < 0.4f ? 2 : 1;
  float L = 0.0f, glow = 0.0f;
  for (int k = 0; k < strokes; k++) {
    float u = w - 1.5f - (a + 0.6f) * 0.5f - k * 0.3f;   // crosses the room in about 0.6 s
    if (u < 0.0f) continue;
    float p = k ? 0.7f : 1.0f;
    L = fmaxf(L, p * (u < 0.05f ? u / 0.05f : expf(-(u - 0.05f) * 4.0f)));
    glow = fmaxf(glow, 0.4f * expf(-u * 0.8f));
  }
  HwColor c = hw_add(base, hw_scale(HZ_STRIKE, L * near * t.d));
  return hz_finish(hw_add(c, hw_scale(HZ_AFTERGLOW, glow * near * t.d)), HZ_STORM_FLOOR);
}

// ---------------------------------------------------------------------------
// Possessed Room - the whole room one colour, breathing slowly: sickly green,
// then bruise violet, then blood red, each about as bright as the last. Every
// 8 s the room shudders - a quick shimmer runs across it as the hue lurches
// to the next. Intensity scales the shudder.
// ---------------------------------------------------------------------------
inline HwColor hz_possessed(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 8.0f, slot, w);
  const HwColor &from = HZ_POSSESSED[slot % 3], &to = HZ_POSSESSED[(slot + 1) % 3];
  float u = w - 6.0f - (hz_along(s, (float) (slot % 7)) + 0.6f) * 0.8f;
  float k = hw_ease(clamp01(u / 0.4f));
  float sh = expf(-(u - 0.2f) * (u - 0.2f) / 0.01f);
  float br = 0.55f + 0.12f * sinf((float) fmod(t.A, 6.283185307179586) * 1.0f + s.x * 3.0f);
  HwColor c = hw_scale(hw_mix(from, to, 0.5f + (k - 0.5f) * t.d), br);
  return hz_finish(hw_add(c, hw_scale(HZ_SHUDDER, 0.5f * sh * t.d)), HZ_POSSESS_FLOOR);
}

// ---------------------------------------------------------------------------
// Creeping Vines - a moss-dark room. Vines of poison green grow out from a
// corner along a winding path, a lime tip leading, until they reach the far
// wall; then they wither back the way they came. A 12 s breath, from a new
// corner each time, the lime tip throbbing as it grows. Intensity scales the
// green.
// ---------------------------------------------------------------------------
inline HwColor hz_vines(const HwT &t, const HzSelf &s) {
  uint32_t slot;
  float w;
  sync_slot(t.A, 12.0f, slot, w);
  float cx = sync_random(slot, 160) < 0.5f ? 0.0f : 1.0f, cy = sync_random(slot, 161) < 0.5f ? 0.0f : s.h;
  float dd = hz_dist(s, cx, cy) + 0.08f * sinf(s.x * 9.0f + s.y * 13.0f + (float) (slot % 61));
  float reach = (w < 6.0f ? w : 12.0f - w) * (1.3f / 6.0f);
  float g = clamp01((reach - dd) / 0.12f), tip = expf(-(reach - dd) * (reach - dd) / 0.01f);
  float throb = 0.8f + 0.2f * sinf(w * 8.0f);
  HwColor c = hw_add(hw_scale(HZ_MOSS_FLOOR, 1.5f), hw_scale(HZ_VINE, 0.8f * g * t.d));
  return hz_finish(hw_add(c, hw_scale(HZ_VINE_TIP, 0.7f * throb * tip * t.d)), HZ_MOSS_FLOOR);
}

// ---------------------------------------------------------------------------
// Will-o'-the-Wisps - a black-blue bog with three wisps of blue-green light
// in it. Each hovers somewhere, pulsing, then darts to a new spot; they take
// turns, a second and a third apart. Intensity scales the wisps.
// ---------------------------------------------------------------------------
inline HwColor hz_wisps(const HwT &t, const HzSelf &s) {
  float g = 0.0f;
  for (int k = 0; k < 3; k++) {
    uint32_t slot;
    float w;
    sync_slot(t.A + (4.0 / 3.0) * k, 4.0f, slot, w);
    float ax = 0.05f + 0.9f * sync_random(slot, 170 + 2 * k), ay = (0.05f + 0.9f * sync_random(slot, 171 + 2 * k)) * s.h;
    float bx = 0.05f + 0.9f * sync_random(slot + 1, 170 + 2 * k), by = (0.05f + 0.9f * sync_random(slot + 1, 171 + 2 * k)) * s.h;
    float m = hw_ease(clamp01((w - 3.2f) / 0.8f));   // the dart, in the last 0.8 s of the hover
    float dd = hz_dist(s, ax + (bx - ax) * m, ay + (by - ay) * m);
    float pulse = 0.80f + 0.20f * sinf(w * 3.0f + (float) k);
    g = fmaxf(g, pulse * expf(-dd * dd / 0.02f));
  }
  return hz_finish(hw_add(hw_scale(HZ_BOG_FLOOR, 1.3f), hw_scale(HZ_WISP, 0.9f * g * t.d)), HZ_BOG_FLOOR);
}
