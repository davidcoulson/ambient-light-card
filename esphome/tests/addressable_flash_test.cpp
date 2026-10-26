// ---------------------------------------------------------------------------
// Flash and floor test for the addressable effects (accent_light_addressable_
// effects.yaml and accent_light_ht2d_effects.yaml).
//
//   addressable_flash_test [--ring] [--mac MAC] [--leds N] [--seconds S] [--only SUBSTR]
//
// Without --ring it runs the strip effects, with it the home-theater ring ones.
//
// Every addressable_lambda effect, as the YAML has it (addressable_cases.py),
// is driven at its own update_interval for 30 minutes on a fake strip, and the
// luminance of every pixel, of every segment of the strip and of the whole
// strip is counted the WCAG 2.3.1 way, as halloween_test.cpp counts a bulb: a
// transition is a swing of >= 10 % of full white against the running extreme,
// a flash is a pair of opposing transitions, and a run of same-direction
// changes inside a second counts once. At most 2 flashes in any second, at
// each of the segment and strip scales. A single pixel is counted too (px/s*)
// but only reported: one LED sweeping past a travelling wave is not a flash of
// the light. Luminance is value^2.8 (ESPHome's gamma), so the
// ring effects that pre-compensate for it are counted by the light they make.
// ---------------------------------------------------------------------------
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <sys/time.h>

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
static uint32_t micros() { return 0; }   // a frozen budget lets Lockstep catch up fully

#include "light_effects_helpers.h"

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  Color() = default;
  Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) : r(r), g(g), b(b), w(w) {}
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
struct FakeValues {
  float get_brightness() const { return 1.0f; }
  void as_rgb(float *r, float *g, float *b) const { *r = 1.0f; *g = 0.6f; *b = 0.2f; }   // the colour picked in HA
};
struct FakeLight {
  FakeValues current_values;
  float get_gamma_correct() const { return 2.8f; }
};
static FakeLight accent_light_obj;
static FakeLight *accent_light = &accent_light_obj;   // ESPHome's light is a pointer

static const char *FX_TEST_MAC = "e018f8";   // ring A of home_theater_map.h
static float FX_TEST_OFFSET = 0.0f;

struct FxCase {
  const char *name;
  int interval_ms;
  bool ring;   // from accent_light_ht2d_effects.yaml: placed by --mac on the home-theater map
  void (*run)(FxIt &, Color, bool);
};

#include "addressable_fx.h"
#include "fx2d_render.h"
#include "home_theater_map.h"
#include "addressable_extracted.h"

// ---- WCAG counting, as halloween_test.cpp ---------------------------------------

struct Trans {
  double t;
  int d;
};
static int max_in(const std::vector<Trans> &tr) {
  int best = 0;
  size_t j = 0;
  for (size_t i = 0; i < tr.size(); i++) {
    while (tr[i].t - tr[j].t >= 1.0) j++;
    best = std::max(best, (int) (i - j + 1));
  }
  return best / 2;
}
struct LumTrace {
  bool have = false;
  double ext = 0;
  int trend = 0;
  std::vector<Trans> tr;
  void add(double t, double y) {
    if (!have) { ext = y; have = true; return; }
    if (trend == 1 && y > ext) ext = y;
    else if (trend == -1 && y < ext) ext = y;
    bool up = y - ext >= 0.1, down = ext - y >= 0.1;
    if ((trend <= 0 && up) || (trend >= 0 && down)) {
      tr.push_back({t, up ? 1 : -1});
      trend = up ? 1 : -1;
      ext = y;
    }
  }
  int flashes() const { return max_in(tr); }
  // The transitions of the busiest second, for --why.
  void print_worst() const {
    size_t bj = 0, bi = 0;
    for (size_t i = 0, j = 0; i < tr.size(); i++) {
      while (tr[i].t - tr[j].t >= 1.0) j++;
      if (i - j + 1 > bi - bj + 1 || (i == 0)) { bj = j; bi = i; }
    }
    for (size_t k = bj; k <= bi && k < tr.size(); k++) printf("    t+%.3f %s\n", tr[k].t - tr[bj].t, tr[k].d > 0 ? "up" : "down");
  }
};
static double lin(uint8_t v) { return pow(v / 255.0, 2.8); }
static double lum(const Color &c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }

int main(int argc, char **argv) {
  int leds = 36;
  double seconds = 1800.0;
  const char *only = nullptr;
  bool ring = false, why = false;
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--mac") && a + 1 < argc) FX_TEST_MAC = argv[++a];
    else if (!strcmp(argv[a], "--leds") && a + 1 < argc) leds = atoi(argv[++a]);
    else if (!strcmp(argv[a], "--seconds") && a + 1 < argc) seconds = atof(argv[++a]);
    else if (!strcmp(argv[a], "--why")) why = true;
    else if (!strcmp(argv[a], "--ring")) { ring = true; leds = 13; }
    else if (!strcmp(argv[a], "--only") && a + 1 < argc) only = argv[++a];
  }
  // A segment is about six LEDs: 6 of a 36 ring, 8 of a 48, 2 of the 13-LED ring.
  const int segs = std::max(1, leds / 6);
  const int seg_len = (leds + segs - 1) / segs;
  int failures = 0;
  printf("%-30s %5s %5s %5s %5s %8s %8s\n", "effect" , "px/s*", "seg/s", "all/s", "ms", "min lum", "mean lum");
  for (const FxCase &fx : fx_cases) {
    if (fx.ring != ring || (only && !strstr(fx.name, only))) continue;
    g_now = 1.8e9 + 1234.567;
    FxIt it(leds);
    std::vector<LumTrace> px(leds), seg(segs);
    LumTrace all;
    double min_lum = 1e9, sum = 0;
    long frames = (long) (seconds * 1000.0 / fx.interval_ms);
    for (long k = 0; k < frames; k++) {
      fx.run(it, Color(), k == 0);
      double sl[64] = {0}, total = 0;   // segments: 64 is room for a 380-LED strip
      for (int i = 0; i < leds; i++) {
        double y = lum(it.px[i]);
        min_lum = std::min(min_lum, y);
        px[i].add(g_now, y);
        sl[i / seg_len] += y;
        total += y;
      }
      for (int s = 0; s < segs; s++) {
        int len = std::min(seg_len, leds - s * seg_len);
        seg[s].add(g_now, sl[s] / len);
      }
      all.add(g_now, total / leds);
      sum += total / leds;
      g_now += fx.interval_ms / 1000.0;
    }
    int wp = 0, ws = 0;
    for (auto &p : px) wp = std::max(wp, p.flashes());
    for (auto &s : seg) ws = std::max(ws, s.flashes());
    int wa = all.flashes();
    printf("%-30s %5d %5d %5d %5d %8.4f %8.4f\n", fx.name, wp, ws, wa, fx.interval_ms, min_lum, sum / frames);
    if (why) {
      size_t best = 0;
      for (size_t k = 0; k < seg.size(); k++) if (seg[k].flashes() > seg[best].flashes()) best = k;
      printf("  busiest second of segment %zu:\n", best);
      seg[best].print_worst();
    }
    if (ws > 2 || wa > 2) {
      failures++;
      printf("FAIL %s: %d flashes per second in a segment, %d in the strip; limit 2\n", fx.name, ws, wa);
    }
  }
  if (failures) {
    printf("%d effect(s) over the flash limit\n", failures);
    return 1;
  }
  printf("all addressable effects within 3 flashes per second\n");
  return 0;
}
