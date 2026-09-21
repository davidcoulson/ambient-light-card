// ambient-light-card - compact control for a room's ambient / accent lights,
// sized for half of an NSPanel Pro. https://github.com/davidcoulson/ambient-light-card
//
//   type: custom:ambient-light-card
//   entity: light.home_theater_accent_lights        # the room's light group
//   speed: input_number.home_theater_accent_speed   # 0-255, 128 = normal
//   intensity: input_number.home_theater_accent_intensity
//   name: Ambient lights                            # optional
//   icon: mdi:television-ambient-light              # optional
//   favorites: [2D Pacifica, Candle Flicker, ...]   # optional; up to 11 effect tiles
//   effects: [...]                                  # optional; the "more" list, default = the light's list
//
// Brightness, speed and intensity are four-step sliders: tap a step or drag
// along the bar. Brightness drives the light group; speed and intensity set
// the room's helpers, which the lights import (common/accent_fx_controls.yaml).
// The top-right glow is a live preview of the effect's feel.
//
// Everything is configurable from the dashboard editor (getConfigElement).

const STEPS = {
  brightness: { values: [20, 50, 80, 100], icons: ["mdi:lightbulb-on-20", "mdi:lightbulb-on-50", "mdi:lightbulb-on-80", "mdi:lightbulb-on"] },
  speed: { values: [64, 128, 192, 255], icons: ["mdi:tortoise", "mdi:walk", "mdi:car-sports", "mdi:rocket-launch"] },
  intensity: { values: [64, 128, 192, 255] },
};
const INTENSITY_ICONS = {
  water: ["mdi:wave", "mdi:waves", "mdi:waves-arrow-up", "mdi:tsunami"],
  flame: ["mdi:candle", "mdi:fire", "mdi:campfire", "mdi:volcano"],
  weather: ["mdi:weather-sunny", "mdi:weather-partly-cloudy", "mdi:weather-windy", "mdi:weather-lightning"],
};
const HIDDEN_EFFECTS = /^(None|Calibrate:.*)$/;
const MAX_FAVORITES = 11;   // plus the "more" tile = 4 rows of 3
// Favourite tiles when the card config names none: one effect from each of
// these groups (the first the light actually has), up to five.
const DEFAULT_FAVORITES = [["2D Pacifica", "Pacifica"], ["2D Aurora (Solar Storm)", "Aurora (Solar Storm)", "Aurora"],
                           ["Candle Flicker"], ["2D Hearth", "Ember Ring", "Fireplace Embers"], ["Heartbeat Pulse"], ["Metronome"]];

// ---- effect tiles ---------------------------------------------------------
// Each tile is a small colour field that conveys how the effect FEELS - not a
// picture of it. field(u, v, t, I) -> [r, g, b], u/v 0..1 across the tile.
const cl = (x) => (x < 0 ? 0 : x > 1 ? 1 : x);
const mix = (a, b, k) => [a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k, a[2] + (b[2] - a[2]) * k];
function hsl(h, s, l) {
  h = ((h % 1) + 1) % 1;
  const a = s * Math.min(l, 1 - l), f = (n) => { const k = (n + h * 12) % 12; return l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1)); };
  return [255 * f(0), 255 * f(8), 255 * f(4)];
}
const SEA = {
  ocean: [[14, 70, 130], [40, 170, 200], [225, 248, 250]],
  lagoon: [[20, 110, 120], [90, 210, 200], [235, 250, 245]],
  storm: [[20, 40, 65], [90, 125, 150], [230, 235, 240]],
  deep: [[5, 20, 80], [30, 80, 170], [150, 190, 240]],
};
const SKY = {
  aurora: { h: 0.36, span: 0.22, s: 0.75, l: 0.6 },
  solar: { h: 0.36, span: 0.38, s: 0.75, l: 0.6 },
  pastel: { h: 0.45, span: 0.4, s: 0.5, l: 0.75 },
  redsky: { h: 0.97, span: 0.1, s: 0.8, l: 0.55 },
};
// Per-cell pseudo-random 0..1, stable for a tile.
const cellRand = (i, j, k) => { const x = Math.sin(i * 127.1 + j * 311.7 + k * 74.7) * 43758.5453; return x - Math.floor(x); };

// Tile designs for the families beyond ocean / aurora / fire / pulse. Each is
// a feel, not a picture: twinkles are points of light, chases are a moving
// streak, storms are dark with flashes, and so on. Checked before the family
// match, so "Fireflies" twinkles rather than burns.
function extraField(e, has) {
  // Points of light that twinkle in and out.
  if (has("twinkle", "fairy", "fireflies", "firefly", "lullaby", "sparkle", "stars", "milky way", "bioluminescence", "confetti", "swarm")) {
    const P = has("firefl") ? [[8, 14, 6], [220, 240, 90]] : has("lullaby") ? [[30, 26, 50], [250, 220, 240]]
      : has("biolum") ? [[2, 18, 30], [80, 240, 230]] : has("milky") ? [[10, 10, 30], [230, 225, 255]]
      : [[16, 20, 44], [255, 250, 235]];
    const multi = has("confetti", "disco");
    return (u, v, t, I) => {
      const gx = 7, gy = 5, i = Math.floor(u * gx), j = Math.floor(v * gy);
      const du = u * gx - i - 0.5, dv = v * gy - j - 0.5, r = cellRand(i, j, 1);
      const tw = Math.pow(Math.max(0, Math.sin(t * (0.8 + r * 1.6) + r * 40)), 10 - 4 * Math.min(I, 1.5));
      const dot = Math.max(0, 1 - Math.hypot(du, dv) * 2.6) * tw;
      const c = multi ? hsl(cellRand(i, j, 2) + t * 0.02, 0.8, 0.6) : P[1];
      return mix(mix(P[0], c, 0.12 + 0.06 * Math.sin(u * 3 + t * 0.3)), c, cl(dot * 1.6));
    };
  }
  // Colour cycles: the hue sweeps across the tile.
  if (has("rainbow", "color loop", "color drift", "pinwheel", "unison random")) {
    const slow = has("drift") ? 0.3 : 1;
    return (u, v, t, I) => hsl(u * (0.35 + 0.25 * Math.min(I, 2)) + v * 0.1 - t * 0.12 * slow, 0.8, 0.55);
  }
  // Slow blobs: plasma, lava, clouds, metaballs, noise.
  if (has("plasma", "lava", "metaball", "noise", "clouds", "black hole", "flood")) {
    const pal = has("lava") ? [[90, 10, 30], [255, 120, 40]] : has("cloud") ? [[70, 90, 120], [220, 228, 240]]
      : has("black hole") ? [[4, 2, 10], [150, 80, 220]] : null;
    return (u, v, t, I) => {
      const n = 0.5 + 0.5 * Math.sin(u * 4 + Math.sin(v * 3 + t * 0.7) * 1.5 + t * 0.4) * Math.cos(v * 4 - t * 0.5 + Math.sin(u * 2 - t * 0.3));
      const k = cl(0.5 + (n - 0.5) * (0.6 + 0.5 * Math.min(I, 2)));
      return pal ? mix(pal[0], pal[1], k) : hsl(0.75 + 0.35 * k + t * 0.02, 0.7, 0.3 + 0.35 * k);
    };
  }
  // Storms and strobes: mostly dark, with sharp flashes.
  if (has("lightning", "strobe", "police", "tv simulator", "flicker") && !has("candle", "radial")) {
    if (has("police"))
      return (u, v, t, I) => { const p = (t * 1.5) % 1, left = p < 0.5, on = (p % 0.5) < 0.08 || ((p % 0.5) > 0.14 && (p % 0.5) < 0.22);
        return (u < 0.5) === left && on ? (left ? [230, 30, 40] : [40, 70, 255]) : [18, 18, 30]; };
    if (has("tv"))
      return (u, v, t, I) => { const s = Math.floor(t * 3), c = cellRand(Math.floor(u * 3), Math.floor(v * 2), s);
        return mix([40, 60, 110], [190, 210, 255], cl(0.3 + 0.6 * c * Math.min(I, 1.5))); };
    const warm = has("neon", "flicker") && !has("lightning");
    return (u, v, t, I) => {
      const slot = Math.floor(t / 2.2), w = t - slot * 2.2, at = cellRand(slot, 0, 3) * 1.4;
      const fl = w > at && w < at + 0.4 ? Math.exp(-(w - at) * 12) * (0.6 + 0.4 * Math.sin((w - at) * 60)) : 0;
      const k = cl(fl * Math.min(I, 1.6) * (0.7 + 0.3 * (1 - v)));
      return warm ? mix([60, 20, 50], [255, 90, 200], cl(0.6 + k - (fl ? 0 : 0.35 * (cellRand(slot, 1, 4) > 0.7)))) : mix([22, 26, 48], [235, 240, 255], k);
    };
  }
  // Movers: a streak with a trail travels across.
  if (has("chase", "comet", "meteor", "scanner", "sinelon", "lighthouse", "radar", "pac-man", "wave", "canon", "bouncing", "chunchun", "halloween")) {
    const hue = has("halloween") ? 0.08 : has("pac-man") ? 0.15 : has("radar") ? 0.33 : has("lighthouse") ? 0.12 : 0.58;
    const pingpong = has("scanner", "bouncing");
    return (u, v, t, I) => {
      let p = (t * 0.45) % 1;
      if (pingpong) p = p < 0.5 ? p * 2 : 2 - p * 2;
      let d = u - (p * 1.3 - 0.15);
      if (!pingpong && d < 0) d = 9;
      const head = Math.exp(-Math.abs(d) * 18), trail = d < 0 ? 0 : Math.exp(-d * (5 - 1.5 * Math.min(I, 2)));
      return mix([14, 16, 26], hsl(hue, 0.85, 0.6), cl(Math.max(head, trail * 0.7)));
    };
  }
  // Ripples: rings spreading from the middle.
  if (has("ripple", "shockwave", "shore break", "bubbles", "rainfall", "fireworks", "caustics")) {
    const hue = has("fireworks") ? null : has("rainfall") ? 0.6 : 0.52;
    return (u, v, t, I) => {
      const d = Math.hypot(u - 0.5, (v - 0.5) * 0.74), p = (t * 0.5) % 1;
      const ring = Math.exp(-Math.pow((d - p * 0.7) * 14, 2)) * (1 - p);
      const base = hue === null ? [10, 8, 24] : hsl(hue, 0.5, 0.2);
      const c = hue === null ? hsl(Math.floor(t * 0.5) * 0.37, 0.85, 0.62) : hsl(hue, 0.6, 0.75);
      return mix(base, c, cl(ring * 1.4 * Math.min(I, 1.6)));
    };
  }
  // Sky gradients: sunrise, sunset, moonrise.
  if (has("sunrise", "sunset", "moon")) {
    const moon = has("moon");
    return (u, v, t, I) => {
      const s = 0.5 + 0.5 * Math.sin(t * 0.25);
      return moon ? mix([10, 14, 40], [170, 185, 230], cl((1 - v) * 0.5 * s + 0.15))
        : mix(mix([40, 30, 90], [255, 120, 60], cl(v * 1.2 - 0.1 + s * 0.3)), [255, 220, 140], cl((v - 0.6) * 2 * s));
    };
  }
  // Rhythm: bars pulsing at their own rates.
  if (has("polyrhythm", "downbeat", "clock", "chime", "game of life")) {
    const life = has("game of life");
    return (u, v, t, I) => {
      if (life) { const i = Math.floor(u * 7), j = Math.floor(v * 5), on = cellRand(i, j, Math.floor(t * 1.5)) > 0.55;
        return on ? [140, 230, 150] : [12, 30, 18]; }
      const bar = Math.floor(u * 3), rate = [1, 1.5, 2][bar], p = (t * rate) % 1;
      return mix([30, 26, 20], hsl(0.1 + bar * 0.08, 0.7, 0.62), cl(Math.exp(-p * 9) * Math.min(I, 1.4)));
    };
  }
  // Soft, slow breathing: fog, glow, breathing, gentle, whisper.
  if (has("fog", "soft glow", "breathing", "gentle", "whisper", "hush", "sunrise")) {
    const pal = has("fog") ? [[60, 70, 90], [170, 180, 200]] : has("soft glow") ? [[120, 80, 50], [240, 200, 150]] : [[40, 60, 90], [150, 190, 230]];
    return (u, v, t, I) => mix(pal[0], pal[1], cl(0.5 + 0.35 * Math.min(I, 1.5) * Math.sin(t * 0.5 + u * 1.5 - v)));
  }
  return null;
}

function nameHash(s) { let h = 2166136261; for (const ch of s) { h ^= ch.charCodeAt(0); h = Math.imul(h, 16777619); } return (h >>> 0) / 4294967296; }
function tileField(effect) {
  const e = (effect || "").toLowerCase();
  const has = (...w) => w.some((x) => e.includes(x));
  const extra = extraField(e, has);
  if (extra) return extra;
  const f = family(effect);
  if (f.kind === "wave") {
    const c = SEA[f.pal] || SEA.ocean;
    return (u, v, t, I) => {
      const sw = Math.min(I, 1.6);
      const w = cl(0.5 + sw * (0.28 * Math.sin(u * 5 + v * 2 - t * 0.9) + 0.18 * Math.sin(u * 3 - v * 4 + t * 0.6) + 0.1 * Math.sin(u * 9 + t * 1.4)));
      const foam = Math.pow(Math.max(0, Math.sin(u * 7 + v * 3 - t * 1.3) * Math.sin(u * 4 - v * 5 + t * 0.8)), 6) * Math.min(I, 2);
      return mix(mix(c[0], c[1], w), c[2], cl(foam));
    };
  }
  if (f.kind === "aurora") {
    const k = SKY[f.pal] || SKY.aurora;
    return (u, v, t, I) => {
      const s = Math.sin((u * 2.4 + 0.35 * Math.sin(t * 0.35 + v * 2.2)) * Math.PI + t * 0.45);
      const g = cl(0.3 + 0.7 * Math.pow(Math.max(0, s), 1 + 0.6 * I));
      const h = k.h + k.span * u + 0.1 * v + 0.08 * Math.sin(t * 0.2);
      return mix(hsl(h + 0.12, k.s * 0.75, k.l * 0.45), hsl(h, k.s, k.l), g);
    };
  }
  if (f.kind === "flame" && e.includes("candle"))
    return (u, v, t, I) => {
      const fl = 0.78 + Math.min(I, 1.6) * (0.12 * Math.sin(t * 8.3) + 0.06 * Math.sin(t * 21 + 1) + 0.04 * Math.sin(t * 37));
      const gut = (t % 7) < 0.5 * Math.min(I, 2) ? 0.6 : 1;
      return mix([150, 45, 5], [255, 190, 90], cl((0.55 + 0.45 * v) * fl * gut + 0.05 * Math.sin(u * 6 + t * 3)));
    };
  if (f.kind === "flame")
    return (u, v, t, I) => {
      const n = 0.5 + Math.min(I, 1.6) * (0.22 * Math.sin(u * 9 + t * 3.1 - v * 6) + 0.18 * Math.sin(u * 5 - t * 2.2 + v * 8) + 0.12 * Math.sin(u * 14 + v * 11 - t * 5));
      const flare = (t % 3.5) < 0.4 ? 0.2 * Math.min(I, 2) : 0;
      return mix([120, 18, 0], [255, 150, 40], cl(n * (0.55 + 0.5 * v) + flare));
    };
  if (f.kind === "heart")
    return (u, v, t, I) => {
      const p = t % 1, W = 1 - 0.3 * Math.max(0, I - 1);
      const b = cl(Math.exp(-Math.pow((p - 0.08) / (0.05 * W), 2)) + 0.65 * Math.exp(-Math.pow((p - 0.26) / (0.045 * W), 2)));
      return mix([110, 8, 24], [245, 55, 80], cl(b * Math.min(I, 1.4) * (1 - Math.hypot(u - 0.5, (v - 0.5) * 0.75) * 0.6)));
    };
  if (f.kind === "tick")
    return (u, v, t, I) => {
      const w = t % 1, l = w < 0.35 ? Math.exp(-10 * (1 + Math.max(0, I - 1)) * w) : 0;
      const side = Math.floor(t) % 2 ? u : 1 - u;
      return mix([95, 82, 64], [255, 240, 210], cl(l * Math.min(I, 1.3) * (0.7 + 0.3 * side)));
    };
  // Anything else: a gentle drift between two colours picked from its name, so
  // every effect gets its own recognisable tile.
  const h0 = nameHash(effect || "none");
  return (u, v, t, I) => {
    const k = cl(0.5 + 0.35 * Math.sin(u * 3 + v * 2 + t * 0.6) * Math.min(I, 1.5));
    return mix(hsl(h0, 0.55, 0.35), hsl(h0 + 0.15, 0.7, 0.62), k);
  };
}

// Preview palettes, dark to bright.
const PAL = {
  ocean: [[0, 29, 76], [8, 66, 74], [22, 126, 156], [60, 170, 190], [100, 206, 212], [226, 246, 247]],
  lagoon: [[0, 50, 60], [10, 110, 120], [40, 170, 170], [110, 215, 205], [200, 245, 235]],
  storm: [[5, 15, 35], [20, 45, 70], [50, 85, 110], [110, 140, 160], [220, 230, 235]],
  deep: [[0, 5, 40], [0, 20, 90], [10, 50, 140], [40, 90, 180], [120, 170, 230]],
  aurora: [[20, 140, 40], [60, 210, 90], [40, 180, 170], [40, 120, 210], [110, 70, 200]],
  solar: [[20, 140, 40], [60, 210, 90], [40, 180, 170], [40, 120, 210], [110, 70, 200], [190, 60, 170]],
  pastel: [[120, 200, 190], [170, 220, 200], [200, 180, 230], [240, 190, 210], [250, 230, 200]],
  redsky: [[90, 10, 20], [170, 30, 30], [220, 70, 40], [240, 120, 60], [120, 40, 110]],
  flame: [[180, 40, 0], [220, 74, 4], [255, 120, 14], [255, 160, 40], [255, 215, 96]],
  heart: [[150, 6, 20], [200, 10, 30], [235, 40, 60]],
  tick: [[120, 110, 90], [255, 235, 200]],
};

function family(effect) {
  const e = (effect || "").toLowerCase();
  if (e.includes("pacifica") || e.includes("tide") || e.includes("shore") || e.includes("caustics") || e.includes("aquarium"))
    return { kind: "wave", pal: e.includes("lagoon") ? "lagoon" : e.includes("storm") ? "storm" : e.includes("deep") ? "deep" : "ocean", icons: "water" };
  if (e.includes("aurora") || e.includes("northern"))
    return { kind: "aurora", pal: e.includes("pastel") ? "pastel" : e.includes("red sky") ? "redsky" : e.includes("solar") ? "solar" : "aurora", icons: "weather" };
  if (e.includes("candle") || e.includes("ember") || e.includes("hearth") || e.includes("radial flicker")
      || (e.includes("fire") && !e.includes("firefl") && !e.includes("firework")))
    return { kind: "flame", pal: "flame", icons: "flame" };
  if (e.includes("heartbeat")) return { kind: "heart", pal: "heart", icons: "weather" };
  if (e.includes("metronome")) return { kind: "tick", pal: "tick", icons: "weather" };
  return { kind: "glow", pal: null, icons: "weather" };
}

function palAt(pal, v) {
  v = Math.max(0, Math.min(0.9999, v)) * (pal.length - 1);
  const i = Math.floor(v), f = v - i, a = pal[i], b = pal[i + 1];
  return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f];
}

class AmbientLightCard extends HTMLElement {
  setConfig(config) {
    if (!config.entity) throw new Error("entity is required");
    this._config = config;
    this._pending = {};   // optimistic step index per row, until HA confirms
    if (this.shadowRoot) this._build();
  }

  static getConfigElement() { return document.createElement("ambient-light-card-editor"); }

  static getStubConfig() {
    return { entity: "light.home_theater_accent_lights", speed: "input_number.home_theater_accent_speed", intensity: "input_number.home_theater_accent_intensity" };
  }

  getCardSize() { return 4; }
  getGridOptions() { return { columns: 6, rows: 4, min_columns: 6 }; }

  connectedCallback() {
    if (!this.shadowRoot) {
      this.attachShadow({ mode: "open" });
      this._build();
    }
    this._running = true;
    this._last = performance.now();
    this._raf = requestAnimationFrame((t) => this._frame(t));
  }

  disconnectedCallback() {
    this._running = false;
    cancelAnimationFrame(this._raf);
  }

  set hass(hass) {
    this._hass = hass;
    if (this._root) this._update();
  }

  _build() {
    const c = this._config;
    if (!c) return;
    this.shadowRoot.innerHTML = `
      <style>
        :host { --alc-accent: var(--state-light-active-color, #ff9800); }
        ha-card { position: relative; overflow: hidden; height: 100%; box-sizing: border-box; }
        canvas { position: absolute; inset: 0; width: 100%; height: 100%; pointer-events: none; z-index: 0; }
        .in { position: relative; z-index: 1; padding: 12px 14px 8px; }
        .hd { display: flex; align-items: center; gap: 10px; }
        /* Tile-card style toggle: the icon is the on/off button. */
        .ib { flex: none; width: 40px; height: 40px; border-radius: 50%; border: 0; padding: 0; cursor: pointer;
              display: flex; align-items: center; justify-content: center; position: relative; overflow: hidden;
              color: var(--alc-icon, var(--alc-accent)); background: none; transition: color .2s, transform .1s;
              -webkit-tap-highlight-color: transparent; }
        .ib::before { content: ""; position: absolute; inset: 0; background: currentColor; opacity: .2; transition: opacity .2s; }
        .ib ha-icon { position: relative; --mdc-icon-size: 24px; }
        .ib:active { transform: scale(.92); }
        .ib:focus-visible { outline: 2px solid var(--alc-accent); outline-offset: 2px; }
        .ib.off { color: var(--secondary-text-color); }
        .ib.off::before { opacity: .12; }
        .ib:disabled { cursor: default; opacity: .5; }
        .tx { flex: 1; min-width: 0; }
        .pri { font-size: var(--card-primary-font-size, 16px); font-weight: 500; color: var(--primary-text-color); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .sec { font-size: var(--card-secondary-font-size, 14px); color: var(--secondary-text-color); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .fav { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; margin-top: 10px; }
        .t { position: relative; aspect-ratio: 1.35; border-radius: 10px; overflow: hidden; border: 0; padding: 0; cursor: pointer;
             background: rgba(127,127,127,.15); -webkit-tap-highlight-color: transparent; transition: transform .1s; }
        .t canvas { position: absolute; inset: 0; width: 100%; height: 100%; display: block; }
        .t:active { transform: scale(.95); }
        .t.on { outline: 2px solid var(--alc-accent); outline-offset: 2px; }
        .t:focus-visible { outline: 2px solid var(--alc-accent); outline-offset: 2px; }
        .more { display: flex; align-items: center; justify-content: center; color: var(--secondary-text-color); }
        .more ha-icon { position: relative; --mdc-icon-size: 22px; pointer-events: none; }
        .more.on ha-icon { color: #fff; filter: drop-shadow(0 1px 2px rgba(0,0,0,.6)); }
        /* The native picker, invisible over the "more" tile: the panel opens its own full-screen list. */
        .more select { position: absolute; inset: 0; width: 100%; height: 100%; opacity: 0; cursor: pointer; font-size: 16px; }
        .stp { position: relative; height: 46px; margin-top: 6px; touch-action: none; cursor: pointer; user-select: none; }
        .trk { position: absolute; left: 6%; right: 6%; top: 10px; height: 8px; border-radius: 4px; background: rgba(127,127,127,0.35); }
        .tk { position: absolute; top: 6px; width: 2px; height: 16px; margin-left: -1px; border-radius: 1px; background: rgba(127,127,127,0.55); }
        .kn { position: absolute; top: 3px; width: 22px; height: 22px; margin-left: -11px; border-radius: 50%;
              background: var(--alc-accent); box-shadow: 0 1px 3px rgba(0,0,0,.35); transition: left .18s ease; }
        .ics { position: absolute; left: 0; right: 0; top: 27px; display: grid; grid-template-columns: repeat(4, 1fr); }
        .ics ha-icon { justify-self: center; --mdc-icon-size: 18px; color: var(--secondary-text-color); opacity: .7; transition: color .15s, opacity .15s; }
        .ics ha-icon.on { color: var(--alc-accent); opacity: 1; }
        .dim .stp, .dim .fav { opacity: .45; }
      </style>
      <ha-card>
        <canvas width="64" height="84"></canvas>
        <div class="in">
          <div class="hd">
            <button class="ib" id="ib" aria-label="Turn on or off"><ha-icon icon="${c.icon || "mdi:television-ambient-light"}"></ha-icon></button>
            <div class="tx"><div class="pri">${c.name || "Ambient lights"}</div><div class="sec" id="sec"></div></div>
          </div>
          ${["brightness", "speed", "intensity"].filter((k) => k === "brightness" || c[k]).map((k) => `
            <div class="stp" data-k="${k}" role="slider" aria-label="${k}" aria-valuemin="0" aria-valuemax="3">
              <div class="trk"></div>
              ${[0, 1, 2, 3].map((j) => `<div class="tk" style="left:${12.5 + j * 25}%"></div>`).join("")}
              <div class="kn"></div>
              <div class="ics">${[0, 1, 2, 3].map(() => "<ha-icon></ha-icon>").join("")}</div>
            </div>`).join("")}
          <div class="fav" id="fav"></div>
        </div>
      </ha-card>`;
    const r = this.shadowRoot;
    this._root = r.querySelector(".in");
    this._canvas = r.querySelector("canvas");
    this._ctx = this._canvas.getContext("2d");
    const toggle = (e) => {
      e.stopPropagation();
      this._call("light", this._on ? "turn_off" : "turn_on", { entity_id: c.entity });
    };
    r.getElementById("ib").addEventListener("click", toggle);
    this._favKey = null;
    r.querySelectorAll(".stp").forEach((row) => this._bindRow(row));
    this._effectsKey = null;
    if (this._hass) this._update();
  }

  _bindRow(row) {
    let down = false;
    const pick = (e) => {
      const b = row.getBoundingClientRect();
      const idx = Math.max(0, Math.min(3, Math.floor(((e.clientX - b.left) / b.width) * 4)));
      this._pending[row.dataset.k] = idx;
      this._paintRow(row, idx);
    };
    row.addEventListener("pointerdown", (e) => { down = true; row.setPointerCapture(e.pointerId); pick(e); });
    row.addEventListener("pointermove", (e) => { if (down) pick(e); });
    const up = () => {
      if (!down) return;
      down = false;
      this._commit(row.dataset.k, this._pending[row.dataset.k]);
    };
    row.addEventListener("pointerup", up);
    row.addEventListener("pointercancel", up);
  }

  _commit(k, idx) {
    const c = this._config;
    const v = STEPS[k].values[idx];
    if (k === "brightness") this._call("light", "turn_on", { entity_id: c.entity, brightness_pct: v });
    else this._call("input_number", "set_value", { entity_id: c[k], value: v });
    // Drop the optimistic value once HA has had a moment to report back.
    clearTimeout(this._pendT);
    this._pendT = setTimeout(() => { this._pending = {}; this._update(); }, 2500);
  }

  _call(domain, service, data) {
    if (this._hass) this._hass.callService(domain, service, data);
  }

  _state(id) { return id && this._hass ? this._hass.states[id] : undefined; }

  _nearest(values, v) {
    let best = 0;
    values.forEach((x, i) => { if (Math.abs(x - v) < Math.abs(values[best] - v)) best = i; });
    return best;
  }

  _level(k) {
    // Current value, 0-255 scale for speed/intensity and percent for brightness.
    const st = this._state(k === "brightness" ? this._config.entity : this._config[k]);
    if (!st) return k === "brightness" ? 100 : 128;
    if (k === "brightness") return st.attributes.brightness ? (st.attributes.brightness / 255) * 100 : 100;
    const n = parseFloat(st.state);
    return isNaN(n) ? 128 : n;
  }

  _paintRow(row, idx) {
    const left = 12.5 + idx * 25;
    row.querySelector(".kn").style.left = left + "%";
    row.querySelectorAll(".tk").forEach((t, j) => (t.style.visibility = j === idx ? "hidden" : "visible"));
    row.querySelectorAll(".ics ha-icon").forEach((ic, j) => ic.classList.toggle("on", j === idx));
    row.setAttribute("aria-valuenow", idx);
  }

  _update() {
    const c = this._config, r = this.shadowRoot;
    const st = this._state(c.entity);
    const on = st && st.state === "on";
    const effect = on && st.attributes.effect && st.attributes.effect !== "None" ? st.attributes.effect : null;
    this._fam = family(effect);
    this._on = on;
    this._effect = effect;
    this._rgb = st && st.attributes.rgb_color;

    r.getElementById("sec").textContent = !st ? "Unavailable" : on ? (effect ? `On · ${effect}` : "On") : "Off";
    const ib = r.getElementById("ib");
    ib.classList.toggle("off", !on);
    ib.disabled = !st || st.state === "unavailable";
    ib.setAttribute("aria-pressed", on ? "true" : "false");
    // Like the tile card, the icon takes the light's colour when it has one.
    const rgb = on && st.attributes.rgb_color;
    if (rgb) this.style.setProperty("--alc-icon", `rgb(${rgb.join(",")})`);
    else this.style.removeProperty("--alc-icon");
    this._root.classList.toggle("dim", !on);

    this._renderTiles(st, effect);

    const icons = { ...STEPS, intensity: { ...STEPS.intensity, icons: INTENSITY_ICONS[this._fam.icons] } };
    r.querySelectorAll(".stp").forEach((row) => {
      const k = row.dataset.k;
      row.querySelectorAll(".ics ha-icon").forEach((ic, j) => {
        if (ic.getAttribute("icon") !== icons[k].icons[j]) ic.setAttribute("icon", icons[k].icons[j]);
      });
      const idx = this._pending[k] !== undefined ? this._pending[k] : this._nearest(STEPS[k].values, this._level(k));
      this._paintRow(row, idx);
    });
  }

  // ---- effect tiles --------------------------------------------------------
  _renderTiles(st, effect) {
    const c = this._config, r = this.shadowRoot;
    const all = (c.effects || (st && st.attributes.effect_list) || []).filter((e) => !HIDDEN_EFFECTS.test(e));
    // Configured favourites: up to 11. Defaults: five, so they make two tidy rows with the "more" tile.
    const favs = c.favorites ? c.favorites.slice(0, MAX_FAVORITES)
      : DEFAULT_FAVORITES.map((g) => g.find((e) => all.includes(e))).filter(Boolean).slice(0, 5);
    const key = favs.join("|") + "#" + all.join("|");
    const fav = r.getElementById("fav");
    const esc = (e) => e.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/"/g, "&quot;");
    if (key !== this._favKey) {
      this._favKey = key;
      fav.innerHTML = favs.map((e) => `<button class="t" data-fx="${esc(e)}" aria-label="${esc(e)}"><canvas width="36" height="26"></canvas></button>`).join("")
        + `<div class="t more" aria-label="More effects"><canvas width="36" height="26"></canvas><ha-icon icon="mdi:dots-horizontal"></ha-icon>`
        + `<select aria-label="All effects"><option value="" disabled>Effects</option><option value="None">No effect</option>`
        + all.map((e) => `<option value="${esc(e)}">${esc(e)}</option>`).join("") + `</select></div>`;
      fav.querySelectorAll("button.t").forEach((b) => b.addEventListener("click", () => {
        this._call("light", "turn_on", { entity_id: c.entity, effect: b.dataset.fx });
      }));
      const sel = fav.querySelector("select");
      sel.addEventListener("change", () => {
        if (sel.value) this._call("light", "turn_on", { entity_id: c.entity, effect: sel.value });
      });
      this._tiles = [...fav.querySelectorAll(".t")].map((el) => {
        const cv = el.querySelector("canvas"), ctx = cv.getContext("2d");
        return { el, ctx, img: ctx.createImageData(36, 26), fx: el.dataset.fx || null };
      });
      this._tileState = null;
    }
    // The running effect's tile is outlined; one not among the favourites
    // shows on the "more" tile instead.
    const inFav = effect && favs.includes(effect);
    this._tiles.forEach((tl) => {
      if (!tl.fx) tl.field = tileField(effect && !inFav ? effect : null), tl.live = !!(effect && !inFav);
      else tl.field = tl.field || tileField(tl.fx), tl.live = tl.fx === effect;
      tl.el.classList.toggle("on", tl.live);
      if (!tl.fx) tl.el.classList.toggle("blank", !tl.live);
    });
    const sel = fav.querySelector("select");
    sel.value = "";
    const state = (effect || "") + "|" + this._on;
    if (state !== this._tileState) {
      this._tileState = state;
      this._tiles.forEach((tl) => { if (tl.fx || tl.live) this._paintTile(tl, 1.08, 1); else tl.ctx.clearRect(0, 0, 36, 26); });
    }
  }

  _paintTile(tl, t, I) {
    const d = tl.img.data;
    for (let y = 0; y < 26; y++)
      for (let x = 0; x < 36; x++) {
        const c = tl.field(x / 35, y / 25, t, I), o = (y * 36 + x) * 4;
        d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
      }
    tl.ctx.putImageData(tl.img, 0, 0);
  }

  // ---- feel preview -------------------------------------------------------
  _frame(now) {
    if (!this._running) return;
    this._raf = requestAnimationFrame((t) => this._frame(t));
    if (now - (this._drawn || 0) < 33 || document.hidden || !this._ctx) return;   // ~30 fps
    this._drawn = now;
    const dt = Math.min(0.1, (now - this._last) / 1000);
    this._last = now;

    const spd = this._config.speed ? Math.pow(2, (this._stepVal("speed") - 128) / 64) : 1;
    const I = this._config.intensity ? this._stepVal("intensity") / 128 : 1;
    const bri = this._stepVal("brightness") / 100;
    this._t = (this._t || 0) + dt * spd;
    // Fade in/out with the light rather than snapping.
    this._fade = (this._fade || 0) + ((this._on ? 1 : 0) - (this._fade || 0)) * Math.min(1, dt * 4);
    this._draw(this._t, I, bri * this._fade);
    if (this._on && this._tiles) this._tiles.forEach((tl) => { if (tl.live) this._paintTile(tl, this._t, I); });
  }

  _stepVal(k) {
    if (this._pending[k] !== undefined) return STEPS[k].values[this._pending[k]];
    return this._level(k);
  }

  _draw(t, I, level) {
    const ctx = this._ctx, W = this._canvas.width, H = this._canvas.height;
    if (level < 0.01) { ctx.clearRect(0, 0, W, H); return; }
    const img = ctx.createImageData(W, H), d = img.data;
    const f = this._fam || family(null);
    const pal = f.pal ? PAL[f.pal] : null;
    const base = this._rgb || [255, 160, 60];
    const Iu = Math.min(1, I), X = Math.max(0, Math.min(1, I - 1));
    // Effect-wide terms for the pulse-type effects.
    let pulse = 0;
    if (f.kind === "heart") {
      const ph = t % 1;
      const w = 1 - 0.35 * X;
      pulse = Math.min(1, Math.exp(-Math.pow((ph - 0.08) / (0.05 * w), 2)) + (0.6 + 0.3 * X) * Math.exp(-Math.pow((ph - 0.22) / (0.04 * w), 2)));
    } else if (f.kind === "tick") {
      const w = t % 1;
      pulse = w < 0.25 ? Math.exp(-14 * (1 + X) * w) : 0;
    }
    const flick = 0.62 + 0.24 * Math.sin(t * 9) + 0.14 * Math.sin(t * 23 + 1);

    for (let py = 0; py < H; py++) {
      const v = py / (H - 1);
      for (let px = 0; px < W; px++) {
        const u = px / (W - 1);
        // Corner wedge: full in the top-right, gone toward the left and down.
        // Falls off faster downward so it stays behind the header and sliders,
        // clear of the effect tiles.
        let q = 1 - ((1 - u) + v * 2.2) / 1.05;
        q = q < 0 ? 0 : q > 1 ? 1 : q;
        const fade = q * q * (3 - 2 * q);
        if (fade <= 0) continue;
        let val, rgb;
        switch (f.kind) {
          case "wave": {
            const w = 0.5 + 0.32 * Math.sin(u * 7 + v * 4 - t * 1.1) + 0.2 * Math.sin(u * 4 - v * 6 + t * 0.7);
            val = 0.35 + (w - 0.35) * (0.4 + 0.6 * Iu) * (1 + 0.5 * X);
            rgb = palAt(pal, val);
            break;
          }
          case "aurora": {
            const s = Math.sin((u * 2.6 + 0.35 * Math.sin(t * 0.4 + v * 2.5)) * Math.PI + t * 0.5);
            val = Math.pow(Math.max(0, s), 1.2 + 1.8 * Iu + 1.5 * X) * (0.35 + 0.65 * Iu) + 0.1;
            rgb = palAt(pal, (val + t * 0.03) % 1);
            break;
          }
          case "flame":
            val = 0.55 + (0.45 * flick - 0.25) * (0.3 + 0.7 * Iu) * (1 + 0.6 * X);
            rgb = palAt(pal, val);
            break;
          case "heart":
          case "tick":
            val = (f.kind === "tick" ? 0.04 * (1 - X) : 0.15 * (1 - X)) + pulse * (0.3 + 0.7 * Iu);
            rgb = palAt(pal, Math.min(1, val));
            break;
          default: {
            val = 0.6 + 0.25 * Math.sin(t * 0.8 + u * 2 + v) * Iu;
            rgb = base;
          }
        }
        val = val < 0 ? 0 : val > 1 ? 1 : val;
        const o = (py * W + px) * 4;
        d[o] = rgb[0]; d[o + 1] = rgb[1]; d[o + 2] = rgb[2];
        d[o + 3] = 255 * fade * val * 0.85 * level;
      }
    }
    ctx.putImageData(img, 0, 0);
  }
}

// ---- visual editor ----------------------------------------------------------
// Uses Home Assistant's own ha-form, so pickers look and behave like any
// built-in card. The favourites list is filled from the chosen light's
// effect list and can be reordered by drag.
class AmbientLightCardEditor extends HTMLElement {
  setConfig(config) { this._config = { ...config }; this._render(); }

  // HA lazy-loads its entity pickers; asking a built-in card for its editor
  // pulls them in, so ha-form can draw the light and helper selectors.
  async connectedCallback() {
    if (customElements.get("ha-entity-picker") || !window.loadCardHelpers) return;
    try {
      const helpers = await window.loadCardHelpers();
      const card = await helpers.createCardElement({ type: "entities", entities: [] });
      if (card.constructor.getConfigElement) await card.constructor.getConfigElement();
      await Promise.race([customElements.whenDefined("ha-entity-picker"), new Promise((r) => setTimeout(r, 3000))]);
    } catch (e) { /* the form still renders; pickers appear once HA has loaded them */ }
    // Rebuild so the pickers are created as the real, now-defined elements.
    this._form = null;
    this.innerHTML = "";
    this._render();
  }
  set hass(hass) { this._hass = hass; this._render(); }

  _schema() {
    const st = this._hass && this._config.entity && this._hass.states[this._config.entity];
    const effects = ((st && st.attributes.effect_list) || []).filter((e) => !HIDDEN_EFFECTS.test(e));
    const current = this._config.favorites || [];
    const options = [...new Set([...current, ...effects])].map((e) => ({ value: e, label: e }));
    return [
      { name: "entity", required: true, selector: { entity: { domain: "light" } } },
      { type: "grid", name: "", schema: [
        { name: "name", selector: { text: {} } },
        { name: "icon", selector: { icon: {} } },
      ] },
      { type: "grid", name: "", schema: [
        { name: "speed", selector: { entity: { domain: "input_number" } } },
        { name: "intensity", selector: { entity: { domain: "input_number" } } },
      ] },
      { name: "favorites", selector: { select: { multiple: true, reorder: true, options } } },
    ];
  }

  _render() {
    if (!this._hass || !this._config) return;
    if (!customElements.get("ha-entity-picker") && window.loadCardHelpers && !this._waited) {
      // Wait for connectedCallback's preload; it calls _render again.
      if (!this._waitT) this._waitT = setTimeout(() => { this._waited = true; this._render(); }, 3500);
      return;
    }
    if (!this._form) {
      this._form = document.createElement("ha-form");
      this._form.computeLabel = (s) => ({
        entity: "Light or light group", name: "Name", icon: "Icon",
        speed: "Speed helper (0-255)", intensity: "Intensity helper (0-255)",
        favorites: `Favourite effects (up to ${MAX_FAVORITES})`,
      })[s.name];
      this._form.computeHelper = (s) => s.name === "favorites"
        ? "Shown as tiles, in this order. Leave empty for sensible defaults; every effect stays reachable from the ... tile." : undefined;
      this._form.addEventListener("value-changed", (ev) => {
        const cfg = { ...ev.detail.value };
        for (const k of Object.keys(cfg)) if (cfg[k] === "" || (Array.isArray(cfg[k]) && !cfg[k].length)) delete cfg[k];
        if (cfg.favorites) cfg.favorites = cfg.favorites.slice(0, MAX_FAVORITES);
        this._config = cfg;
        this.dispatchEvent(new CustomEvent("config-changed", { detail: { config: cfg }, bubbles: true, composed: true }));
      });
      this.appendChild(this._form);
    }
    this._form.hass = this._hass;
    this._form.data = this._config;
    this._form.schema = this._schema();
  }
}

customElements.define("ambient-light-card", AmbientLightCard);
customElements.define("ambient-light-card-editor", AmbientLightCardEditor);
// Earlier name, kept so existing dashboards keep working.
customElements.define("accent-light-card", class extends AmbientLightCard {});
window.customCards = window.customCards || [];
window.customCards.push({
  type: "ambient-light-card",
  name: "Ambient Light Card",
  description: "Effects as feel tiles, plus brightness, speed and intensity, for a room's ambient lights.",
  preview: false,
  documentationURL: "https://github.com/davidcoulson/ambient-light-card",
});
