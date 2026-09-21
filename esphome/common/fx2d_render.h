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
