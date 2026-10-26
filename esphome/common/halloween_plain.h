#pragma once

// ---------------------------------------------------------------------------
// The flame and colour maths of the plain Halloween candles
// (halloween_plain_effects.yaml), for any PWM RGB light: a Lumary, a single
// bulb, a chandelier of them.
//
// hl_flame() is Candle Flicker's flame, line for line (accent_light_
// nonaddressable_effects.yaml): value noise on the shared clock for the wander,
// one draught for the room, each light adding its own quick shimmer. The
// candles here wear other colours, so their flicker only counts as a flash if
// the LIGHT changes: hl_tint() holds a colour to the luminance a warm candle
// would have at that level, so a hue change is never a brightness change and a
// drifting or swapping colour adds no flashes of its own.
//
// Pure functions of the shared clock and the light's offset, as everywhere
// else: a rebooted light rejoins in step, and nothing is sent between lights.
// ---------------------------------------------------------------------------

#include "light_effects_helpers.h"

//                                  r      g      b
constexpr float HL_PURPLE[3] = {0.55f, 0.00f, 1.00f};
constexpr float HL_GREEN[3]  = {0.12f, 1.00f, 0.05f};
constexpr float HL_ORANGE[3] = {1.00f, 0.30f, 0.02f};
constexpr float HL_EMBER[3]  = {1.00f, 0.04f, 0.00f};
constexpr float HL_GHOST[3]  = {0.35f, 1.00f, 0.50f};

// Luminance a flame of colour `level` 1 has: dim enough that a saturated purple
// can reach it, so all the colours can share it.
constexpr float HL_Y = 0.16f;

inline float hl_ease(float x) { x = clamp01(x); return x * x * (3.0f - 2.0f * x); }
// Weights of a typical RGB bulb's three LEDs against warm white at full (the figures tests/effects_flash_test.cpp counts flashes with).
inline float hl_lum(float r, float g, float b) { return 0.2167f * r + 0.4078f * g + 0.0612f * b; }

// The colour (r, g, b) at luminance y: a hue, held at one brightness.
inline void hl_tint(float y, float r, float g, float b, float &R, float &G, float &B) {
  float l = hl_lum(r, g, b), k = l > 1e-4f ? y / l : 0.0f;
  R = clamp01(r * k); G = clamp01(g * k); B = clamp01(b * k);
}
// The same between two hues: mix them, then hold the luminance.
inline void hl_tint2(float y, const float a[3], const float b[3], float m, float &R, float &G, float &B) {
  hl_tint(y, a[0] + (b[0] - a[0]) * m, a[1] + (b[1] - a[1]) * m, a[2] + (b[2] - a[2]) * m, R, G, B);
}

// Candle Flicker's level, 0..1, for the shared clock T and this light's offset.
inline float hl_flame(double T, float off) {
  const float STEP = 0.2f;
  uint32_t b0 = sync_bucket(T, STEP);
  float f = (float) (T / STEP - floor(T / STEP));
  float n0 = sync_random(b0, 1), n1 = sync_random(b0 + 1, 1);
  float noise = n0 + (n1 - n0) * (f * f * (3.0f - 2.0f * f));
  float shimmer = sync_noise(T + off * 7.0f, 0.07f, 9) - 0.5f;

  uint32_t slot; float within;
  sync_slot(T, 5.0f, slot, within);
  float gutter_at = sync_random(slot, 2) * 4.0f;
  const float X = fx_extra();
  const float CALM = fx_calm(0.0f);
  float gutter_len = (0.18f + sync_random(slot, 3) * 0.40f) * (1.0f + 1.5f * X);
  bool guttering = (within >= gutter_at && within < gutter_at + gutter_len);

  float flame = 0.62f + 0.30f * noise * fx_calm(0.25f) * (1.0f + 0.6f * X) - 0.10f * X + 0.04f * shimmer;
  return clamp01(guttering ? flame + ((0.18f + 0.10f * noise) - flame) * CALM : flame);
}
