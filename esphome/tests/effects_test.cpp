// ---------------------------------------------------------------------------
// Host tests for the effect library (common/light_effects_helpers.h and the
// effect lambdas in the YAML files).
//
// Build and run with run_effects_tests.sh, which extracts the effects under
// test into effects_extracted.h (extract_lambda.py) and compiles them here
// against a fake LED buffer, fake PWM outputs and a settable wall clock. The
// lambdas are the YAML text as it is; nothing is copied by hand.
//
// Checks:
//   - Fireworks Burst sparks start inside the intended speed and hue spread;
//   - Pacifica (all four ring versions) draws the same frame for the same
//     instant whatever frames the light drew before it;
//   - Polyrhythm's threes and fours land together on every downbeat, at
//     epoch-scale time;
//   - out_rgb() holds an RGBWW light's white channels off once
//     g_fx_whites_off is set, and leaves outputs alone when it is not.
// ---------------------------------------------------------------------------

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>   // std::min, std::max in the effects
#include <string>
#include <vector>
#include <sys/time.h>

// ---- what ESPHome gives a lambda ----------------------------------------------

// The wall clock, settable. The helpers read gettimeofday(); route it here.
static double g_now = 1.8e9;
static int fake_gettimeofday(struct timeval *tv) {
  tv->tv_sec = (time_t) floor(g_now);
  tv->tv_usec = (suseconds_t) llround((g_now - floor(g_now)) * 1e6);
  if (tv->tv_usec >= 1000000) {
    tv->tv_sec++;
    tv->tv_usec -= 1000000;
  }
  return 0;
}
#define gettimeofday(tv, tz) fake_gettimeofday(tv)
static uint32_t millis() { return (uint32_t) (uint64_t) (g_now * 1000.0); }
// Lockstep budgets frames by micros(); a frozen clock lets every frame catch up fully.
static uint32_t micros() { return 0; }

#include "light_effects_helpers.h"

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  Color() = default;
  Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) : r(r), g(g), b(b), w(w) {}
  bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && w == o.w; }
};
struct FxPixel {
  Color *c;
  FxPixel &operator=(const Color &v) { *c = v; return *this; }
  Color get() const { return *c; }
};
struct FxIt {
  std::vector<Color> px;
  explicit FxIt(int n) : px(n) {}
  int size() const { return (int) px.size(); }
  FxPixel operator[](int i) { return FxPixel{&px[i]}; }
};

struct FakeOutput {
  float level = -1.0f;   // -1: never written
  void set_level(float v) { level = v; }
};
struct FakeValues {
  float brightness = 1.0f;
  float get_brightness() const { return brightness; }
};
struct FakeLight {
  FakeValues current_values;
  float get_gamma_correct() const { return 2.8f; }
};
static FakeLight accent_light;
static FakeOutput red_output, green_output, blue_output, warm_output, cold_output;
#define id(x) (&(x))

// Test hooks the extractor swaps in.
static const char *FX_TEST_MAC = "fxtest";
static float FX_TEST_OFFSET = 0.0f;
struct Probe {
  std::string target;
  float value;
};
static std::vector<Probe> g_probes;
template<typename T> static T fx_probe(const char *target, T v) {
  g_probes.push_back({target, (float) v});
  return v;
}

#include "effects_extracted.h"

// ---- harness ------------------------------------------------------------------

static int failures = 0;
#define CHECK(cond, ...)                                    \
  do {                                                      \
    if (!(cond)) {                                          \
      failures++;                                           \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);          \
      printf(__VA_ARGS__);                                  \
      printf("\n");                                         \
    }                                                       \
  } while (0)

// ---- Fireworks Burst ------------------------------------------------------------

// Every spark is launched at (-100..99) * 0.6 LEDs/s and at the rocket's hue
// +-0.1. The draws used to be unsigned, so (draw % 200) - 100 wrapped to ~4.3e9
// for half of them: sparks flew off at billions of LEDs a second and their hue
// collapsed to red or cyan.
static void test_fireworks() {
  const int before = failures;
  g_now = 1.8e9;
  FxIt ring(36);
  g_probes.clear();
  bool first = true;
  // One full lockstep slot (60 s) from its start, frame by frame.
  const double start = floor(g_now / 60.0) * 60.0;
  for (int f = 0; f < 3000; f++) {
    g_now = start + f * 0.020;
    fireworks::run(ring, Color(), first);
    first = false;
  }
  int vels = 0, hues = 0;
  for (const Probe &p : g_probes) {
    if (p.target == "p_vel[i]") {
      vels++;
      CHECK(p.value >= -60.0f && p.value <= 60.0f, "spark velocity %g LEDs/s outside +-60", p.value);
    } else if (p.target == "p_hue[i]") {
      hues++;
      // The rocket's hue is 0..1, so the spark's is -0.1..1.1.
      CHECK(p.value >= -0.1f && p.value <= 1.1f, "spark hue %g outside the rocket's hue +-0.1", p.value);
    }
  }
  CHECK(vels > 50, "only %d sparks launched in a minute; the test is not exercising bursts", vels);
  CHECK(vels == hues, "%d velocities but %d hues", vels, hues);
  if (failures == before)
    printf("fireworks: %d sparks, all inside the intended speed and hue spread\n", vels);
}

// ---- Pacifica -------------------------------------------------------------------

// The frame at an instant must not depend on which frames the light drew before
// it. Whitecaps used to be added only on a light's FIRST frame in each window,
// so a light that had just drawn one showed no foam where another showed it.
typedef void (*RingFx)(FxIt &, Color, bool);
static void check_pacifica(const char *name, RingFx a, RingFx b) {
  int diffs = 0;
  for (int k = 0; k < 400; k++) {
    const double t = 1.8e9 + k * 0.37;
    FxIt ra(36), rb(36), fresh(36);
    // Light A draws 10 ms earlier (same whitecap window, almost always), then at t.
    g_now = t - 0.010;
    a(ra, Color(), false);
    g_now = t;
    a(ra, Color(), false);
    // Light B draws only at t.
    b(rb, Color(), false);
    // And B again at t: a repeat of the same instant must also match.
    b(fresh, Color(), false);
    if (!(ra.px == rb.px) || !(fresh.px == rb.px))
      diffs++;
  }
  CHECK(diffs == 0, "%s: %d of 400 instants drew a different frame depending on the frame before", name, diffs);
  if (diffs == 0)
    printf("pacifica: %s draws the same frame for the same instant\n", name);
}

// ---- Polyrhythm -----------------------------------------------------------------

// Threes and fours over a 2.4 s bar: both voices strike at the top of every bar
// (and the threes every 0.8 s, the fours every 0.6 s). At epoch scale a float
// beat put each voice tens of seconds off, so they never met.
static float poly_level(float offset, double t) {
  FX_TEST_OFFSET = offset;
  g_now = t;
  red_output.level = green_output.level = blue_output.level = -1.0f;
  polyrhythm::run(false);
  // Threes are red-led, fours blue-led; out_rgb writes the level to that channel.
  return offset < 0.5f ? red_output.level : blue_output.level;
}
static void test_polyrhythm() {
  int bad = 0;
  for (int k = 0; k < 200; k++) {
    // Bar starts at epoch scale: n * 2.4 s with n a multiple of 5 is a whole second.
    const double bar = (750000000.0 + 5.0 * k * 7919.0) * 2.4;
    for (int voice = 0; voice < 2; voice++) {
      const float off = voice == 0 ? 0.25f : 0.75f;
      const double beat = voice == 0 ? 0.8 : 0.6;
      const int beats = voice == 0 ? 3 : 4;
      for (int j = 0; j < beats; j++) {
        // Just after the strike: near full. Just before the next: down near the floor.
        float on = poly_level(off, bar + j * beat + 0.002);
        float off_beat = poly_level(off, bar + j * beat + beat * 0.9);
        if (on < 0.9f || off_beat > 0.1f) {
          if (bad++ < 5)
            printf("  %s beat %d of bar %.1f: %.3f just after, %.3f just before the next\n",
                   voice == 0 ? "threes" : "fours", j, bar, on, off_beat);
        }
      }
    }
  }
  CHECK(bad == 0, "polyrhythm: %d beats off the bar grid", bad);
  if (bad == 0)
    printf("polyrhythm: threes and fours strike on the bar grid at epoch scale\n");
}

// ---- out_rgb and the white channels ------------------------------------------------

static void test_out_rgb_whites() {
  const int before = failures;
  accent_light.current_values.brightness = 1.0f;
  // An RGB light: no hook, and out_rgb touches only the three outputs it is given.
  g_fx_whites_off = nullptr;
  warm_output.level = cold_output.level = 0.7f;
  out_rgb(id(accent_light), id(red_output), id(green_output), id(blue_output), 0.2f, 0.4f, 0.6f);
  CHECK(warm_output.level == 0.7f && cold_output.level == 0.7f, "out_rgb wrote outputs it was not given");
  CHECK(red_output.level == 0.2f && green_output.level == 0.4f && blue_output.level == 0.6f, "out_rgb levels wrong");

  // An RGBWW light: white_effects.yaml sets the hook at boot, so an RGB frame
  // clears whatever ESPHome's own state write left on the whites.
  g_fx_whites_off = []() {
    id(warm_output)->set_level(0.0f);
    id(cold_output)->set_level(0.0f);
  };
  warm_output.level = cold_output.level = 0.7f;
  out_rgb(id(accent_light), id(red_output), id(green_output), id(blue_output), 0.2f, 0.4f, 0.6f);
  CHECK(warm_output.level == 0.0f && cold_output.level == 0.0f, "whites left at %g / %g under an RGB frame",
        warm_output.level, cold_output.level);
  CHECK(red_output.level == 0.2f && green_output.level == 0.4f && blue_output.level == 0.6f, "out_rgb levels wrong");
  g_fx_whites_off = nullptr;
  if (failures == before)
    printf("out_rgb: whites held off on an RGBWW light, untouched on an RGB one\n");
}

int main() {
  test_fireworks();
  check_pacifica("Pacifica", pacifica_a::run, pacifica_b::run);
  check_pacifica("Pacifica (Calm Lagoon)", lagoon_a::run, lagoon_b::run);
  check_pacifica("Pacifica (Storm)", storm_a::run, storm_b::run);
  check_pacifica("Pacifica (Deep Current)", deep_a::run, deep_b::run);
  test_polyrhythm();
  test_out_rgb_whites();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all effect checks passed\n");
  return 0;
}
