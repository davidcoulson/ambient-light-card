#pragma once

// ---------------------------------------------------------------------------
// Shared renderers for the brightness-as-intensity 2D effects: 2D Pacifica and
// the 2D Aurora colour variants. Each is a pure function of (x, y, T) with no
// state, so every light computes its own point of one room-wide scene and they
// all agree - the same contract as the rest of the 2D effects.
//
// Brightness is a plain dimmer for these, as for every other effect: callers
// pass apply_level = false and write through out_rgb() (PWM lights) or let
// ESPHome's colour correction dim them (addressable rings).
//
// Intensity shapes the effect - calm at 0, today's look at "normal" (1.0, the
// 128 midpoint of the room's intensity helper), rougher up to 2.0 at 255 - and
// comes from fx_intensity(), not the brightness slider. Intensity changes
// amplitude and contrast only, never speed or phase, so it cannot desync a room.
// ---------------------------------------------------------------------------

#include "light_effects_helpers.h"

// Intensity at "normal" (the 128 midpoint of the room's intensity control):
// the look these effects have always had at full brightness.
constexpr float FX_INTENSITY_NORMAL = 1.0f;

// Compress highlights smoothly instead of flat-clipping. Values up to the knee
// pass through untouched; anything above eases asymptotically toward 1. Keeps
// gradation in the whitecaps and bright folds that a hard clamp flattens into a
// solid white block. (Host-tested: without this, full-intensity Pacifica peaked
// at 1.83 and clipped for long stretches.)
inline float soft_knee(float v) {
  const float K = 0.72f;
  if (v <= K)
    return v < 0.0f ? 0.0f : v;
  return K + (1.0f - K) * (1.0f - expf(-(v - K) / (1.0f - K)));
}

// 2D Pacifica: four plane waves crossing the room from different directions,
// layered through three palettes the way FastLED's Pacifica is, but sampled at
// this light's (x, y) so the swell visibly travels across the room.
//
//   intensity  0..2, 1 = normal: how much of the brighter layers and the
//              whitecaps come through; above 1, more swell and much more foam
//   energy     fixed per variant: Storm runs rougher than Calm Lagoon
//   speed      fixed per variant: deliberately NOT the slider
//   apply_level  true (basement): the effect sets its own level with a visible
//                floor. false (addressable rings): ESPHome's colour correction
//                already scales every LED by the slider, so the effect must not
//                apply level too or low brightness would be dimmed twice.
inline void pacifica2d(float px, float py, double T, float intensity, float energy, float speed,
                       const uint8_t deep[16][3], const uint8_t mid[16][3], const uint8_t bright[16][3],
                       float &r, float &g, float &b, bool apply_level = true) {
  // kx, ky (rad per room unit), hz (cycles/s at speed 1), phase offset.
  static const float WV[4][4] = {
      {5.2f, 1.1f, 0.110f, 0.0f},     // long swell from the west
      {-3.4f, 4.3f, 0.083f, 1.7f},    // cross swell from the south-east
      {7.1f, -2.6f, 0.157f, 3.1f},    // shorter chop
      {-6.3f, -5.0f, 0.197f, 4.4f},   // choppy counter-wave
  };
  // Everything below that depends only on T (and the variant's speed) is the
  // same for every LED in a frame. A ring calls this 13 times per frame with
  // one T, so compute it once and let the other 12 reuse it; the basement calls
  // once per frame and simply recomputes. Same maths, same result either way.
  static double c_T = -1.0;
  static float c_speed = -1.0f, c_tp[4], c_d0, c_d1, c_d2;
  if (T != c_T || speed != c_speed) {
    c_T = T;
    c_speed = speed;
    // Temporal phase folded in DOUBLE first: T * hz is ~2e8, which a float
    // cannot resolve (the trap that froze three earlier 2D effects).
    for (int i = 0; i < 4; i++)
      c_tp[i] = (float) (fmod(T * (double) (WV[i][2] * speed), 1.0) * 6.283185307179586);
    // Slow drift of where each layer sits in its palette, also in double.
    c_d0 = (float) (fmod(T * 0.021 * speed, 1.0) * 16.0);
    c_d1 = (float) (fmod(T * 0.034 * speed, 1.0) * 16.0);
    c_d2 = (float) (fmod(T * 0.047 * speed, 1.0) * 16.0);
  }

  float wv[4];
  for (int i = 0; i < 4; i++) {
    // The spatial term is small and float-safe.
    float ph = WV[i][0] * px + WV[i][1] * py - c_tp[i] + WV[i][3];
    wv[i] = sinf(ph) * 0.5f + 0.5f;
  }
  float d0 = c_d0, d1 = c_d1, d2 = c_d2;

  float I = clamp01(intensity);
  float X = fx_over_normal(intensity);         // 0 up to normal, then 0..1
  float gain = energy * (0.25f + 0.75f * I) * (1.0f + 0.35f * X);   // mid and bright layers
  float cap = energy * I * I * (1.0f + 1.5f * X);                     // whitecaps mostly near the top

  auto pos16 = [](float v) {
    v = fmodf(v, 16.0f);
    return v < 0.0f ? v + 16.0f : v;
  };
  float lr, lg, lb;

  // Deep water is always present, so even minimum intensity reads as the sea.
  lerp_palette16(deep, pos16((wv[0] + wv[1]) * 8.0f + d0), lr, lg, lb);
  r = lr;
  g = lg;
  b = lb;

  lerp_palette16(mid, pos16(wv[0] * 16.0f + d1), lr, lg, lb);
  float w = wv[0] * 0.65f * gain;
  r += lr * w;
  g += lg * w;
  b += lb * w;

  lerp_palette16(mid, pos16(wv[1] * 16.0f + d2), lr, lg, lb);
  w = wv[1] * 0.55f * gain;
  r += lr * w;
  g += lg * w;
  b += lb * w;

  lerp_palette16(bright, pos16(wv[2] * 16.0f + d1), lr, lg, lb);
  w = wv[2] * wv[2] * 0.85f * gain;   // squared: crests stand up out of the swell
  r += lr * w;
  g += lg * w;
  b += lb * w;

  // Whitecaps where two crests meet. Deterministic, so the foam travels with
  // the waves and every light agrees on where it is.
  float c = wv[2] * wv[3];
  float foam = c * c * c * 220.0f * cap;
  float wc = wv[3] * wv[3] * wv[3] * 0.65f * gain;
  r += bright[15][0] * wc + foam;
  g += bright[15][1] * wc + foam;
  b += bright[15][2] * wc + foam;

  // Level: a calm floor that stays visible at minimum brightness.
  float level = (apply_level ? (0.18f + 0.82f * I) : 1.0f) / 255.0f;
  r = soft_knee(r * level);
  g = soft_knee(g * level);
  b = soft_knee(b * level);
}

// 2D Aurora in a palette: curtains folding across the room - the geometry of
// 2D Aurora Curtains - coloured from a 16-entry palette that drifts slowly.
//
//   intensity  0..2, 1 = normal: a soft even glow at 0; sharp, bright,
//              shimmering folds at 1; above 1, sharper, brighter and livelier
//   drift      fixed per variant: how fast the palette walks (cycles/s)
//   apply_level  as for pacifica2d: false on the addressable rings.
inline void aurora2d(float px, float py, double T, float intensity, double drift,
                     const uint8_t pal[16][3], float &r, float &g, float &b, bool apply_level = true) {
  float I = clamp01(intensity);
  float X = fx_over_normal(intensity);

  // T-only terms, computed once per frame and shared by every LED (see
  // pacifica2d). The fold line wanders, so the curtain is never straight.
  static double c_T = -1.0, c_drift = -1.0;
  static float c_t, c_warp, c_pd, c_shn;
  if (T != c_T || drift != c_drift) {
    c_T = T;
    c_drift = drift;
    c_t = sync_fold(T);
    c_warp = sync_noise(T, 6.0f, 61) * 0.45f;
    c_pd = (float) (fmod(T * drift, 1.0) * 16.0);
    c_shn = sync_noise(T, 0.35f, 63);
  }
  float t = c_t;
  float warp = c_warp;
  float s = px * 2.6f + warp * 2.0f - py * 0.5f + t * 0.16f;
  float band = sinf(s * 3.1415927f);
  float fold = band > 0.0f ? band : 0.0f;

  // Higher intensity sharpens the folds into distinct bright sheets.
  float curtain = powf(fold, 1.2f + 1.8f * I + 1.5f * X);

  // Colour: walk the palette slowly (folded in double), plus a little up the
  // curtain and across the room so it is not one flat colour.
  float pd = c_pd;
  float pos = fmodf(pd + fold * 5.0f + py * 6.0f, 16.0f);
  if (pos < 0.0f)
    pos += 16.0f;
  float cr, cg, cb;
  lerp_palette16(pal, pos, cr, cg, cb);

  // Shimmer along the curtain, only at higher intensity.
  float shim = 1.0f + (0.30f * I + 0.30f * X) * (c_shn - 0.5f) * 2.0f * curtain;

  // A faint, even sky glow when calm; contrast and level rise with intensity.
  float glow = 0.05f + 0.08f * (1.0f - I);
  float lvl = (glow + (0.35f + 0.80f * I + 0.35f * X) * curtain) * shim * (apply_level ? (0.25f + 0.75f * I) : 1.0f);
  r = soft_knee(cr / 255.0f * lvl);
  g = soft_knee(cg / 255.0f * lvl);
  b = soft_knee(cb / 255.0f * lvl);
}

// ---------------------------------------------------------------------------
// Room-scale renderers shared by the basement lights and the media-room rings.
//
// Each was a basement effect written inline for one light; pulled out here so
// a ring can sample the same scene at each of its LEDs. The arithmetic is the
// basement's, operation for operation, so the basement renders the same frames
// it always did. What they need from a room is its aspect (depth / width) and
// centre, passed as a Room2D, where the basement code used ROOM_* directly.
//
// Output is 0..1 RGB, before brightness: a PWM light passes it to out_rgb(), a
// ring to ht_ring_rgb8(). Intensity enters only through fx_swing() /
// fx_accents(), both exactly 1 at normal.
// ---------------------------------------------------------------------------
struct Room2D {
  float aspect;   // depth / width: y runs 0..aspect
  float cx, cy;   // the room's centre, in the same units
};

// Hearth: a fire at (fire_x, fire_y * aspect). Brightness falls off with
// distance, the flicker is shared so the room surges together, a surge reaches
// near points a beat before far ones, and `salt` gives each point its own
// shimmer (the light's MAC hash; per LED on a ring).
inline void hearth2d(float px, float py, double T, double fire_x, double fire_y, const Room2D &room,
                     uint32_t salt, float &r, float &g, float &b) {
  static const uint8_t pal_fire[16][3] = {
    {30,0,0},{60,4,0},{95,10,0},{130,20,0},
    {165,34,0},{195,52,2},{220,74,4},{238,98,8},
    {250,124,14},{255,150,24},{255,174,40},{255,196,64},
    {255,215,96},{255,231,136},{255,243,182},{255,250,220}
  };
  float dx = px - fire_x, dy = py - fire_y * room.aspect;
  float dist = sqrtf(dx * dx + dy * dy);

  double Td = T - dist * 0.10;          // light reaches near points first

  float body = 0.55f * sync_noise(Td, 0.55f, 1)
             + 0.30f * sync_noise(Td, 0.19f, 2)
             + 0.15f * sync_noise(Td, 0.07f, 3);

  uint32_t slot; float within;
  sync_slot(Td, 4.0f, slot, within);
  float at = sync_random(slot, 4) * 3.0f;
  float flare = within > at
              ? (0.30f + 0.45f * sync_random(slot, 5)) * fx_accents() * expf(-2.2f * (within - at))
              : 0.0f;

  const float R = 0.30f;
  float fall = 1.0f / (1.0f + (dist / R) * (dist / R));
  float shim = 0.90f + 0.20f * sync_noise(T, 0.13f, salt);

  // Intensity: flicker depth (fx_swing) and flares (fx_accents).
  float level = clamp01(fall * (0.42f + 0.55f * (0.5f + (body - 0.5f) * fx_swing()) + flare) * shim);

  lerp_palette16(pal_fire, clamp01(level * 0.70f + fall * 0.30f) * 15.0f, r, g, b);
  r = r / 255.0f * level;
  g = g / 255.0f * level;
  b = b / 255.0f * level;
}

// Clouds: overcast banks drifting over blue sky, with clear gaps between them.
// Intensity sharpens the banks against the sky.
inline void clouds2d(float px, float py, double T, float &r, float &g, float &b) {
  // Position shifts the sample point; time drifts it along. All in double:
  // T * 0.014 is ~2.5e7, where a float resolves only to 2.0 and would swallow
  // the position term whole.
  double u = px * 2.2 + T * 0.014;
  double v = py * 1.8 - T * 0.006;

  float n = 0.55f * sync_noise(u + v * 0.7, 1.0f, 21)
          + 0.30f * sync_noise(u * 2.1 - v, 0.5f, 22)
          + 0.15f * sync_noise(u * 4.3 + v * 2.0, 0.25f, 23);

  float cloud = clamp01((n - 0.42f) * 2.4f * fx_swing());   // gaps of clear sky

  r = 0.05f + 0.70f * cloud;
  g = 0.11f + 0.70f * cloud;
  b = 0.30f + 0.52f * cloud;
}

// Noise Drift: soft organic colour clouds sliding across the room, on layers
// at unrelated rates so nothing repeats visibly. Intensity is colour depth.
inline void noise_drift2d(float px, float py, double T, float &r, float &g, float &b) {
  // Double throughout - see clouds2d. A float cannot hold T * rate.
  double u = px * 1.7 + T * 0.020;
  double v = py * 1.5 - T * 0.013;

  float hue = 0.60f * sync_noise(u + v * 0.6, 1.0f, 31)
            + 0.40f * sync_noise(u * 1.9 - v * 1.3, 0.6f, 32);
  float lum = 0.35f + 0.45f * sync_noise(u * 0.8 + v, 1.4f, 33);

  hsv_to_rgb(fmodf(hue, 1.0f), clamp01(0.72f * fx_swing()), clamp01(lum), r, g, b);
}

// Radar Sweep: a beam rotating about the room's centre with a fading trail.
// Intensity lengthens the trail.
inline void radar2d(float px, float py, double T, const Room2D &room, float &r, float &g, float &b) {
  float ang = atan2f(py - room.cy, px - room.cx);           // -pi..pi
  float beam = (float) (fmod(T, 4.0) / 4.0) * 6.2831853f - 3.1415927f;

  // Angle BEHIND the beam, so the trail lags rather than leads.
  float da = beam - ang;
  while (da < 0.0f) da += 6.2831853f;
  while (da > 6.2831853f) da -= 6.2831853f;

  float level = 0.03f + 0.95f * expf(-da * (2.6f / fx_swing()));     // sharp head, long tail

  r = level * 0.15f;
  g = level;
  b = level * 0.35f;
}

// Shockwave: three rings expanding at once from different points at staggered
// times, each its own colour. Intensity widens the rings.
inline void shockwave2d(float px, float py, double T, const Room2D &room, float &r, float &g, float &b) {
  r = 0; g = 0; b = 0;
  const float W = 0.09f * fx_swing();
  for (int i = 0; i < 3; i++) {
    // Each ring runs on its own offset slot, so they overlap.
    double Ti = T + i * 1.7;
    uint32_t slot; float within;
    sync_slot(Ti, 4.5f, slot, within);

    float ox = sync_random(slot, 70 + i);
    float oy = sync_random(slot, 80 + i) * room.aspect;
    float dx = px - ox, dy = py - oy;
    float dist = sqrtf(dx * dx + dy * dy);

    float front = within * 0.34f;
    float amp = expf(-sq((dist - front) / W))
              * (1.0f - within / 4.5f);

    float hue = fmodf(sync_random(slot, 90 + i) * 0.4f + i * 0.33f, 1.0f);
    float rr, gg, bb;
    hsv_to_rgb(hue, 0.85f, clamp01(amp), rr, gg, bb);
    if (rr > r) r = rr;
    if (gg > g) g = gg;
    if (bb > b) b = bb;
  }
  r += 0.02f;
  g += 0.02f;
  b += 0.03f;
}

// Comet: a bright head crossing the room on a fresh heading each pass, its tail
// fading behind it along its own track. Intensity lengthens the tail.
inline void comet2d(float px, float py, double T, const Room2D &room, float &r, float &g, float &b) {
  uint32_t slot; float within;
  sync_slot(T, 3.5f, slot, within);

  float ang = sync_random(slot, 101) * 6.2831853f;
  float ux = cosf(ang), uy = sinf(ang);

  // Head travels from one side to the other along the heading.
  float travel = within / 3.5f;
  float hx = room.cx - ux * 0.8f + ux * 1.6f * travel;
  float hy = room.cy - uy * 0.8f + uy * 1.6f * travel;

  float dx = px - hx, dy = py - hy;
  float along = dx * ux + dy * uy;            // + is ahead of the head
  float across = fabsf(dx * uy - dy * ux);

  float tail = along < 0.0f ? expf(along * (4.5f / fx_swing())) : expf(-along * 26.0f);
  float level = tail * expf(-sq(across / 0.11f));
  // The head has left the room by now but the tail has not; ease it out over
  // the last 15 % rather than cutting it dead at the slot end.
  if (travel > 0.85f) level *= (1.0f - travel) / 0.15f;

  float hue = fmodf(0.08f + sync_random(slot, 102) * 0.18f, 1.0f);
  hsv_to_rgb(hue, 0.55f, clamp01(0.02f + 0.95f * level), r, g, b);
}
