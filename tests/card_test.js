// Host test for ambient-light-card.js, with node and no browser: just enough of a
// DOM is stubbed for the card to load and build its markup.
//
//   node tests/card_test.js
//
// Checks:
//   - the card's name and icon, free text from the dashboard YAML, come out as
//     text in the markup, not as HTML;
//   - the brightness row reads a light at brightness 0 as 0 %, and a light with
//     no brightness (off) as 100 %.
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

let failures = 0;
const check = (cond, msg) => {
  if (!cond) {
    failures++;
    console.log("FAIL " + msg);
  }
};

// ---- load the card ----------------------------------------------------------------
const defined = {};
const stubEl = () => ({
  addEventListener() {}, setAttribute() {}, getAttribute() { return null; }, querySelector: () => stubEl(),
  querySelectorAll: () => [], getContext: () => null, classList: { toggle() {} }, style: {}, dataset: {},
});
class FakeShadowRoot {
  constructor() { this.html = ""; }
  set innerHTML(v) { this.html = v; }
  get innerHTML() { return this.html; }
  querySelector() { return stubEl(); }
  querySelectorAll() { return []; }
  getElementById() { return stubEl(); }
}
const sandbox = {
  HTMLElement: class {},
  customElements: { define: (name, cls) => { defined[name] = cls; }, get: (name) => defined[name], whenDefined: () => Promise.resolve() },
  document: { createElement: () => stubEl(), hidden: false },
  performance: { now: () => 0 },
  requestAnimationFrame: () => 0,
  cancelAnimationFrame() {},
  CustomEvent: class {},
  console,
};
sandbox.window = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(__dirname, "..", "ambient-light-card.js"), "utf8"), sandbox);
const Card = defined["ambient-light-card"];
check(Card, "ambient-light-card was not defined");

const build = (config) => {
  const card = new Card();
  card.shadowRoot = new FakeShadowRoot();
  card.setConfig(config);
  return card;
};

// ---- name and icon are text ------------------------------------------------------
{
  const name = `Den <img src=x onerror="alert(1)"> & 'co'`;
  const icon = `mdi:x" onmouseover="alert(2)`;
  const html = build({ entity: "light.den", name, icon }).shadowRoot.html;
  check(!html.includes("<img"), "the card name was parsed as HTML");
  check(html.includes("Den &lt;img src=x onerror=&quot;alert(1)&quot;&gt; &amp; &#39;co&#39;"),
        "the card name is not shown as its literal text");
  check(!html.includes(`onmouseover="alert(2)`), "the icon broke out of its attribute");
  check(html.includes(`icon="mdi:x&quot; onmouseover=&quot;alert(2)"`), "the icon attribute is not escaped");
  // The defaults are unchanged.
  const plain = build({ entity: "light.den" }).shadowRoot.html;
  check(plain.includes(`<div class="pri">Ambient lights</div>`), "the default name changed");
  check(plain.includes(`icon="mdi:television-ambient-light"`), "the default icon changed");
  if (!failures) console.log("card: name and icon are shown as text");
}

// ---- brightness 0 is 0 % ----------------------------------------------------------
{
  const before = failures;
  const level = (attrs) => {
    const card = build({ entity: "light.den" });
    card._hass = { states: { "light.den": { state: attrs.brightness == null ? "off" : "on", attributes: attrs } } };
    return card._level("brightness");
  };
  check(level({ brightness: 0 }) === 0, `brightness 0 read as ${level({ brightness: 0 })} %`);
  check(level({ brightness: 255 }) === 100, "brightness 255 is not 100 %");
  check(Math.abs(level({ brightness: 128 }) - 50.2) < 0.1, "brightness 128 is not ~50 %");
  check(level({ brightness: null }) === 100, "a light with no brightness (off) is not shown at 100 %");
  check(level({}) === 100, "a light without a brightness attribute is not shown at 100 %");
  if (failures === before) console.log("card: brightness 0 reads as 0 %, no brightness as 100 %");
}

if (failures) {
  console.log(`${failures} card check(s) failed`);
  process.exit(1);
}
console.log("all card checks passed");
