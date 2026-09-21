#pragma once

// Shared helpers for the hand-written lambda light effects in
// accent_light_nonaddressable_effects.yaml (and any sibling effects
// file that wants them). Previously each effect redefined its own
// copy of these; pulling them out here means a fix or tweak only
// needs to happen once.

#include <cmath>
#include <cstdint>
#include <sys/time.h>
#include <ctime>


// Standard HSV -> RGB conversion. h, s, v in [0, 1]; r, g, b out in [0, 1].
inline void hsv_to_rgb(float h, float s, float v, float &r, float &g, float &b) {
  h = fmodf(h, 1.0f);
  if (h < 0)
    h += 1.0f;

  float i = floorf(h * 6.0f);
  float f = h * 6.0f - i;
  float p = v * (1.0f - s);
  float q = v * (1.0f - f * s);
  float w = v * (1.0f - (1.0f - f) * s);

  switch (((int) i) % 6) {
    case 0:
      r = v; g = w; b = p; break;
    case 1:
      r = q; g = v; b = p; break;
    case 2:
      r = p; g = v; b = w; break;
    case 3:
      r = p; g = q; b = v; break;
    case 4:
      r = w; g = p; b = v; break;
    default:
      r = v; g = p; b = q; break;
  }
}

// Look up a fractional position (0-16, wrapping) in a 16-entry RGB
// palette (0-255 per channel) and linearly interpolate between the
// two nearest entries. r, g, b out in [0, 255].
inline void lerp_palette16(const uint8_t pal[16][3], float pos, float &r, float &g, float &b) {
  int idx0 = (int) pos;
  int idx1 = (idx0 + 1) % 16;
  float frac = pos - idx0;

  r = pal[idx0][0] * (1 - frac) + pal[idx1][0] * frac;
  g = pal[idx0][1] * (1 - frac) + pal[idx1][1] * frac;
  b = pal[idx0][2] * (1 - frac) + pal[idx1][2] * frac;
}

// Clamp a 0-1 output level. The lambda effects below write straight to
// float outputs, which reject out-of-range values, and several of the
// effects sum or multiply terms that can briefly overshoot.
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// ---------------------------------------------------------------------------
// Shared phase clock.
//
// Every effect used to take its phase from millis(), which counts from THAT
// light's boot. Thirty lights booted at thirty different moments sit at thirty
// unrelated phases forever, which is why a room full of them never pulsed
// together. These helpers swap that for wall-clock time, set by SNTP: every
// light independently
// computes the same value for the same instant, so they agree without talking
// to each other. There is no leader, no traffic, and a light that reboots
// rejoins in phase on its very next update.
//
// Alignment is bounded by each effect's update_interval plus clock error -
// invisible on anything slower than a hard strobe.
// ---------------------------------------------------------------------------

// Absolute epoch seconds, straight from the system clock.
//
// This reads gettimeofday() directly rather than interpolating between stamps
// taken by an on_time cron, which is what an earlier version did. That version
// was wrong in a subtle way: the cron fires from ESPHome's main loop, so the
// stamp lands however late that particular device's loop happens to be. The
// delay is small but STABLE per device and DIFFERENT between devices, which is
// precisely the shape of error that pulls lights out of phase - and it was
// measurable at roughly 200 ms between two lights sitting next to each other.
//
// The system clock already carries microsecond resolution and SNTP sets it
// with full precision, so interpolating from a jittery stamp was throwing away
// accuracy that was there for free.
constexpr time_t SYNC_MIN_VALID = 1546300800;  // 2019-01-01; anything earlier means "not set"

inline double sync_seconds() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);

  // Clock not set yet (still booting, or no NTP): fall back to uptime. The
  // effect keeps running, just not in step until time arrives - and falls
  // straight back into step on its own once it does.
  if (tv.tv_sec < SYNC_MIN_VALID)
    return millis() / 1000.0;

  return (double) tv.tv_sec + tv.tv_usec / 1000000.0;
}

// Free-running seconds for phase math, folded into a 24 h window.
//
// The fold matters: a float cannot hold epoch seconds usefully (about 1.8e9
// needs 31 bits, leaving 128 s of resolution), so a raw cast freezes every
// effect. Folded to 24 h the value keeps ~8 ms of resolution.
//
// The price is one jump cut a day: at the fold, anything whose rate does not
// divide 24 h snaps to an unrelated phase. Every light snaps identically, so
// it never costs sync, but it IS visible - and folding at UTC midnight put it
// at 8 pm Eastern, the middle of the evening. The +15 h shift moves the fold
// to 09:00 UTC, which is 4-5 am here. Cast to float ONLY through this.
inline float sync_fold(double t) {
  double f = fmod(t + 54000.0, 86400.0);
  if (f < 0)
    f += 86400.0;
  return (float) f;
}

inline float sync_t() { return sync_fold(sync_seconds()); }

// Deterministic stand-in for random_uint32(). An effect that rolls its own
// dice can never stay in step, so the ones that need a random hue or interval
// derive it from the shared clock instead: same bucket, same number, on every
// light. `salt` separates independent draws inside one effect.
inline uint32_t sync_hash(uint32_t bucket, uint32_t salt) {
  uint32_t h = bucket * 2654435761u + salt * 2246822519u;
  h ^= h >> 15; h *= 2246822519u;
  h ^= h >> 13; h *= 3266489917u;
  h ^= h >> 16;
  return h;
}

// The same source as a 0.0 - 1.0 float.
inline float sync_random(uint32_t bucket, uint32_t salt) {
  return (sync_hash(bucket, salt) & 0xFFFFFFu) / 16777216.0f;
}

// Convert a whole-number double to a bucket index, wrapping modulo 2^32.
//
// This exists because of a real bug. Epoch seconds are ~1.8e9, so dividing by
// any period under ~0.42 s gives a quotient past UINT32_MAX, and casting an
// out-of-range double straight to uint32_t is undefined behaviour - on this
// soft-float RISC-V it SATURATES to 0xFFFFFFFF. Every fast bucket therefore
// came out as the same constant: Disco Sparkle and Sparkle (Picker) never
// sparked, Rainfall Hush never rained, and the quick flicker layers of the
// fire effects collapsed into a fixed sawtooth. Going through uint64_t first
// is in range, and the narrowing that follows is well-defined modular wrap.
// All lights wrap identically, so sync is unaffected.
inline uint32_t sync_u32(double whole) {
  return (uint32_t) (uint64_t) whole;
}

// Which numbered slot of length period_s we are in. Effects use this as the
// bucket so every light advances to its next draw at the same instant.
inline uint32_t sync_bucket(double t, double period_s) {
  return sync_u32(floor(t / period_s));
}

// Split the shared clock into fixed-length slots. `slot` numbers the current
// one - feed it to sync_random for the draws that slot should make - and
// `within` is how far into it we are, in seconds.
//
// Fixed-length slots with randomised CONTENT are what make the irregular
// effects syncable. Randomising the slot LENGTH instead, which is what a
// state machine does when it picks its next pause, would mean a light could
// only know which slot it was in by having watched every slot before it, and
// two lights that started at different moments would never agree again.
inline void sync_slot(double t, float period_s, uint32_t &slot, float &within) {
  double f = fmod(t, (double) period_s);
  if (f < 0)
    f += period_s;
  slot = sync_u32(floor(t / period_s));
  within = (float) f;
}

// A fixed 0-1 offset unique to this light, hashed from its MAC suffix.
//
// Not every effect wants unison. A room of 30 lights all twinkling on the same
// beat reads as one big blinking lamp, not a field of stars - scattered is the
// point of a twinkle. So the scattered effects still run off the shared clock,
// which keeps them deterministic and lets a rebooted light rejoin correctly,
// but shift their phase by this per-light constant so they spread out on
// purpose instead of by the accident of boot order. Offset 0 gives unison;
// that is what the rhythmic effects (Lightning Storm, Disco Strobe, Fireworks
// Burst) use, because those DO want every light firing together.
inline uint32_t mac_hash32(const char *id_str) {
  static uint32_t cached = 0;
  if (cached == 0) {
    uint32_t h = 2166136261u;
    for (const char *p = id_str; *p; ++p) {
      h ^= (uint8_t) *p;
      h *= 16777619u;
    }
    cached = h ? h : 1u;
  }
  return cached;
}

inline float sync_offset(const char *id_str) {
  return (mac_hash32(id_str) & 0xFFFFFFu) / 16777216.0f;
}

// Write one RGB frame to the three PWM outputs, scaled by the light's
// brightness.
//
// The lambda effects drive the outputs directly rather than going through the
// light, which used to mean the brightness slider did nothing while an effect
// ran. This applies it, through the light's own gamma curve so that 50 % on the
// slider dims an effect by the same amount it dims a plain colour. At 100 % the
// scale is exactly 1 and every effect looks as it always has. The powf is only
// paid when the slider actually moves.
//
// Templated so this header needs nothing from ESPHome; pass id(accent_light)
// and the three id(..._output)s.
template<typename L>
inline float fx_brightness_scale(L *light) {
  static float last_br = -1.0f, scale = 1.0f;
  float br = light->current_values.get_brightness();
  if (br != last_br) {
    last_br = br;
    float gamma = light->get_gamma_correct();
    scale = (br >= 1.0f) ? 1.0f : (gamma > 0.0f ? powf(br, gamma) : br);
  }
  return scale;
}

template<typename L, typename O>
inline void out_rgb(L *light, O *ro, O *go, O *bo, float r, float g, float b) {
  float scale = fx_brightness_scale(light);
  ro->set_level(clamp01(r) * scale);
  go->set_level(clamp01(g) * scale);
  bo->set_level(clamp01(b) * scale);
}

// The same for the white-only effects on RGBWW bulbs: colour channels held
// off, warm and cold white scaled by the slider.
template<typename L, typename O>
inline void out_white(L *light, O *ro, O *go, O *bo, O *wo, O *co, float ww, float cw) {
  float scale = fx_brightness_scale(light);
  ro->set_level(0.0f);
  go->set_level(0.0f);
  bo->set_level(0.0f);
  wo->set_level(clamp01(ww) * scale);
  co->set_level(clamp01(cw) * scale);
}

// Square a value. powf(x, 2.0f) is the slowest call in libm and this chip has
// no FPU, so squaring by multiplication is a real saving in per-frame code.
inline float sq(float v) { return v * v; }

// Milliseconds on the shared clock, as a wrapping uint32. Integer-only, for
// effects that just need ticks or positions and should not pay for doubles.
// Wraps every ~49 days, identically on every light.
inline uint32_t sync_ms() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < SYNC_MIN_VALID)
    return millis();
  return (uint32_t) ((uint64_t) tv.tv_sec * 1000ULL + (uint64_t) (tv.tv_usec / 1000));
}

// Local wall-clock parts, for the effects that encode the actual time. The
// time component sets TZ, so localtime_r() gives local time even though the
// underlying clock is UTC.
inline void sync_localtime(double t, struct tm &out) {
  time_t tt = (time_t) t;
  localtime_r(&tt, &out);
}

// Smooth value noise on the shared clock: interpolate the hash draws for
// adjacent buckets of `period` seconds. Every light computes the same curve,
// so a fire can flicker as ONE fire across the whole room rather than as 26
// unrelated flames. Pass a per-light salt where you want independent shimmer.
inline float sync_noise(double t, float period, uint32_t salt) {
  double u = t / (double) period;
  double fl = floor(u);
  uint32_t b = sync_u32(fl);
  float f = (float) (u - fl);
  float s = f * f * (3.0f - 2.0f * f);          // smoothstep, no corners
  float a = sync_random(b, salt);
  float c = sync_random(b + 1, salt);
  return a + (c - a) * s;
}

// (The room map - ROOM_MAP, light_xy(), room_index() - lives in basement_map.h.)

// ---------------------------------------------------------------------------
// Lockstep simulation, for effects that are genuinely stateful.
//
// A particle system (fireworks, comets, ripples) cannot be written as a pure
// function of the clock without changing how it looks. Lockstep keeps the
// original simulation and makes it REPRODUCIBLE instead: time is cut into
// fixed ticks counted from the shared clock, the effect advances one tick at
// a time, and every random draw is a hash of (slot, tick, draw number). Two
// lights therefore compute the identical sequence, bit for bit - same chip,
// same soft-float library - with nothing passing between them.
//
// Time is grouped into slots (one minute here). At each slot boundary every
// light restarts the simulation from tick 0, which is what lets a light that
// reboots, or an effect selected part-way through, rejoin: it re-simulates
// from the start of the current slot and catches up. The cost is one brief
// restart of the effect per slot.
//
// The catch-up is capped per frame BY TIME (ls_in_budget), not by tick count.
// It used to be 120 ticks a frame, and measured on the 48-LED ring that was
// 70-90 ms a frame for Fireworks Burst and 280 ms for Disco Mirror Ball -
// main-loop stalls long enough to log warnings and stutter. A time cap keeps
// every frame short whatever a tick costs; catch-up shows as a few seconds of
// smooth fast-forward. How many ticks fit in a frame has no effect on the
// simulation itself, so lights with different loads still converge on the
// identical state.
// ---------------------------------------------------------------------------
struct Lockstep {
  uint32_t slot = 0xFFFFFFFFu;
  uint32_t tick = 0;      // next tick of this slot still to simulate
  uint32_t calls = 0;     // draws made so far within the current tick
};

inline uint64_t sync_ms64() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < SYNC_MIN_VALID)
    return millis();
  return (uint64_t) tv.tv_sec * 1000ULL + (uint64_t) (tv.tv_usec / 1000);
}

// ---------------------------------------------------------------------------
// Room Speed and Intensity.
//
// Each room has two Home Assistant helpers, input_number.<area>_accent_speed
// and input_number.<area>_accent_intensity, 0-255 with 128 as "normal" - the
// WLED convention. accent_fx_controls.yaml imports them and calls the setters
// below. A light whose room has no helpers (or that has not heard from HA yet)
// stays at 128, which is exactly the look every effect had before these
// controls existed.
//
// Speed is a multiplier on the effect clock: 128 = 1x, each 64 steps doubles
// or halves it (0 = 1/4x, 64 = 1/2x, 192 = 2x, 255 = ~4x). Every light in a
// room gets the same integer from HA and computes the same multiplier, so the
// room stays in step; a change makes the effect jump to a new phase, which is
// accepted. Intensity is 128 -> 1.0 and 255 -> ~2.0; what it means is up to
// each effect (amplitude, contrast, foam, flicker depth).
// ---------------------------------------------------------------------------
inline float g_fx_speed = 1.0f;
inline float g_fx_intensity = 1.0f;

inline float fx_raw_clamp(float v) {
  if (isnan(v))
    return 128.0f;
  return v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v);
}
// Rounded to whole steps, so a helper set to 127.6 by some other client
// cannot give two lights a different speed.
inline void fx_set_speed(float raw) { g_fx_speed = exp2f((roundf(fx_raw_clamp(raw)) - 128.0f) / 64.0f); }
inline void fx_set_intensity(float raw) { g_fx_intensity = roundf(fx_raw_clamp(raw)) / 128.0f; }
inline float fx_speed() { return g_fx_speed; }
inline float fx_intensity() { return g_fx_intensity; }

// Building blocks for effects that honour intensity. Both are exactly 1 / 0 at
// normal, so an effect written with them looks exactly as it always has at 128.
//   fx_calm(k)   1 at normal and above, easing down to k at intensity 0:
//                multiply an effect's swing by it
//   fx_extra()   0 up to normal, rising to 1 at 255: scale anything added
//                for "more than normal" by it
inline float fx_over_normal(float intensity) {
  return intensity <= 1.0f ? 0.0f : (intensity >= 2.0f ? 1.0f : intensity - 1.0f);
}
inline float fx_calm(float at_zero) { return 1.0f - (1.0f - at_zero) * (1.0f - clamp01(g_fx_intensity)); }
inline float fx_extra() { return fx_over_normal(g_fx_intensity); }
// The two shapes most effects use. Both are exactly 1 at normal.
//   fx_swing()    scales an effect's movement - wave height, breathing,
//                 flicker depth: 0.3x at intensity 0, 1.5x at 255
//   fx_accents()  scales its occasional extras - foam, sparkles, gusts,
//                 flares: none at intensity 0, 2.5x at 255
inline float fx_swing() { return fx_calm(0.3f) * (1.0f + 0.5f * fx_extra()); }
inline float fx_accents() {
  float c = clamp01(g_fx_intensity);
  return c * c * (1.0f + 1.5f * fx_extra());
}

// Speed-scaled versions of the shared clock. Effects use these; only the
// effects that tell the real time (Clock, Hourly Chime) and the ones timed from
// the moment they are selected (Wake-Up Alarm) keep the unscaled clock. At 1x
// each returns exactly what the unscaled one does.
inline double fx_seconds() {
  double t = sync_seconds();
  return g_fx_speed == 1.0f ? t : t * (double) g_fx_speed;
}
inline float fx_t() { return sync_fold(fx_seconds()); }
inline uint64_t fx_ms64() {
  if (g_fx_speed == 1.0f)
    return sync_ms64();
  return (uint64_t) (fx_seconds() * 1000.0);
}
inline uint32_t fx_ms() { return g_fx_speed == 1.0f ? sync_ms() : (uint32_t) fx_ms64(); }

// Speed-scaled millis(), for the few effects that integrate their own motion
// from frame to frame instead of reading the shared clock. It accumulates real
// elapsed time times the current speed, so a speed change bends the motion
// rather than jumping it.
inline uint32_t fx_millis() {
  static uint32_t last = 0;
  static double acc = 0.0;
  uint32_t now = millis();
  if (last == 0) {
    last = now;
    acc = now;
  }
  acc += (double) (uint32_t) (now - last) * g_fx_speed;
  last = now;
  return (uint32_t) (uint64_t) acc;
}

// ---------------------------------------------------------------------------
// Stateless replacements for the frame-to-frame state some effects kept.
//
// An effect that integrates its own motion (t += dt * speed) or rolls
// random_uint32() each frame drifts apart from its neighbours, however good
// the clock: each light integrates from its own start and rolls its own dice.
// These give the same result as a pure function of the shared clock.
// ---------------------------------------------------------------------------

// Phase of a rate that wobbles, rate(T) = a + b * sin(w * T + ph), integrated
// in closed form. Returned in double: at epoch scale it must be folded with
// fx_wrap() before it goes anywhere near a float.
inline double fx_wobble(double T, double a, double b, double w, double ph) {
  return a * T - (b / w) * cos(w * T + ph);
}

// v folded into [0, period), in double, then cast.
inline float fx_wrap(double v, double period) {
  double f = fmod(v, period);
  if (f < 0)
    f += period;
  return (float) f;
}

// A decaying random "gust" - candle draughts, Pacifica foam - rebuilt each
// frame from the shared dice for the last `window` ticks instead of being
// carried in a static. Per tick: decay, then with chance_per_1000 add
// add_min + [0, add_span), capped. The same recipe the effects ran per frame;
// every light now draws the same gusts at the same moments.
inline float fx_gust(double T, double tick_s, uint32_t chance_per_1000, float add_min, float add_span,
                     float decay, float cap, uint32_t salt, int window = 48) {
  uint32_t now_b = sync_bucket(T, tick_s);
  float g = 0.0f;
  for (int k = window - 1; k >= 0; k--) {
    uint32_t b = now_b - (uint32_t) k;
    g *= decay;
    if (sync_hash(b, salt) % 1000 < chance_per_1000) {
      g += add_min + (sync_hash(b, salt + 1) % 1000) / 1000.0f * add_span;
      if (g > cap)
        g = cap;
    }
  }
  return g;
}

// Returns the tick the simulation should have reached. Restarts it when a new
// slot begins or the effect has just been (re)selected.
inline uint32_t ls_begin(Lockstep &ls, bool restart, uint32_t tick_ms, uint32_t slot_ticks) {
  uint64_t abs_tick = fx_ms64() / tick_ms;
  uint32_t slot = (uint32_t) (abs_tick / slot_ticks);
  uint32_t target = (uint32_t) (abs_tick % slot_ticks);
  // ls.tick > target + 1 only happens when the speed was turned down and the
  // clock stepped back; restart rather than stall until time catches up.
  if (restart || slot != ls.slot || ls.tick > target + 1) {
    ls.slot = slot;
    ls.tick = 0;
  }
  return target;
}

// True while this frame may still simulate another tick. Always allows the
// first one, so the simulation can never stall completely.
constexpr uint32_t LS_BUDGET_US = 15000;
inline bool ls_in_budget(uint32_t frame_start_us) {
  return (uint32_t) (micros() - frame_start_us) < LS_BUDGET_US;
}

// Wall-clock seconds of the tick being simulated. Inside a lockstep loop use
// this, never sync_seconds(): a replayed tick must see its own time.
inline double ls_seconds(const Lockstep &ls, uint32_t tick_ms, uint32_t slot_ticks) {
  return ((double) ls.slot * slot_ticks + ls.tick) * tick_ms / 1000.0;
}

// Drop-in for random_uint32() inside a lockstep loop.
inline uint32_t ls_rand(Lockstep &ls) {
  return sync_hash(ls.slot * 2654435761u + ls.tick, ls.calls++);
}
