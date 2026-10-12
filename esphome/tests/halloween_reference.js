// ---------------------------------------------------------------------------
// Halloween effects - reference model.
//
// The same maths as common/halloween_fx.h, in JavaScript, for two jobs:
//   - the preview page renders every effect from these functions, so what the
//     page shows is what the bulbs compute;
//   - tests/halloween_test.sh compares the C++ port against it (parity), and
//     the flash and floor checks run on both.
// Hashing (sync_hash), noise (sync_noise), slots and the 24 h fold mirror
// light_effects_helpers.h exactly; arithmetic is double here and float on the
// bulb, so outputs agree to ~1e-4, not bit for bit.
//
// f(T epoch seconds, bulb) -> [r, g, b, ww, cw] PWM levels 0..1, at 100 %
// brightness.
// ---------------------------------------------------------------------------
"use strict";

// The front yard. Feet; origin at G1, x east, y south (toward the street).
// path = distance from the street along the trick-or-treat route.
var YARD = [
  { id: "S1", g: 0, x: -23,   y: 75, path: 0,   h: 1.2 },
  { id: "S2", g: 0, x: -18.5, y: 75, path: 4,   h: 1.2 },
  { id: "G3", g: 1, x: 0,     y: 20, path: 60,  h: 7 },
  { id: "G2", g: 1, x: 0,     y: 10, path: 70,  h: 7 },
  { id: "G1", g: 1, x: 0,     y: 0,  path: 80,  h: 7 },
  { id: "D1", g: 2, x: 32,    y: 22, path: 118, h: 7 },
  { id: "D2", g: 2, x: 42,    y: 22, path: 128, h: 7 }
];
var DOOR = { x: 37, y: 22 };
var ROLE = ["S", "G", "D"];

// A bulb as the effects see it (HwSelf in C++): its row, group, index within
// the group, group size and a per-bulb offset from the shared dice.
function self(i) {
  var r = YARD[i], same = YARD.filter(function (o) { return o.g === r.g; });
  return { i: i, id: r.id, role: ROLE[r.g], gi: r.g, k: same.indexOf(r), n: same.length,
           x: r.x, y: r.y, path: r.path, h: r.h, door: Math.hypot(r.x - DOOR.x, r.y - DOOR.y),
           off: sync_random(i + 11, 977) };
}

// ---- light_effects_helpers.h, mirrored ----
function u32(whole) { return whole % 4294967296; }               // sync_u32 for whole >= 0
function sync_hash(bucket, salt) {
  var h = (Math.imul(bucket >>> 0, 2654435761) + Math.imul(salt >>> 0, 2246822519)) >>> 0;
  h = (h ^ (h >>> 15)) >>> 0; h = Math.imul(h, 2246822519) >>> 0;
  h = (h ^ (h >>> 13)) >>> 0; h = Math.imul(h, 3266489917) >>> 0;
  h = (h ^ (h >>> 16)) >>> 0;
  return h;
}
function sync_random(bucket, salt) { return (sync_hash(bucket, salt) & 0xFFFFFF) / 16777216; }
function sync_fold(t) { var f = (t + 54000) % 86400; if (f < 0) f += 86400; return Math.fround(f); }
function sync_noise(t, period, salt) {
  var u = t / period, fl = Math.floor(u), b = u32(fl), f = u - fl;
  var s = f * f * (3 - 2 * f), a = sync_random(b, salt), c = sync_random((b + 1) >>> 0, salt);
  return a + (c - a) * s;
}
function sync_slot(t, period) { var f = t % period; if (f < 0) f += period; return { slot: u32(Math.floor(t / period)), w: f }; }
function clamp01(v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// The bulb passes periods and offsets as float. At epoch scale t / 0.45 and
// t / 0.45f land ~100 buckets apart, so every such constant is rounded to
// float here exactly as the C++ sees it.
var F = Math.fround;

// ---- small helpers (halloween_fx.h) ----
function ease(f) { return f * f * (3 - 2 * f); }
function fmodp(a, n) { var f = a % n; return f < 0 ? f + n : f; }
function sc(c, k) { return [c[0] * k, c[1] * k, c[2] * k, c[3] * k, c[4] * k]; }
function mix(a, c, t) { return [a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t, a[3] + (c[3] - a[3]) * t, a[4] + (c[4] - a[4]) * t]; }
function add(a, c) { return [a[0] + c[0], a[1] + c[1], a[2] + c[2], a[3] + c[3], a[4] + c[4]]; }

// hw_out(): cap the channel sum (the bulb's power budget once the effect
// bypasses color_interlock), then hold every channel at or above the effect's
// floor, so a light never goes dark.
var CAP = 1.3;
function out(c, floor) {
  var s = c[0] + c[1] + c[2] + c[3] + c[4], k = s > CAP ? CAP / s : 1, o = new Array(5);
  for (var i = 0; i < 5; i++) o[i] = clamp01(Math.max(c[i] * k, floor[i]));
  return o;
}

// Turn-taking: a group shares one slot of `period` seconds, split into one
// window per bulb, so occasional events (gutters, pops) on neighbours never
// land in the same second.
function groupTurn(T, b, period, reserve, salt) {
  period = F(period); reserve = F(reserve);
  var Tg = T + F(sync_random(b.gi, 991) * period), s = sync_slot(Tg, period), sub = F(period / b.n);
  return { w: F(s.w), slot: s.slot, at: b.k * sub + sync_random(s.slot, salt + b.i) * (sub - reserve) };
}

//                  r     g     b     ww    cw
var PUMPKIN     = [0.45, 0.08, 0,    0.50, 0   ];
var EMBER       = [0.10, 0.01, 0,    0.05, 0   ];   // floor: Jack-o'-Lantern, Brownout, Scare Cue
var VIOLET      = [0.50, 0,    1,    0,    0.08];
var STORM_FLOOR = [0.05, 0,    0.10, 0,    0.02];   // floor: Lightning, Something Follows You
var STRIKE      = [0,    0,    0,    0.25, 1   ];
var POISON      = [0.10, 0.90, 0,    0,    0   ];
var VIO2        = [0.60, 0,    1,    0,    0.12];
var LIME        = [0.40, 1,    0,    0,    0.15];
var BREW_FLOOR  = [0.03, 0.05, 0.05, 0,    0.02];   // floor: Witch's Cauldron
var MOONWHITE   = [0,    0,    0,    0.50, 0.30];
var COPPER      = [0.80, 0.06, 0,    0.05, 0   ];
var MOON_FLOOR  = [0.03, 0,    0,    0.05, 0   ];   // floor: Blood Moon
var SICKLY      = [0,    0.15, 0,    0,    0.60];
var MOONLIGHT   = [0,    0,    0.25, 0,    0.40];
var NIGHT_FLOOR = [0,    0,    0.08, 0,    0.10];   // floor: moonlit scenes
var AMBER       = [1,    0.28, 0,    0.20, 0   ];
var ANGRY       = [1,    0.02, 0,    0,    0   ];
var EYE_FLOOR   = [0.12, 0.02, 0,    0.02, 0   ];
var BLOOD       = [1,    0,    0.03, 0,    0   ];
var BLOOD_FLOOR = [0.17, 0,    0.005, 0,   0   ];
var SPECTRE     = [0,    0.08, 0.45, 0,    0.80];
var WISP        = [0,    0.35, 0.30, 0,    0.45];
var FOG         = [0.05, 0.55, 0.05, 0,    0.35];
var CANDLE      = [0.15, 0,    0,    0.45, 0   ];
var SPIRIT      = [0,    0.10, 0.35, 0,    0.42];
var TREAT_ORANGE= [0.60, 0.10, 0,    0.10, 0   ];   // luminance-matched pair:
var TREAT_PURPLE= [0.60, 0,    1,    0,    0.08];   // a swap is a hue change, not a flash
var TREAT_FLOOR = [0.10, 0,    0.05, 0.02, 0   ];

function pumpkinAt(lvl) {
  var warm = 0.6 + 0.4 * lvl;                       // redder as it dims
  return [PUMPKIN[0] * lvl, PUMPKIN[1] * lvl * warm, 0, PUMPKIN[3] * lvl * warm, 0];
}

// ======================= BUILT =======================

// 1. Jack-o'-Lantern Row. Gentle two-layer flame (its swing stays under the
// 10 % flash threshold) plus one slow gutter per bulb every 7 s, taking turns
// within the group.
function jack(T, b, steady) {
  var Tj = T + F(b.off * 7);
  var slow = sync_noise(Tj, F(0.45), b.i + 1), fast = sync_noise(Tj, F(0.11), b.i + 21);
  var lvl = 0.66 + 0.10 * (slow - 0.5) + 0.03 * (fast - 0.5);
  var gt = groupTurn(T, b, 7, 1.6, 50);
  var len = 0.45 + sync_random(gt.slot, b.i + 60) * 0.35;
  if (!steady && gt.w >= gt.at && gt.w < gt.at + len) {
    var u = gt.w - gt.at, e = Math.min(clamp01(u / 0.25), clamp01((len - u) / 0.30));
    lvl = lvl + (0.36 - lvl) * ease(e);
  }
  if (b.gi === 2) lvl *= 1.1;
  return out(pumpkinAt(lvl), EMBER);
}

// 6. Lightning Over the House. One strike per 11 s slot, never a flicker
// burst: a fast rise and a slow fall; about one in three is a soft distant
// glow. The street bed peaks at 40 %.
function lightning(T, b) {
  var t = sync_fold(T), s = sync_slot(T, 11), w = s.w;
  var base = sc(VIOLET, 0.14 * (0.85 + 0.15 * Math.sin(t * 0.3)));
  var at = 0.8 + sync_random(s.slot, 1) * 8, L = 0;
  var distant = sync_random(s.slot, 2) < 0.35;
  var peak = distant ? 0.35 : 0.75 + 0.25 * sync_random(s.slot, 30);
  var rise = distant ? 0.25 : 0.06, hold = distant ? 0.10 : 0.05, fall = distant ? 2.5 : 4.0;
  if (w >= at) {
    var u = w - at;
    L = u < rise ? peak * (u / rise) : u < rise + hold ? peak : peak * Math.exp(-(u - rise - hold) * fall);
  }
  if (b.gi === 0) L *= 0.4;
  return out(add(base, sc(STRIKE, L)), STORM_FLOOR);
}

// 7. Witch's Cauldron. Each bulb drifts poison green <-> violet on its own
// phase; one slow bubble per bulb every 4.2 s, taking turns within the group.
function cauldron(T, b) {
  var t = sync_fold(T + F(b.off * 20));
  var m = 0.5 + 0.5 * Math.sin(t * 0.21 + Math.sin(t * 0.13) * 2);
  var col = sc(mix(POISON, VIO2, m), 0.55);
  var gt = groupTurn(T, b, 4.2, 1.0, 3), w = gt.w - gt.at;
  var env = w < 0 ? 0 : w < 0.4 ? ease(w / 0.4) : Math.exp(-(w - 0.4) * 4);
  return out(add(col, sc(LIME, env * 0.28)), BREW_FLOOR);
}

// 9. Blood Moon. Moon white, then an eclipse shadow sweeps west to east and
// turns everything copper red; four-minute cycle.
function moon(T, b) {
  var c = fmodp(T - F((b.x + 25) / 3), 240), k;
  if (c < 50) k = 0; else if (c < 90) k = ease((c - 50) / 40); else if (c < 170) k = 1;
  else if (c < 210) k = 1 - ease((c - 170) / 40); else k = 0;
  var col = mix(sc(MOONWHITE, 0.75), sc(COPPER, 0.5), k);
  if (k === 1) col = sc(col, 0.85 + 0.15 * Math.sin(c * 0.5));
  return out(col, MOON_FLOOR);
}

// ======================= DESIGNED (preview only) =======================

function followsPos(T) {
  var CYC = 52, s = sync_slot(T, CYC), c = s.w, r = sync_random(s.slot, 5);
  if (r < 0.25) return { p: c < 18.75 ? c * 3.2 : c < 21.75 ? 60 : 60 + (c - 21.75) * 3.2, dir: 1, c: c, CYC: CYC };
  if (r < 0.40) return { p: 128 - c * 3.2, dir: -1, c: c, CYC: CYC };
  return { p: c * 3.2, dir: 1, c: c, CYC: CYC };
}
function follows(T, b) {
  var f = followsPos(T), g, t = sync_fold(T);
  if (f.dir > 0 && b.gi === 2 && f.p >= b.path) {
    g = 0.62 + 0.18 * Math.sin(t * 1.6 + b.i);
    if (f.c > f.CYC - 5) g *= (f.CYC - f.c) / 5;
  } else {
    var d = (f.p - b.path) * f.dir;
    g = d < 0 ? Math.exp(-(d / 8) * (d / 8)) : Math.exp(-d / 13);
    if (f.dir < 0 && f.c > f.CYC - 5) g *= (f.CYC - f.c) / 5;
  }
  return out(mix(sc(VIOLET, 0.10), SICKLY, clamp01(g)), STORM_FLOOR);
}

function lookAt(T) {
  var s = sync_slot(T, 9);
  var a = (sync_random((s.slot - 1) >>> 0, 3) - 0.5) * 2, z = (sync_random(s.slot, 3) - 0.5) * 2;
  return a + (z - a) * ease(clamp01(s.w / 1.5));
}
function watching(T, b) {
  var t = sync_fold(T);
  if (b.gi !== 2) return out(sc(MOONLIGHT, 0.62 + 0.12 * Math.sin(t * 0.45 + b.i * 1.3)), NIGHT_FLOOR);
  var s = sync_slot(T, 5), w = s.w, at = 1.2 + sync_random(s.slot, 7) * 2.4;
  var side = b.id === "D1" ? -1 : 1, lvl = 0.72 * (1 + 0.35 * lookAt(T) * side);
  // ~1 slot in 7 the eyes narrow and redden after the blink, easing back
  // before the slot ends, so the change is a slow shift, not a jump
  var anger = 0;
  if (sync_random(s.slot, 9) < 0.15) anger = ease(clamp01((w - at - 0.4) / 0.6)) * (1 - ease(clamp01((w - 3.9) / 1.0)));
  var col = mix(sc(AMBER, clamp01(lvl)), sc(ANGRY, clamp01(lvl * 0.5)), anger);
  // a slow blink: the lid closes to 15 % in 0.15 s and opens over 0.25 s
  var u = w - at, lid = 1;
  if (u >= 0 && u < 0.15) lid = 1 - 0.85 * ease(u / 0.15);
  else if (u >= 0.15 && u < 0.40) lid = 0.15 + 0.85 * ease((u - 0.15) / 0.25);
  return out(sc(col, lid), EYE_FLOOR);
}

function heart(T, b) {
  var P = 1.5, s = sync_slot(T - b.door / 180, P), w = s.w;   // 40 bpm
  var lvl = 0.15 + 0.80 * Math.exp(-6 * w) + (w > 0.35 ? 0.25 * Math.exp(-6 * (w - 0.35)) : 0);
  var amp = 1 - 0.35 * Math.min(1, b.door / 80);
  return out(sc(BLOOD, clamp01(lvl * amp)), BLOOD_FLOOR);
}

// 5. Brownout (was Blackout): the lights sag to an ember, door first, and
// come back street first with one slow surge. Never dark.
function brownout(T, b) {
  var s = sync_slot(T, 80), c = s.w, lvl = 0.7 + 0.04 * Math.sin(sync_fold(T) * 1.7);
  var sag = 63 + (2 - b.gi) * 0.8, back = 69 + b.gi * 1.5;
  if (c >= 60 && c < sag) lvl = 0.7 - 0.30 * ease(clamp01((c - 60) / 3));
  else if (c >= sag && c < back) lvl = 0.4 - 0.25 * ease(clamp01((c - sag) / 1.2));
  else if (c >= back && c < back + 3) lvl = 0.15 + 0.55 * ease(clamp01((c - back) / 3));
  return out(pumpkinAt(lvl), EMBER);
}

var GROUP_AT = { 0: [-21, 75], 1: [0, 10], 2: [37, 22] };
function ghostPos(T) {
  var s = sync_slot(T, 20), c = s.w;
  if (c > 12) return null;
  var a = Math.floor(sync_random(s.slot, 1) * 3), z = (a + 1 + Math.floor(sync_random(s.slot, 2) * 2)) % 3;
  var A = GROUP_AT[a], Z = GROUP_AT[z], dx = Z[0] - A[0], dy = Z[1] - A[1], len = Math.hypot(dx, dy);
  var ux = dx / len, uy = dy / len, p = c / 12 * (len + 30);
  return { x: A[0] - ux * 15 + ux * p, y: A[1] - uy * 15 + uy * p };
}
function ghost(T, b) {
  var base = [0, 0, 0.08, 0, 0.04], g = ghostPos(T);
  if (!g) return out(base, NIGHT_FLOOR);
  var d = Math.hypot(b.x - g.x, b.y - g.y);
  return out(add(base, sc(SPECTRE, 0.9 * Math.exp(-(d / 16) * (d / 16)))), NIGHT_FLOOR);
}

var CUES = [];
function scareAt(T, b, cue) {
  var r = T - cue, u = jack(T, b, r >= -1.2 && r < 1.0);
  if (r < 0 || r > 7) return u;
  var ember = pumpkinAt(0.2);
  var drained = mix(u, ember, ease(clamp01(r / 0.8)));
  if (b.gi === 1) {                                  // a sickly swell runs up the drive
    var at = 1.0 + b.k * 1.0, v = r - at;           // G3, then G2, then G1, a second apart
    var sw = v < 0 ? 0 : v < 0.6 ? ease(v / 0.6) : Math.exp(-(v - 0.6) * 2.0);
    if (r > 5) return out(mix(drained, u, ease(clamp01((r - 5) / 2))), EMBER);
    return out(add(drained, sc(SICKLY, sw * 0.9)), EMBER);
  }
  if (b.gi === 2) {
    var red = r < 3.2 ? 0 : r < 3.8 ? ease((r - 3.2) / 0.6) : r < 5.5 ? 1 : 1 - ease(clamp01((r - 5.5) / 1.5));
    var bg = r < 5.5 ? drained : mix(drained, u, ease(clamp01((r - 5.5) / 1.5)));
    return out(mix(bg, BLOOD, red), EMBER);
  }
  if (r > 4.5) return out(mix(drained, u, ease(clamp01((r - 4.5) / 2))), EMBER);
  return out(drained, EMBER);
}
function scare(T, b) {
  var cue = Math.floor((T - 3) / 14) * 14 + 3;
  for (var i = 0; i < CUES.length; i++) if (T >= CUES[i] && T - CUES[i] < 7) cue = CUES[i];
  return scareAt(T, b, cue);
}

// ======================= NEW =======================

// 11. Will-o'-the-Wisp: a pale wisp glides a little ahead of visitors, street
// to door, swelling each light for several seconds as it passes.
function wispPos(T) { var s = sync_slot(T, 60); return { p: s.w * 3.5 - 10, c: s.w }; }
function wisp(T, b) {
  var f = wispPos(T), d = f.p - b.path, g = Math.exp(-(d / 14) * (d / 14));
  if (b.gi === 2 && f.p > b.path) g = Math.max(g, 0.55 * clamp01((60 - f.c) / 8));
  return out(add(sc(MOONLIGHT, 0.45), sc(WISP, g)), NIGHT_FLOOR);
}

// 12. Creeping Fog: a green fog front rolls in from the street at 1 ft/s,
// swallows the drive and the door, thins, and clears; 3-minute cycle.
function fogFront(T) {
  var c = sync_slot(T, 180).w;
  return c < 100 ? { y: 90 - c * 1.0, thin: 1 } : c < 160 ? { y: -10, thin: 1 - (c - 100) / 60 } : { y: -10, thin: 0 };
}
function fog(T, b) {
  var f = fogFront(T), into = (f.y < b.y) ? clamp01((b.y - f.y) / 15) : 0;
  var swirl = 0.75 + 0.25 * sync_noise(T + b.y / 2, 6, 7);
  var d = ease(into) * swirl * f.thin;
  return out(mix(sc(MOONLIGHT, 0.6), sc(FOG, 0.65), d), NIGHT_FLOOR);
}

// 13. Seance Circle: steady candles; a spirit passes from bulb to bulb along
// the path and back, turning each one ghost blue-white as it goes through.
var SEANCE_STEP = 2.5;
function seanceAt(T) {
  var s = sync_slot(T, SEANCE_STEP * 12), pos = s.w / SEANCE_STEP;   // 0..12
  return pos <= 6 ? pos : 12 - pos;                                  // ping-pong along the path
}
function seance(T, b) {
  var p = seanceAt(T), d = Math.abs(p - b.i), g = d < 1 ? ease(1 - d) : 0;
  var flick = 0.94 + 0.06 * sync_noise(T + b.off * 9, 0.6, b.i + 5);
  return out(mix(sc(CANDLE, flick), SPIRIT, g), EMBER);
}

// 14. Trick-or-Treat Swap: neighbours alternate orange and purple and trade
// colours every 8 s with a 2.5 s crossfade. The two colours carry the same
// luminance, so a swap reads as a change of hue, not a flash.
function swap(T, b) {
  var s = sync_slot(T, 8), parity = (s.slot + YARD_ORDER[b.i]) % 2;
  var from = parity ? TREAT_PURPLE : TREAT_ORANGE, to = parity ? TREAT_ORANGE : TREAT_PURPLE;
  var k = s.w > 8 - 2.5 ? ease((s.w - 5.5) / 2.5) : 0;
  return out(sc(mix(from, to, k), 0.8), TREAT_FLOOR);
}
var YARD_ORDER = [0, 1, 2, 3, 4, 5, 6];

var FXFN = { jack: jack, lightning: lightning, cauldron: cauldron, moon: moon,
  follows: follows, watching: watching, heart: heart, brownout: brownout, ghost: ghost, scare: scare,
  wisp: wisp, fog: fog, seance: seance, swap: swap };
var FLOORS = { jack: EMBER, lightning: STORM_FLOOR, cauldron: BREW_FLOOR, moon: MOON_FLOOR };

// ======================= appearance + checks =======================
// Each channel's light output relative to warm white at full, as linear sRGB
// (typical RGBCW bulb: red ~20 %, green ~40 %, blue ~6 % of warm white).
var LED = [[0.70, 0, 0], [0.02, 0.42, 0.06], [0.015, 0.03, 0.30], [1, 0.72, 0.42], [0.74, 0.77, 0.80]];
var Y_WW = 0.2126 + 0.7152 * 0.72 + 0.0722 * 0.42;
function toLinear(c) {
  var r = 0, g = 0, bl = 0;
  for (var i = 0; i < 5; i++) { r += c[i] * LED[i][0]; g += c[i] * LED[i][1]; bl += c[i] * LED[i][2]; }
  return [r, g, bl];
}
function lumRel(lin) { return (0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]) / Y_WW; }
function isSatRed(lin) { var s = lin[0] + lin[1] + lin[2]; return s > 0.02 && lin[0] / s >= 0.8; }

// WCAG 2.3.1 counting. A transition is a swing of >= 10 % of warm-white
// luminance against the running extreme; a flash is a pair of opposing
// transitions, so flashes in a window = transitions / 2. Red when either end
// is saturated red. A group merges its bulbs' transitions in time order and
// counts a run of same-direction changes (a sweep across the group) as one
// event, so it measures reversals the eye sees in that part of the yard.
// Also reports the lowest
// luminance seen (the floor check).
function flashCheck(fn, seconds, dt, t0) {
  var n = Math.round(seconds / dt), groups = [[], [], []], bulb = 0, red = 0, minLum = 1e9, perBulb = [];
  for (var i = 0; i < YARD.length; i++) {
    var b = SELF[i], ext = null, trend = 0, extRed = false, tr = [];
    for (var k = 0; k < n; k++) {
      var t = t0 + k * dt, lin = toLinear(fn(t, b)), y = lumRel(lin), r = isSatRed(lin);
      if (y < minLum) minLum = y;
      if (ext === null) { ext = y; extRed = r; continue; }
      if (trend === 1 && y > ext) { ext = y; extRed = r; } else if (trend === -1 && y < ext) { ext = y; extRed = r; }
      var up = y - ext >= 0.1, down = ext - y >= 0.1;
      if ((trend <= 0 && up) || (trend >= 0 && down)) { tr.push({ t: t, d: up ? 1 : -1, red: r || extRed }); trend = up ? 1 : -1; ext = y; extRed = r; }
    }
    var mb = Math.floor(maxIn(tr, false) / 2);
    perBulb.push(mb); bulb = Math.max(bulb, mb); red = Math.max(red, Math.floor(maxIn(tr, true) / 2));
    groups[b.gi] = groups[b.gi].concat(tr);
  }
  var group = 0;
  groups.forEach(function (g) {
    g.sort(function (a, z) { return a.t - z.t; });
    // a run of same-direction changes across the group's bulbs is one sweep, not several flashes
    var o = [];
    g.forEach(function (x) {
      var p = o[o.length - 1];
      if (p && p.d === x.d && x.t - p.t < 1) { if (x.red) p.red = true; return; }
      o.push({ t: x.t, d: x.d, red: x.red });
    });
    group = Math.max(group, Math.floor(maxIn(o, false) / 2));
    red = Math.max(red, Math.floor(maxIn(o, true) / 2));
  });
  return { bulb: bulb, group: group, red: red, minLum: minLum, perBulb: perBulb };
}
function maxIn(tr, redOnly) {
  var ts = redOnly ? tr.filter(function (x) { return x.red; }) : tr, best = 0, j = 0;
  for (var i = 0; i < ts.length; i++) { while (ts[i].t - ts[j].t >= 1) j++; best = Math.max(best, i - j + 1); }
  return best;
}

var SELF = YARD.map(function (r, i) { return self(i); });

if (typeof module !== "undefined") {
  module.exports = { YARD: YARD, SELF: SELF, FXFN: FXFN, FLOORS: FLOORS, flashCheck: flashCheck,
    toLinear: toLinear, lumRel: lumRel, sync_hash: sync_hash, sync_random: sync_random,
    sync_noise: sync_noise, sync_fold: sync_fold, self: self };
}
