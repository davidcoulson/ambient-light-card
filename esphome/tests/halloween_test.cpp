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
//   - hw_out() applies the brightness slider like out_rgb();
//   - the calendar, sunset and weather plumbing behave (evening rollover,
//     calm fallbacks on NaN or a lost API);
//   - the daylight lift only raises channels, stays inside the power cap,
//     and follows the sun or measured light;
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
static const HwWeather STORM = {true, 30.0f, 45.0f, 5.0f, 225.0f, 0.6f, 38.0f, 37.0f, NAN};

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
    hw_wx_set(HW_WX_CALM);
    check_window(fx, lights, T0, "Oct 31, 8 pm");
    if (follows_clock(fx)) check_window(fx, lights, T_LIGHTS_ON, "Oct 15, 5:45 pm");
    if (reads_weather(fx)) {
      hw_wx_set(STORM);
      check_window(fx, lights, T0, "Oct 31, storm");
      hw_wx_set(HW_WX_CALM);
    }
    g_hw_amb = 1.0f;                               // full daylight lift
    check_window(fx, lights, follows_clock(fx) ? T_LIGHTS_ON : T0, "daylight lift");
    g_hw_amb = 0.0f;
  }
}

static void test_lift() {
  auto lights = yard();
  for (const Effect &fx : EFFECTS)
    for (int k = 0; k < 3000; k++) {
      double t = T0 + k * 0.613;
      const HwSelf &b = lights[k % YARD_N];
      g_hw_amb = 0.0f;
      HwColor night = fx.fn(t, b);
      g_hw_amb = 1.0f;
      HwColor day = fx.fn(t, b);
      g_hw_amb = 0.0f;
      float sum = day.r + day.g + day.b + day.ww + day.cw;
      auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
      bool ok = day.r >= night.r - 1e-6f && day.g >= night.g - 1e-6f && day.b >= night.b - 1e-6f && day.ww >= night.ww - 1e-6f &&
                day.cw >= night.cw - 1e-6f && sum <= HW_CAP + 1e-4f && unit(day.r) && unit(day.g) && unit(day.b) &&
                unit(day.ww) && unit(day.cw);
      if (!ok) {
        CHECK(false, "%s: the lift must only raise channels, keep each in [0, 1] and stay inside the cap (t+%.3f)", fx.key,
              t - T0);
        break;
      }
    }
  CHECK(hw_ambient_from_sun(-10.0) == 0.0f && hw_ambient_from_sun(10.0) == 1.0f, "sun ambient: 0 in the dark, 1 in daylight");
  CHECK(hw_ambient_from_lux(1.0f) == 0.0f && hw_ambient_from_lux(1000.0f) == 1.0f, "lux ambient: 0 at 1 lx, 1 at 1000 lx");
  hw_wx_set(HW_WX_CALM);
  hw_frame(T0, LAT, LON);
  CHECK(g_hw_amb == 0.0f, "8 pm on Oct 31: no lift");
  hw_frame(T_LIGHTS_ON, LAT, LON);
  CHECK(g_hw_amb > 0.9f, "5:45 pm on Oct 15: full lift (got %.2f)", g_hw_amb);
  float a1 = hw_ambient_at(T_LIGHTS_ON + 3600.0, LAT, LON), a2 = hw_ambient_at(T_LIGHTS_ON + 3600.4, LAT, LON);
  CHECK(fabsf(a1 - a2) < 0.01f && fabsf(hw_ambient_at(T_LIGHTS_ON + 3600.0, LAT, LON) - a1) == 0.0f,
        "the sun's ambient is smooth and the same for the same time");
  HwWeather dim = HW_WX_CALM;
  dim.lux = 2.0f;
  hw_wx_set(dim);
  hw_frame(T_LIGHTS_ON, LAT, LON);
  CHECK(g_hw_amb == 0.0f, "a measured 2 lx (heavy overcast) beats the sun: no lift");
  hw_wx_set(HW_WX_CALM);
  g_hw_amb = 0.0f;
}

// A custom map with a long driveway: eight lights in one group. Turn-taking
// must stretch the group's period rather than crowd the events together.
static void test_big_group() {
  static const YardLight BIG[8] = {{"B1", YARD_G, 0, 0, 0},  {"B2", YARD_G, 0, 5, 5},   {"B3", YARD_G, 0, 10, 10},
                                   {"B4", YARD_G, 0, 15, 15}, {"B5", YARD_G, 0, 20, 20}, {"B6", YARD_G, 0, 25, 25},
                                   {"B7", YARD_G, 0, 30, 30}, {"B8", YARD_G, 0, 35, 35}};
  std::vector<HwSelf> lights;
  for (const YardLight &r : BIG) lights.push_back(hw_lookup(BIG, 8, r.name, "000000", 37.0f, 22.0f));
  CHECK(lights[7].n == 8 && lights[7].k == 7, "eight lights in one group");
  for (const Effect &fx : EFFECTS) {
    if (strcmp(fx.key, "jack") && strcmp(fx.key, "cauldron") && strcmp(fx.key, "windcandles")) continue;
    hw_wx_set(strcmp(fx.key, "windcandles") ? HW_WX_CALM : STORM);
    check_window(fx, lights, T0, "8-light group");
  }
  hw_wx_set(HW_WX_CALM);
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
  const double M = floor(T0 / HW_WX_PERIOD) * HW_WX_PERIOD;   // a whole minute of the shared clock
  g_hw_wxs = HwWxState();
  hw_wx_tick(true, M + 1);
  CHECK(!hw_wx_at(M + 1).on, "no readings yet: calm, weather off");
  hw_wx_in(HW_WX_WIND, 12.0f, M + 20); hw_wx_in(HW_WX_DIR, 200.0f, M + 20); hw_wx_in(HW_WX_TEMP, 40.0f, M + 20);
  hw_wx_tick(true, M + 30);
  CHECK(!hw_wx_at(M + 30).on, "a reading waits for the next whole minute");
  HwWeather a = hw_wx_at(M + 61);
  CHECK(a.on && a.wind_mph == 12.0f && a.dir_deg == 200.0f, "wind applies at the minute");
  CHECK(a.temp_f < 50.0f && a.temp_f > 47.0f, "temperature eases in (got %.2f)", a.temp_f);
  CHECK(fabsf(hw_wx_at(M + 71).temp_f - 40.0f) < 0.01f, "and arrives 10 s after the minute");
  hw_wx_in(HW_WX_WIND, 20.0f, M + 120 - 3);                   // inside the 5 s settle window
  CHECK(hw_wx_at(M + 121).wind_mph == 12.0f, "a reading 3 s before the minute waits a minute more");
  CHECK(hw_wx_at(M + 181).wind_mph == 20.0f, "and applies at the next");
  // a newer reading must not erase one that had settled for the coming minute,
  // whichever order this bulb's callbacks run in
  hw_wx_in(HW_WX_WIND, 14.0f, M + 190);                       // settled for M + 240
  hw_wx_in(HW_WX_WIND, 16.0f, M + 238);                       // inside the cutoff: for M + 300
  CHECK(hw_wx_at(M + 241).wind_mph == 14.0f, "a settled reading is held for its minute when a newer one arrives");
  CHECK(hw_wx_at(M + 301).wind_mph == 16.0f, "and the newer one follows a minute later");
  hw_wx_in(HW_WX_WIND, 7.0f, M + 320);                        // settled for M + 360
  hw_wx_in(HW_WX_WIND, 8.0f, M + 360.1);                      // arrives just after the minute, before any frame
  CHECK(hw_wx_at(M + 360.2).wind_mph == 7.0f, "a reading due at the minute is promoted even if the next one arrives first");
  hw_wx_in(HW_WX_WIND, NAN, M + 380);
  CHECK(!hw_wx_at(M + 421).on && hw_wx_at(M + 421).wind_mph == 0.0f, "an unknown wind reading falls back to calm");
  hw_wx_in(HW_WX_WIND, 12.0f, M + 430);
  hw_wx_tick(false, M + 440);                                  // Home Assistant goes away
  CHECK(!hw_wx_at(M + 481).on && hw_wx_at(M + 481).dir_deg == 270.0f, "losing HA falls back to calm");
  hw_wx_tick(true, M + 490);
  CHECK(!hw_wx_at(M + 541).on, "and stays calm after reconnecting until HA sends fresh readings");
  hw_wx_in(HW_WX_WIND, 9.0f, M + 550);
  CHECK(hw_wx_at(M + 601).on && hw_wx_at(M + 601).wind_mph == 9.0f, "a fresh reading after reconnecting applies");
  hw_wx_set(HW_WX_CALM);
}

// Seven bulbs, each with its own weather state, fed by one Home Assistant
// with a different delay to each (0 - 0.85 s), ticking at their own phases.
// Some readings are followed by another just after the next minute turns, so
// it reaches each bulb before that bulb has run a frame in the new minute.
// The effects that read the weather must show identical frames on every bulb
// except around two deliberate events - a bulb rebooting, and a reading sent
// just inside the settle cutoff for some bulbs but not others - and even then
// for no more than two weather minutes. The lift's ambient (measured light,
// sometimes lost so it falls back to the sun) is compared the same way.
static void test_weather_timeline() {
  const int N = 7;
  const float skew[N] = {0.0f, 0.12f, 0.25f, 0.40f, 0.55f, 0.70f, 0.85f};
  const double start = floor(T0 / HW_WX_PERIOD) * HW_WX_PERIOD - 1800.0, end = start + 2700.0, dt = 0.25;
  const double reboot_at = start + 900.4;
  const double straddle_minute = floor((start + 1500.0) / HW_WX_PERIOD) * HW_WX_PERIOD;
  HwWxState st[N];
  struct Send { double t; float v[HW_WX_N]; };
  std::vector<Send> sends;
  for (double t = start + 7.0; t < end; ) {
    uint32_t k = (uint32_t) sends.size();
    Send sd;
    sd.t = t;
    float v[HW_WX_N] = {sync_random(k, 1) * 25.0f, 10.0f + sync_random(k, 2) * 30.0f, sync_random(k, 3) * 5.0f,
                        sync_random(k, 4) * 360.0f, sync_random(k, 5) < 0.3f ? sync_random(k, 6) * 0.4f : 0.0f,
                        35.0f + sync_random(k, 7) * 30.0f, 30.0f + sync_random(k, 8) * 20.0f,
                        sync_random(k, 10) < 0.15f ? NAN : powf(10.0f, sync_random(k, 11) * 3.0f), NAN};   // lux, sometimes lost
    memcpy(sd.v, v, sizeof v);
    double cut = floor(t / HW_WX_PERIOD) * HW_WX_PERIOD + HW_WX_PERIOD - HW_WX_SETTLE;
    if (t > cut - 1.0 && t <= cut) sd.t = cut + 0.5;           // keep ordinary sends clear of the cutoff
    sends.push_back(sd);
    if (k % 5 == 4) {                                          // a second reading just after the next minute turns, so
      Send burst = sd;                                         // it reaches each bulb before that bulb's first frame
      burst.t = floor(sd.t / HW_WX_PERIOD) * HW_WX_PERIOD + HW_WX_PERIOD + 0.1;   // of the minute
      burst.v[HW_WX_WIND] = fmodf(sd.v[HW_WX_WIND] + 9.0f, 25.0f);
      burst.v[HW_WX_DIR] = fmodf(sd.v[HW_WX_DIR] + 135.0f, 360.0f);
      if (burst.t - sd.t > HW_WX_SETTLE) sends.push_back(burst);
    }
    t = sd.t + 47.0 + sync_random(k, 9) * 26.0;
  }
  Send late = sends.back();                                    // the deliberate straddle: some bulbs before the cutoff, some after
  late.t = straddle_minute - HW_WX_SETTLE - 0.3;
  late.v[HW_WX_DIR] = fmodf(late.v[HW_WX_DIR] + 180.0f, 360.0f);
  late.v[HW_WX_WIND] = 18.0f;
  sends.push_back(late);
  std::sort(sends.begin(), sends.end(), [](const Send &a, const Send &b) { return a.t < b.t; });

  auto lights = yard();
  const HwSelf &g2 = lights[3], &d1 = lights[5];
  size_t next[N] = {0, 0, 0, 0, 0, 0, 0};
  double last_tick[N];
  for (int i = 0; i < N; i++) last_tick[i] = start - 1.0 + i * 0.13;
  bool rebooted = false;
  double diverged = 0.0, outside = 0.0;
  for (double t = start; t < end; t += dt) {
    if (!rebooted && t >= reboot_at) {                          // bulb 5 reboots; HA resends its current state 3 s later
      rebooted = true;
      st[5] = HwWxState();
      size_t cur = 0;
      while (cur + 1 < sends.size() && sends[cur + 1].t <= reboot_at) cur++;
      next[5] = cur;
      sends.push_back(sends[cur]);
      sends.back().t = reboot_at + 3.0 - skew[5];
      std::sort(sends.begin() + (long) next[5] + 1, sends.end(), [](const Send &a, const Send &b) { return a.t < b.t; });
      next[5] = cur + 1;
    }
    HwColor ref[7];
    for (int i = 0; i < N; i++) {
      g_hw_wxs = st[i];
      while (next[i] < sends.size() && sends[next[i]].t + skew[i] <= t) {
        if (!(i != 5 && rebooted && sends[next[i]].t == reboot_at + 3.0 - skew[5])) {
          for (int f = 0; f < HW_WX_N; f++) hw_wx_in((HwWxField) f, sends[next[i]].v[f], sends[next[i]].t + skew[i]);
        }
        next[i]++;
      }
      while (last_tick[i] + 1.0 <= t) { last_tick[i] += 1.0; hw_wx_tick(true, last_tick[i]); }
      hw_frame(t, LAT, LON);                                     // the lift's ambient, as each lambda sets it
      float amb = g_hw_amb;
      HwColor c[7] = {hw_mist(t, g2), hw_mist(t, d1), hw_season(t, g2), hw_season(t, d1), hw_windcandles(t, g2),
                      hw_windcandles(t, d1), {amb, 0.0f, 0.0f, 0.0f, 0.0f}};
      st[i] = g_hw_wxs;
      if (i == 0) { memcpy(ref, c, sizeof c); continue; }
      bool same = true;
      for (int k = 0; k < 7; k++) {
        const float a[5] = {c[k].r, c[k].g, c[k].b, c[k].ww, c[k].cw}, r[5] = {ref[k].r, ref[k].g, ref[k].b, ref[k].ww, ref[k].cw};
        for (int ch = 0; ch < 5; ch++) if (fabsf(a[ch] - r[ch]) > 1e-4f) same = false;
      }
      if (!same) {
        diverged += dt;
        bool near_reboot = i == 5 && t >= reboot_at && t < reboot_at + 2 * HW_WX_PERIOD + HW_WX_EASE;
        bool near_straddle = t >= straddle_minute && t < straddle_minute + HW_WX_PERIOD + 15.0;
        if (!near_reboot && !near_straddle) outside += dt;
      }
    }
  }
  g_hw_wxs = HwWxState();
  hw_wx_set(HW_WX_CALM);
  g_hw_amb = 0.0f;
  printf("weather timeline: %zu readings to 7 bulbs over 45 min; bulb-seconds out of step: %.1f around the reboot and the straddle, %.1f elsewhere\n",
         sends.size(), diverged - outside, outside);
  CHECK(outside == 0.0, "bulbs disagreed about the weather outside the reboot and straddle windows (%.1f bulb-s)", outside);
  CHECK(diverged > 0.0, "the deliberate straddle and reboot should have shown up");
  CHECK(diverged - outside <= 6 * (2 * HW_WX_PERIOD + HW_WX_EASE), "divergence lasted longer than two weather minutes");
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
  // a frame at the cap whose floor lifts a dark channel: both the cap and the floor hold
  HwColor purple = hw_finish({0.48f, 0.0f, 0.8f, 0.0f, 0.064f}, HW_TREAT_FLOOR);
  float ps = purple.r + purple.g + purple.b + purple.ww + purple.cw;
  CHECK(ps <= HW_CAP + 1e-5f && purple.ww >= HW_TREAT_FLOOR.ww, "cap %.3f and floor %.3f both hold", ps, purple.ww);
  HwColor dark = hw_finish({0, 0, 0, 0, 0}, HW_EMBER);
  CHECK(same(dark, HW_EMBER), "an all-off frame comes out as the floor");
}

static void dump() {
  auto lights = yard();
  printf("effect,light,t,r,g,b,ww,cw\n");
  // the minute's weather when it has just changed: last minute calm from the west, this one the storm
  HwWeather before = HW_WX_CALM;
  before.on = true;
  before.wind_mph = 6.0f;
  before.dir_deg = 45.0f;
  for (int pass = 0; pass < 4; pass++)      // 0 calm, 1 storm, 2 daylight lift, 3 the weather turning
    for (const Effect &fx : EFFECTS) {
      if ((pass == 1 || pass == 3) && !reads_weather(fx)) continue;
      bool storm = pass == 1;
      hw_wx_set(storm ? STORM : HW_WX_CALM);
      g_hw_amb = pass == 2 ? 1.0f : 0.0f;
      double base = follows_clock(fx) ? T_LIGHTS_ON : T0;
      for (int i = 0; i < YARD_N; i++)
        for (int k = 0; k < 4000; k++) {
          double t = base + k * 1.3719;
          if (pass == 3) {                   // pin this minute's state: `before` last minute, STORM now
            int64_t m = (int64_t) floor(t / HW_WX_PERIOD);
            hw_wx_set(STORM);
            hw_wx_advance(t);
            g_hw_wxs.prev = before;
            g_hw_wxs.minute = m;
          }
          HwColor c = fx.fn(t, lights[i]);
          printf("%s%s,%s,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f\n", fx.key,
                 pass == 1 ? "@storm" : pass == 2 ? "@day" : pass == 3 ? "@turn" : "",
                 YARD_MAP[i].name, t, c.r, c.g, c.b, c.ww, c.cw);
        }
    }
  hw_wx_set(HW_WX_CALM);
  g_hw_amb = 0.0f;
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
  test_out();
  test_calendar_and_sun();
  test_weather_plumbing();
  test_weather_timeline();
  test_lift();
  test_floor_and_flashes();
  test_big_group();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all Halloween checks passed\n");
  return 0;
}
