// Parity: the C++ effects against the reference model the preview page runs.
//
//   ./halloween_test --dump | node halloween_parity.js
//
// Reads the C++ frames from stdin and recomputes each one with
// halloween_reference.js ("@storm" frames under the simulated storm, "@day"
// frames with the full daylight lift). Run
// with TZ=America/New_York, as run_halloween_tests.sh does: the calendar
// effects read local time. The bulb works in float and the reference in double,
// so frames agree to ~1e-4 rather than bit for bit; a sample that lands within
// a few microseconds of an event edge (a gutter starting, a strike) can fall on
// the other side of it. Passes when 99.9 % of samples agree within 0.01 on
// every channel.
"use strict";
const ref = require("./halloween_reference.js");

const rows = require("fs").readFileSync(0, "utf8").trim().split("\n").slice(1);
const byName = {};
ref.SELF.forEach((b) => { byName[b.id] = b; });

// The storm the C++ test simulates for "@storm" frames.
const CALM = Object.assign({}, ref.WX);
const STORM = { on: true, windMph: 30, gustMph: 45, lullMph: 5, dirDeg: 225, rainInH: 0.6, tempF: 38, dewF: 37 };

const stats = {};
for (const line of rows) {
  const [tag, light, t, ...ch] = line.split(",");
  const [key, variant] = tag.split("@");
  Object.assign(ref.WX, variant === "storm" ? STORM : CALM);
  ref.setAmbient(variant === "day" ? 1 : 0);
  const want = ref.FXFN[key](parseFloat(t), byName[light]);
  let diff = 0;
  for (let i = 0; i < 5; i++) diff = Math.max(diff, Math.abs(want[i] - parseFloat(ch[i])));
  const s = (stats[tag] = stats[tag] || { n: 0, off: 0, max: 0, worst: "" });
  s.n++;
  if (diff > 0.01) s.off++;
  if (diff > s.max) { s.max = diff; s.worst = light + " @ " + t; }
}

let ok = true;
for (const [key, s] of Object.entries(stats)) {
  const agree = 1 - s.off / s.n;
  const pass = agree >= 0.999;
  ok = ok && pass;
  console.log(`${pass ? "ok  " : "FAIL"} ${key.padEnd(10)} ${s.n} frames, ${(agree * 100).toFixed(2)} % within 0.01, max diff ${s.max.toExponential(2)} (${s.worst})`);
}
if (!rows.length) { console.log("FAIL no frames on stdin"); ok = false; }
process.exit(ok ? 0 : 1);
