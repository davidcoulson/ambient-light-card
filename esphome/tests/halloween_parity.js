// Parity: the C++ effects against the reference model the preview page runs.
//
//   ./halloween_test --dump | node halloween_parity.js
//
// Reads the C++ frames from stdin and recomputes each one with
// halloween_reference.js ("@storm" frames under the simulated storm, "@day"
// frames with the full daylight lift, "@turn" frames in a minute whose weather
// has just changed, which exercise the eases and crossfades). Run
// with TZ=America/New_York, as run_halloween_tests.sh does: the calendar
// effects read local time. The bulb works in float and the reference in double,
// so frames agree to ~1e-4 rather than bit for bit; a sample that lands within
// a few microseconds of an event edge (a gutter starting, a strike) can fall on
// the other side of it. Passes when 99.9 % of samples agree within 0.01 on
// every channel. Any malformed or non-finite row fails it, and so does a dump
// that is incomplete: it must end with its "#end,<rows>" trailer and cover
// every built effect in every variant.
"use strict";
const ref = require("./halloween_reference.js");

const lines = require("fs").readFileSync(0, "utf8").trim().split("\n");
const rows = lines.slice(1).filter((l) => !l.startsWith("#"));
const trailer = lines.find((l) => l.startsWith("#end,"));
const byName = {};
ref.SELF.forEach((b) => { byName[b.id] = b; });

// The storm the C++ test simulates for "@storm" frames.
const CALM = Object.assign({}, ref.WX);
const STORM = { on: true, windMph: 30, gustMph: 45, lullMph: 5, dirDeg: 225, rainInH: 0.6, tempF: 38, dewF: 37 };
// "@turn" frames: the weather just changed - last minute a light wind from the northeast, this minute the storm.
const BEFORE = Object.assign({}, CALM, { on: true, windMph: 6, dirDeg: 45 });

const stats = {};
let bad = 0;
for (const line of rows) {
  const parts = line.split(",");
  const [tag, light, t, ...ch] = parts;
  const [key, variant] = tag.split("@");
  // a malformed row - wrong shape, unknown effect or light, a non-finite number - is a failure, not a skip
  const nums = [t].concat(ch).map(Number);
  if (parts.length !== 8 || !ref.FXFN[key] || !byName[light] || nums.some((v) => !Number.isFinite(v))) { bad++; continue; }
  Object.assign(ref.WX, variant === "storm" || variant === "turn" ? STORM : CALM);
  ref.setPrevWeather(variant === "turn" ? BEFORE : null);
  ref.setAmbient(variant === "day" ? 1 : 0);
  const want = ref.FXFN[key](parseFloat(t), byName[light]);
  let diff = 0;
  for (let i = 0; i < 5; i++) {
    const d = Math.abs(want[i] - parseFloat(ch[i]));
    diff = Number.isFinite(d) ? Math.max(diff, d) : Infinity;   // NaN from the reference fails too
  }
  const s = (stats[tag] = stats[tag] || { n: 0, off: 0, max: 0, worst: "" });
  s.n++;
  if (!(diff <= 0.01)) s.off++;
  if (!(diff <= s.max)) { s.max = diff; s.worst = light + " @ " + t; }
}

let ok = true;
for (const [key, s] of Object.entries(stats)) {
  const agree = 1 - s.off / s.n;
  const pass = agree >= 0.999;
  ok = ok && pass;
  console.log(`${pass ? "ok  " : "FAIL"} ${key.padEnd(10)} ${s.n} frames, ${(agree * 100).toFixed(2)} % within 0.01, max diff ${s.max.toExponential(2)} (${s.worst})`);
}
if (!rows.length) { console.log("FAIL no frames on stdin"); ok = false; }
if (bad) { console.log(`FAIL ${bad} malformed or non-finite row(s)`); ok = false; }
// The dump must be complete: its trailer, and every built effect in every variant, 7 lights x 4000 frames.
if (!trailer || Number(trailer.split(",")[1]) !== rows.length) {
  console.log(`FAIL the dump is incomplete (trailer ${trailer || "missing"}, ${rows.length} rows read)`); ok = false;
}
const WEATHER = ["season", "mist", "windcandles"];
for (const key of Object.keys(ref.FLOORS)) {
  const tags = [key, key + "@day"].concat(WEATHER.includes(key) ? [key + "@storm", key + "@turn"] : []);
  for (const tag of tags) {
    if (!stats[tag] || stats[tag].n !== 7 * 4000) { console.log(`FAIL ${tag}: ${stats[tag] ? stats[tag].n : 0} frames, expected ${7 * 4000}`); ok = false; }
  }
}
process.exit(ok ? 0 : 1);
