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
//   - Fireworks Burst sparks start inside the intended speed and hue spread.
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

int main() {
  test_fireworks();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all effect checks passed\n");
  return 0;
}
