#pragma once
// Turn-taking for the addressable effects, so that no stretch of a strip flares
// more than 2 times in a second (WCAG 2.3.1) however busy the effect is.
//
// The strip is cut into arcs of about six LEDs (two to eight of them). An effect
// that scatters sparks, foam or flashes lets ONE arc take each window, in
// rotation, so an arc is lit at most once every AFX_GAP_S seconds while the
// strip as a whole stays as busy as before. Everything here is a pure function
// of the shared clock and the LED's place, like the effects that use it.
#include "light_effects_helpers.h"

constexpr double AFX_GAP_S = 0.5;   // an arc takes a turn at most once in this long

// Number of arcs: about six LEDs each, between two and eight.
inline int afx_arcs(int n) { return n < 12 ? 2 : (n >= 48 ? 8 : n / 6); }

// The arc whose turn it is in window number `slot` (sync_bucket, or a tick count).
// Stepping by arcs-1 shuffles the order a little: neighbours do not follow neighbours.
inline int afx_turn(uint32_t slot, int arcs) { return (int) ((slot * (uint32_t) (arcs > 2 ? arcs - 1 : 1)) % (uint32_t) arcs); }

// First LED of an arc, and how many it has.
inline int afx_arc_start(int arc, int n, int arcs) { return (int) ((long) arc * n / arcs); }
inline int afx_arc_len(int arc, int n, int arcs) { return afx_arc_start(arc + 1, n, arcs) - afx_arc_start(arc, n, arcs); }

// A LED of that arc, chosen by a dice roll r.
inline int afx_pick(int arc, int n, int arcs, uint32_t r) {
  int len = afx_arc_len(arc, n, arcs);
  return afx_arc_start(arc, n, arcs) + (int) (r % (uint32_t) (len > 0 ? len : 1));
}

// The window an effect with `base_s` between draws should use: its own, or one
// arc's share of `gap_s` if that is longer.
inline double afx_window(double base_s, int arcs, double gap_s = AFX_GAP_S) { return std::max(base_s, gap_s / arcs); }

// The same in ticks, for the effects that simulate in fixed ticks (Lockstep).
inline uint32_t afx_window_ticks(uint32_t tick_ms, int arcs, double gap_s = AFX_GAP_S) {
  double w = gap_s * 1000.0 / arcs / tick_ms;
  return w < 1.0 ? 1u : (uint32_t) lround(w);
}
