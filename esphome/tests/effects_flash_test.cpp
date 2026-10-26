// ---------------------------------------------------------------------------
// Flash and floor check for the accent and white lambda effects
// (accent_light_nonaddressable_effects.yaml, white_effects.yaml,
// accent_light_2d_effects.yaml). Built and run by run_effects_tests.sh, which
// extracts every plain lambda effect as it stands in the YAML (extract_lambda.py
// --all).
//
// Each effect runs for 30 minutes at 10 ms on a fake light, once per light in
// a forked child (the MAC lookups cache per process), and its output channels
// are turned into luminance relative to warm white at full. Counted as
// halloween_test.cpp counts, the WCAG 2.3.1 way: a transition is a swing of
// >= 10 % against the running extreme, a flash is a pair of opposing ones.
//   bulb   worst 1 s on one light
//   group  worst 1 s over a group, a run of same-direction changes as one sweep
//          (the plain effects: five lights merged; the 2D ones: the room's mean
//          colour, as halloween_test.cpp has it. The lights within 0.25 of one
//          another are merged too and printed, but a crest sweeping past
//          them is not a flash, so that figure is not held to the limit)
//   red    the same, for saturated red
// All must be <= 2 (WCAG allows 3; the extra headroom is deliberate). The floor is each effect's lowest luminance, which may not
// fall under the value in FLOORS (what it was when the limit moved to 2/s).
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
#include <sys/wait.h>
#include <unistd.h>

static double g_now = 1.8e9;
static int fake_gettimeofday(struct timeval *tv) {
  tv->tv_sec = (time_t) floor(g_now);
  tv->tv_usec = (suseconds_t) llround((g_now - floor(g_now)) * 1e6);
  if (tv->tv_usec >= 1000000) { tv->tv_sec++; tv->tv_usec -= 1000000; }
  return 0;
}
#define gettimeofday(tv, tz) fake_gettimeofday(tv)
static uint32_t millis() { return (uint32_t) (uint64_t) (g_now * 1000.0); }
static uint32_t micros() { return 0; }   // a frozen clock lets lockstep catch up fully
static uint32_t random_uint32() { static uint32_t s = 12345; return s = s * 1664525u + 1013904223u; }

#include "basement_map.h"
#include "fx2d_render.h"

struct FakeOutput {
  float level = 0;
  void set_level(float v) { level = v; }
};
// The picker colour the Picker effects read: a warm orange at full brightness.
struct FakeValues {
  float get_brightness() const { return 1.0f; }
  float get_red() const { return 1.0f; }
  float get_green() const { return 0.6f; }
  float get_blue() const { return 0.3f; }
};
struct FakeLight {
  FakeValues current_values;
  float get_gamma_correct() const { return 2.8f; }
};
static FakeLight accent_light;
static FakeOutput red_output, green_output, blue_output, warm_output, cold_output;
#define id(x) (&(x))

struct Color { uint8_t r = 0, g = 0, b = 0, w = 0; };
struct FxIt {};
static const char *FX_TEST_MAC = "fxtest";
static float FX_TEST_OFFSET = 0.0f;
template<typename T> static T fx_probe(const char *, T v) { return v; }
typedef void (*FxRun)(bool);
struct FxCase { const char *name; FxRun run; };

#include "effects_flash_extracted.h"

// Luminance per channel at full, linear sRGB relative to warm white (as in halloween_test.cpp).
static const float LED[5][3] = {
    {0.70f, 0.00f, 0.00f}, {0.02f, 0.42f, 0.06f}, {0.015f, 0.03f, 0.30f}, {1.00f, 0.72f, 0.42f}, {0.74f, 0.77f, 0.80f}};
static const double Y_WW = 0.2126 + 0.7152 * 0.72 + 0.0722 * 0.42;

struct Trans { double t; int d; bool red; };
static int max_in(const std::vector<Trans> &tr, bool red_only) {
  std::vector<double> ts;
  for (auto &x : tr) if (!red_only || x.red) ts.push_back(x.t);
  int best = 0;
  size_t j = 0;
  for (size_t i = 0; i < ts.size(); i++) {
    while (ts[i] - ts[j] >= 1.0) j++;
    best = std::max(best, (int) (i - j + 1));
  }
  return best;
}
static int group_flashes(std::vector<Trans> g, bool red_only) {
  std::sort(g.begin(), g.end(), [](const Trans &a, const Trans &b) { return a.t < b.t; });
  std::vector<Trans> o;
  for (auto &x : g) {
    if (!o.empty() && o.back().d == x.d && x.t - o.back().t < 1.0) { if (x.red) o.back().red = true; continue; }
    o.push_back(x);
  }
  return max_in(o, red_only) / 2;
}

// One signal's transitions, counted per WCAG 2.3.1 as described above.
struct LumTrace {
  bool have = false, ext_red = false;
  double ext = 0;
  int trend = 0;
  std::vector<Trans> tr;
  void add(double t, const double lin[3]) {
    double y = (0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]) / Y_WW, s = lin[0] + lin[1] + lin[2];
    bool r = s > 0.02 && lin[0] / s >= 0.8;
    if (!have) { ext = y; ext_red = r; have = true; return; }
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
};

static const long FRAMES = 180000;   // 30 minutes at 10 ms
static const double T0 = 1793491200.0 + 20 * 3600.0, DT = 0.01;   // an October evening, UTC

// One effect on one light: the linear colour it makes each frame, as three floats.
static void run_light(FxRun run, FILE *fp) {
  red_output.level = green_output.level = blue_output.level = warm_output.level = cold_output.level = 0;
  for (long k = 0; k < FRAMES; k++) {
    g_now = T0 + k * DT;
    run(k == 0);
    const float ch[5] = {red_output.level, green_output.level, blue_output.level, warm_output.level, cold_output.level};
    float lin[3] = {0, 0, 0};
    for (int i = 0; i < 5; i++) for (int j = 0; j < 3; j++) lin[j] += ch[i] * LED[i][j];
    fwrite(lin, sizeof lin, 1, fp);
  }
}

struct Set {
  const char *title;
  const FxCase *cases;
  size_t n;
  bool room;   // 2D: lights are the room map's, grouped by distance
};

// Lowest luminance each effect may reach: the minimum it had when the flash limit moved to 3/s.
struct Floor { const char *name; double lum; };
static const Floor FLOORS[] = {
#include "effects_flash_floors.h"
};

static int failures = 0;
#define CHECK(cond, ...)                          \
  do {                                            \
    if (!(cond)) { failures++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
  } while (0)

static const char *g_only = nullptr;   // argv[1]: run only the effects whose name contains it

static int run_set(const Set &s) {
  const int nl = s.room ? ROOM_N : 5;
  static const float offs[5] = {0.00f, 0.21f, 0.47f, 0.68f, 0.93f};
  printf("%-34s %5s %5s %5s %8s\n", s.title, "bulb", "group", "red", "min lum");
  for (size_t i = 0; i < s.n; i++) {
    if (g_only && !strstr(s.cases[i].name, g_only)) continue;
    // Each light in its own process, as the MAC lookups cache per process; the signal comes back in a temp file.
    std::vector<std::string> files;
    std::vector<pid_t> pids;
    for (int l = 0; l < nl; l++) {
      files.push_back("/tmp/fxflash_" + std::to_string(getpid()) + "_" + std::to_string(l));
      pid_t p = fork();
      if (p == 0) {
        FX_TEST_MAC = s.room ? ROOM_MAP[l].mac : "000000";
        FX_TEST_OFFSET = s.room ? (float) l / ROOM_N : offs[l];
        FILE *fp = fopen(files[l].c_str(), "wb");
        run_light(s.cases[i].run, fp);
        fclose(fp);
        _exit(0);
      }
      pids.push_back(p);
    }
    for (pid_t p : pids) {
      int st;
      waitpid(p, &st, 0);
      if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL: %s crashed\n", s.cases[i].name); return 1; }
    }
    std::vector<LumTrace> bulb(nl);
    LumTrace room;
    std::vector<std::vector<float>> lin(nl, std::vector<float>(FRAMES * 3));
    double min_lum = 1e9;
    for (int l = 0; l < nl; l++) {
      FILE *fp = fopen(files[l].c_str(), "rb");
      if (fread(lin[l].data(), sizeof(float), FRAMES * 3, fp) != (size_t) FRAMES * 3) { printf("FAIL: short trace\n"); return 1; }
      fclose(fp);
      remove(files[l].c_str());
    }
    for (long k = 0; k < FRAMES; k++) {
      double mean[3] = {0, 0, 0};
      for (int l = 0; l < nl; l++) {
        double c[3] = {lin[l][3 * k], lin[l][3 * k + 1], lin[l][3 * k + 2]};
        bulb[l].add(T0 + k * DT, c);
        min_lum = std::min(min_lum, (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / Y_WW);
        for (int j = 0; j < 3; j++) mean[j] += c[j] / nl;
      }
      room.add(T0 + k * DT, mean);
    }
    int nb = 0, grp = 0, red = 0, nbhd = 0, room_f = 0;
    std::vector<Trans> all;
    for (int l = 0; l < nl; l++) {
      nb = std::max(nb, max_in(bulb[l].tr, false) / 2);
      red = std::max(red, max_in(bulb[l].tr, true) / 2);
      all.insert(all.end(), bulb[l].tr.begin(), bulb[l].tr.end());
      if (!s.room) continue;
      std::vector<Trans> g;   // the lights within 0.25 of this one
      for (int j = 0; j < nl; j++)
        if (hypotf(ROOM_MAP[l].x - ROOM_MAP[j].x, (ROOM_MAP[l].y - ROOM_MAP[j].y) * ROOM_ASPECT) <= 0.25f)
          g.insert(g.end(), bulb[j].tr.begin(), bulb[j].tr.end());
      nbhd = std::max(nbhd, group_flashes(g, false));
    }
    if (s.room) {   // the room as one lamp: its mean colour
      room_f = max_in(room.tr, false) / 2;
      grp = room_f;
      red = std::max(red, max_in(room.tr, true) / 2);
    } else {
      grp = group_flashes(all, false);
      red = std::max(red, group_flashes(all, true));
    }
    printf("%-34s %5d %5d %5d %8.4f", s.cases[i].name, nb, grp, red, min_lum);
    if (s.room) printf("   (neighbours %d, room %d)", nbhd, room_f);
    printf("\n");
    CHECK(nb <= 2 && grp <= 2 && red <= 2, "%s: %d/%d/%d flashes per second (bulb/group/red), limit 2", s.cases[i].name, nb, grp, red);
    const Floor *fl = nullptr;
    for (const Floor &f : FLOORS) if (!strcmp(f.name, s.cases[i].name)) fl = &f;
    CHECK(fl, "%s: no floor listed in effects_flash_floors.h", s.cases[i].name);
    if (fl) CHECK(min_lum >= fl->lum - 2e-4f, "%s: luminance %.4f fell under its floor %.4f", s.cases[i].name, min_lum, fl->lum);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1) g_only = argv[1];
  const Set sets[] = {
      {"RGB effects (30 min)", rgb_cases, sizeof rgb_cases / sizeof *rgb_cases, false},
      {"White effects (30 min)", w_cases, sizeof w_cases / sizeof *w_cases, false},
      {"2D effects (30 min)", d_cases, sizeof d_cases / sizeof *d_cases, true},
  };
  for (const Set &s : sets) if (run_set(s)) return 1;
  if (failures) { printf("%d check(s) failed\n", failures); return 1; }
  printf("all flash and floor checks passed\n");
  return 0;
}
