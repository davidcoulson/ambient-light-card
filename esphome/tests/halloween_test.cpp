// ---------------------------------------------------------------------------
// Host tests for the Halloween effects (common/halloween_fx.h).
//
// Build and run with run_halloween_tests.sh. Checks, on every light of the
// example yard map:
//   - the same time and the same light always give the same frame;
//   - the floor holds: no channel under the effect's floor, and never less
//     than 3 % of full warm white's luminance;
//   - flashes stay at or under 1 per second per light and per group;
//   - a light that is not on the map still runs, as a driveway light;
//   - Speed can slow the effect clock but not speed it up;
//   - hw_out() applies the brightness slider like out_rgb();
//   - the calendar, sunset and weather plumbing behave (evening rollover,
//     calm fallbacks on NaN or a lost API);
// and the flash and floor checks also run at lights-on (daylight, for the
// effects that follow the clock) and in a simulated storm for the effects
// that read the weather.
// Local time is pinned to US Eastern so the C++ and the reference agree.
// `--dump` prints frames on a time grid for halloween_parity.js, which checks
// the C++ against the reference model the preview page runs.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <vector>
#include <algorithm>

// What ESPHome provides to lambdas; the helpers only reach these when the
// wall clock is not set, which the tests never rely on.
static uint32_t millis() { return 0; }
static uint32_t micros() { return 0; }

#include "front_yard_map.h"

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

typedef HwColor (*EffectFn)(double, const HwSelf &);
static HwColor jack_fn(double T, const HwSelf &b) { return hw_jack(T, b); }
// The place the reference model uses (tests/halloween_reference.js SUN): New York City.
static const double LAT = 40.7, LON = -74.0;
static HwColor witching_fn(double T, const HwSelf &b) { return hw_witching(T, b, LAT, LON); }

// A simulated storm: 30 mph gusting 45, a downpour, at the dew point.
static const HwWeather STORM = {true, 30.0f, 45.0f, 5.0f, 225.0f, 0.6f, 38.0f, 37.0f};

struct Effect {
  const char *key, *name;
  EffectFn fn;
  HwColor floor;
};
static const Effect EFFECTS[] = {
    {"jack", "Jack-o'-Lantern Row", jack_fn, HW_EMBER},
    {"lightning", "Lightning Over the House", hw_lightning, HW_STORM_FLOOR},
    {"cauldron", "Witch's Cauldron", hw_cauldron, HW_BREW_FLOOR},
    {"moon", "Blood Moon", hw_moon, HW_MOON_FLOOR},
    {"heart", "Heartbeat of the House", hw_heart, HW_BLOOD_FLOOR},
    {"swap", "Trick-or-Treat Swap", hw_swap, HW_TREAT_FLOOR},
    {"season", "Turning of the Season", hw_season, HW_EMBER},
    {"witching", "Witching Hour", witching_fn, HW_NIGHT_FLOOR},
    {"mist", "Graveyard Mist", hw_mist, HW_NIGHT_FLOOR},
    {"windcandles", "Wind-Blown Candles", hw_windcandles, HW_EMBER},
};
static bool reads_weather(const Effect &fx) {
  return !strcmp(fx.key, "windcandles") || !strcmp(fx.key, "mist") || !strcmp(fx.key, "season");
}
static bool follows_clock(const Effect &fx) {
  return !strcmp(fx.key, "season") || !strcmp(fx.key, "witching") || !strcmp(fx.key, "mist");
}

// Light output of each channel relative to warm white at full, as linear sRGB
// - a typical RGBCW bulb. Same table as tests/halloween_reference.js.
static const float LED[5][3] = {
    {0.70f, 0.00f, 0.00f}, {0.02f, 0.42f, 0.06f}, {0.015f, 0.03f, 0.30f}, {1.00f, 0.72f, 0.42f}, {0.74f, 0.77f, 0.80f}};
static const double Y_WW = 0.2126 + 0.7152 * 0.72 + 0.0722 * 0.42;

static void to_linear(const HwColor &c, double lin[3]) {
  const float ch[5] = {c.r, c.g, c.b, c.ww, c.cw};
  lin[0] = lin[1] = lin[2] = 0;
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 3; j++) lin[j] += ch[i] * LED[i][j];
}
static double lum_rel(const double lin[3]) { return (0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]) / Y_WW; }
static bool sat_red(const double lin[3]) {
  double s = lin[0] + lin[1] + lin[2];
  return s > 0.02 && lin[0] / s >= 0.8;
}

// WCAG 2.3.1 counting, as in halloween_reference.js: a transition is a swing
// of >= 10 % of warm-white luminance against the running extreme; a flash is
// a pair of opposing transitions. A group merges its lights' transitions in
// time order and counts a run of same-direction changes as one sweep.
struct Trans {
  double t;
  int d;
  bool red;
};
static int max_in(const std::vector<Trans> &tr, bool red_only) {
  std::vector<double> ts;
  for (auto &x : tr)
    if (!red_only || x.red) ts.push_back(x.t);
  int best = 0;
  size_t j = 0;
  for (size_t i = 0; i < ts.size(); i++) {
    while (ts[i] - ts[j] >= 1.0) j++;
    best = std::max(best, (int) (i - j + 1));
  }
  return best;
}
struct FlashResult {
  int bulb = 0, group = 0, red = 0;
  double min_lum = 1e9;
  bool floor_ok = true;
};
static FlashResult flash_check(const Effect &fx, const std::vector<HwSelf> &lights, double seconds, double dt, double t0) {
  FlashResult res;
  std::vector<Trans> groups[4];   // S, G, D, and unmapped lights on their own
  long n = (long) (seconds / dt + 0.5);
  for (const HwSelf &b : lights) {
    std::vector<Trans> tr;
    bool have = false, ext_red = false;
    double ext = 0;
    int trend = 0;
    for (long k = 0; k < n; k++) {
      double t = t0 + k * dt;
      HwColor c = fx.fn(t, b);
      if (c.r < fx.floor.r || c.g < fx.floor.g || c.b < fx.floor.b || c.ww < fx.floor.ww || c.cw < fx.floor.cw)
        res.floor_ok = false;
      double lin[3];
      to_linear(c, lin);
      double y = lum_rel(lin);
      bool r = sat_red(lin);
      res.min_lum = std::min(res.min_lum, y);
      if (!have) { ext = y; ext_red = r; have = true; continue; }
      if (trend == 1 && y > ext) { ext = y; ext_red = r; }
      else if (trend == -1 && y < ext) { ext = y; ext_red = r; }
      bool up = y - ext >= 0.1, down = ext - y >= 0.1;
      if ((trend <= 0 && up) || (trend >= 0 && down)) {
        tr.push_back({t, up ? 1 : -1, r || ext_red});
        trend = up ? 1 : -1;
        ext = y;
        ext_red = r;
      }
    }
    res.bulb = std::max(res.bulb, max_in(tr, false) / 2);
    res.red = std::max(res.red, max_in(tr, true) / 2);
    int gi = b.mapped ? b.group : 3;
    groups[gi].insert(groups[gi].end(), tr.begin(), tr.end());
  }
  for (auto &g : groups) {
    std::sort(g.begin(), g.end(), [](const Trans &a, const Trans &b) { return a.t < b.t; });
    std::vector<Trans> o;
    for (auto &x : g) {
      if (!o.empty() && o.back().d == x.d && x.t - o.back().t < 1.0) {
        if (x.red) o.back().red = true;
        continue;
      }
      o.push_back(x);
    }
    res.group = std::max(res.group, max_in(o, false) / 2);
    res.red = std::max(res.red, max_in(o, true) / 2);
  }
  return res;
}

static bool same(const HwColor &a, const HwColor &b) { return memcmp(&a, &b, sizeof(HwColor)) == 0; }

static std::vector<HwSelf> yard() {
  std::vector<HwSelf> v;
  for (int i = 0; i < YARD_N; i++) v.push_back(hw_me(YARD_MAP[i].name, "000000"));
  return v;
}

// An October evening in 2026, 20:00 EDT, and an evening two weeks earlier in daylight (17:45 EDT,
// before sunset, when the lights would already be on).
static const double T0 = 1793491200.0;
static const double T_LIGHTS_ON = 1792100700.0;

static void test_identity() {
  auto lights = yard();
  CHECK(lights[0].group == YARD_S && lights[0].k == 0 && lights[0].n == 2, "S1 row");
  CHECK(lights[3].group == YARD_G && lights[3].k == 1 && lights[3].n == 3, "G2 is the second of three driveway lights");
  CHECK(lights[6].group == YARD_D && lights[6].k == 1 && lights[6].n == 2, "D2 row");
  CHECK(hw_me("g2", "abcdef").i == 3, "names match case-insensitively");
  HwSelf lone = hw_me("", "a1b2c3"), lone2 = hw_me("nope", "a1b2c3"), other = hw_me("", "ffffff");
  CHECK(!lone.mapped && lone.group == YARD_G && lone.n == 1, "unmapped light plays a lone driveway light");
  CHECK(lone.off == lone2.off && lone.i == lone2.i, "unmapped identity comes from the MAC alone");
  CHECK(lone.off != other.off, "two unmapped lights are spread apart");
}

static void test_deterministic() {
  auto lights = yard();
  for (const Effect &fx : EFFECTS) {
    for (int i = 0; i < YARD_N; i++) {
      HwSelf again = hw_me(YARD_MAP[i].name, "123456");   // MAC is irrelevant on the map
      for (int k = 0; k < 2000; k++) {
        double t = T0 + k * 0.731;
        HwColor a = fx.fn(t, lights[i]), b = fx.fn(t, lights[i]), c = fx.fn(t, again);
        if (!same(a, b) || !same(a, c)) {
          CHECK(false, "%s %s differs at the same time and identity (t+%.3f)", fx.key, YARD_MAP[i].name, k * 0.731);
          break;
        }
      }
    }
  }
}

static void check_window(const Effect &fx, const std::vector<HwSelf> &lights, double t0, const char *when) {
  FlashResult r = flash_check(fx, lights, 1800.0, 0.01, t0);
  printf("%-26s %-18s %6d %6d %6d %8.3f\n", fx.name, when, r.bulb, r.group, r.red, r.min_lum);
  CHECK(r.floor_ok, "%s (%s): a channel fell under the effect's floor", fx.key, when);
  CHECK(r.min_lum >= 0.03, "%s (%s): luminance %.3f is under the 3 %% floor", fx.key, when, r.min_lum);
  CHECK(r.bulb <= 1 && r.group <= 1 && r.red <= 1, "%s (%s): %d/%d/%d flashes per second (bulb/group/red), target 1",
        fx.key, when, r.bulb, r.group, r.red);
}

static void test_floor_and_flashes() {
  auto lights = yard();
  lights.push_back(hw_me("", "a1b2c3"));          // and an unmapped light
  printf("%-26s %-18s %6s %6s %6s %8s\n", "effect (30 min, 10 ms)", "when", "bulb/s", "group/s", "red/s", "min lum");
  for (const Effect &fx : EFFECTS) {
    g_hw_wx = HW_WX_CALM;
    check_window(fx, lights, T0, "Oct 31, 8 pm");
    if (follows_clock(fx)) check_window(fx, lights, T_LIGHTS_ON, "Oct 15, 5:45 pm");
    if (reads_weather(fx)) {
      g_hw_wx = STORM;
      check_window(fx, lights, T0, "Oct 31, storm");
      g_hw_wx = HW_WX_CALM;
    }
  }
}

static void test_calendar_and_sun() {
  // 00:30 on Oct 16 still belongs to the evening of Oct 15
  HwEvening a = hw_evening(T_LIGHTS_ON), b = hw_evening(T_LIGHTS_ON + 6.75 * 3600);
  CHECK(a.oct == 15 && b.oct == 15 && a.night == b.night, "an evening keeps its date past midnight");
  CHECK(fabs(b.hour - 24.5) < 1e-6, "00:30 is hour 24.5 of the evening (got %.4f)", b.hour);
  CHECK(hw_evening(T0).oct == 31 && hw_evening(T0 - 86400.0 * 40).oct == 1, "October dates, clamped outside October");
  // Sunset in New York City (almanac): 6:38 pm on Oct 1, 5:53 pm on Oct 31, EDT
  double s1 = hw_sunset_hour(T0 - 30 * 86400.0, LAT, LON), s31 = hw_sunset_hour(T0, LAT, LON);
  CHECK(fabs(s1 - (18 + 38 / 60.0)) < 2 / 60.0, "Oct 1 sunset %.3f h, expected ~18.63", s1);
  CHECK(fabs(s31 - (17 + 53 / 60.0)) < 2 / 60.0, "Oct 31 sunset %.3f h, expected ~17.88", s31);
}

static void test_weather_plumbing() {
  g_hw_wx_in = HwWeatherIn();
  g_hw_wx = HW_WX_CALM;
  hw_wx_tick(true, 1.0f);
  CHECK(!g_hw_wx.on && g_hw_wx.wind_mph == 0.0f, "no readings yet: calm, weather off");
  g_hw_wx_in.wind = 12.0f; g_hw_wx_in.dir = 200.0f; g_hw_wx_in.temp = 40.0f;
  hw_wx_tick(true, 1.0f);
  CHECK(g_hw_wx.on && g_hw_wx.wind_mph == 12.0f && g_hw_wx.dir_deg == 200.0f, "wind applies at once");
  CHECK(g_hw_wx.temp_f < 50.0f && g_hw_wx.temp_f > 47.0f, "temperature eases in (got %.2f)", g_hw_wx.temp_f);
  for (int k = 0; k < 120; k++) hw_wx_tick(true, 1.0f);
  CHECK(fabsf(g_hw_wx.temp_f - 40.0f) < 0.01f, "and arrives");
  g_hw_wx_in.wind = NAN;
  hw_wx_tick(true, 1.0f);
  CHECK(!g_hw_wx.on && g_hw_wx.wind_mph == 0.0f, "an unknown wind reading falls back to calm");
  g_hw_wx_in.wind = 12.0f;
  hw_wx_tick(false, 1.0f);
  CHECK(!g_hw_wx.on && g_hw_wx.wind_mph == 0.0f && g_hw_wx.dir_deg == 270.0f, "a lost API falls back to calm");
  g_hw_wx_in = HwWeatherIn();
  g_hw_wx = HW_WX_CALM;
}

static void test_speed() {
  fx_set_speed(255.0f);   // ~4x on other effects
  double a = sync_seconds(), h = hw_seconds(), b = sync_seconds();
  CHECK(h >= a && h <= b, "Speed above normal must not speed the Halloween clock up");
  fx_set_speed(64.0f);    // 1/2x
  a = sync_seconds();
  h = hw_seconds();
  CHECK(h > a * 0.49 && h < a * 0.51, "Speed below normal slows it");
  fx_set_speed(128.0f);
}

struct FakeValues {
  float b;
  float get_brightness() const { return b; }
};
struct FakeLight {
  FakeValues current_values{1.0f};
  float get_gamma_correct() const { return 2.8f; }
};
struct FakeOut {
  float level = -1;
  void set_level(float v) { level = v; }
};
static void test_out() {
  FakeLight light;
  FakeOut r, g, b, w, c;
  HwColor col = {0.5f, 0.25f, 0.1f, 0.8f, 0.0f};
  hw_out(&light, &r, &g, &b, &w, &c, col);
  CHECK(r.level == 0.5f && w.level == 0.8f && c.level == 0.0f, "100 %% brightness writes the frame as is");
  light.current_values.b = 0.5f;
  hw_out(&light, &r, &g, &b, &w, &c, col);
  float s = powf(0.5f, 2.8f);
  CHECK(fabsf(r.level - 0.5f * s) < 1e-6f && fabsf(w.level - 0.8f * s) < 1e-6f, "the slider scales through gamma");
  HwColor capped = hw_finish({1, 1, 1, 1, 1}, HW_EMBER);
  float sum = capped.r + capped.g + capped.b + capped.ww + capped.cw;
  CHECK(sum <= HW_CAP + 1e-5f, "the channel sum is capped at %.2f (got %.3f)", HW_CAP, sum);
  HwColor dark = hw_finish({0, 0, 0, 0, 0}, HW_EMBER);
  CHECK(same(dark, HW_EMBER), "an all-off frame comes out as the floor");
}

static void dump() {
  auto lights = yard();
  printf("effect,light,t,r,g,b,ww,cw\n");
  for (int storm = 0; storm < 2; storm++)
    for (const Effect &fx : EFFECTS) {
      if (storm && !reads_weather(fx)) continue;
      g_hw_wx = storm ? STORM : HW_WX_CALM;
      double base = follows_clock(fx) ? T_LIGHTS_ON : T0;
      for (int i = 0; i < YARD_N; i++)
        for (int k = 0; k < 4000; k++) {
          double t = base + k * 1.3719;
          HwColor c = fx.fn(t, lights[i]);
          printf("%s%s,%s,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f\n", fx.key, storm ? "@storm" : "", YARD_MAP[i].name, t, c.r, c.g,
                 c.b, c.ww, c.cw);
        }
    }
  g_hw_wx = HW_WX_CALM;
}

int main(int argc, char **argv) {
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);   // US Eastern, as halloween_parity.js runs
  tzset();
  if (argc > 1 && strcmp(argv[1], "--dump") == 0) {
    dump();
    return 0;
  }
  test_identity();
  test_deterministic();
  test_speed();
  test_out();
  test_calendar_and_sun();
  test_weather_plumbing();
  test_floor_and_flashes();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all Halloween checks passed\n");
  return 0;
}
