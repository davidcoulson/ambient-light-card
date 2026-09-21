#pragma once

// ---------------------------------------------------------------------------
// The basement, as the 2D effects see it. THE one place a light's position
// lives.
//
// Each light finds its own row here by MAC suffix (the `mac` substitution every
// device file already has), so a device file carries no coordinates at all:
// include the fx2d package and the light knows where it is. The same table is
// what the shared-simulation effects (Game of Life, Pac-Man) use to reason
// about the WHOLE room, which is why it has to exist in C++ rather than as
// per-device YAML substitutions - and why keeping a second copy in YAML, as an
// earlier version did, meant every move had to be made twice.
//
// To move a light: edit its x / y. To add one: add a row and bump ROOM_N.
// A light whose MAC is not listed still runs the effects, scattered by a hash
// of its MAC instead of placed - harmless, just not part of the picture.
//
// x runs west -> east, y runs from the NORTH wall (0) to the south (1), each
// normalised 0..1 on its own axis, read off the lower-level plan. Letters are
// the plan's labels.
// ---------------------------------------------------------------------------

#include <strings.h>
#include "light_effects_helpers.h"

// The plan is 1465 px wide but only 780 px deep. x and y are each normalised
// on their own axis, so treating them as equal units would squash the room: a
// "circle" would be an ellipse nearly twice as wide as deep. light_xy()
// therefore returns y pre-scaled by this ratio, giving coordinates where one
// unit is the same real distance on both axes: x spans 0..1 and y spans
// 0..ROOM_ASPECT. Anything that places a point in the room (a drop, a ball,
// the room centre) must use the same convention.
constexpr float ROOM_ASPECT = 0.5324f;
constexpr float ROOM_CX = 0.5f;
constexpr float ROOM_CY = 0.5f * ROOM_ASPECT;

struct RoomLight {
  const char *mac;   // last six hex digits, as in the device's `mac` substitution
  float x, y;        // 0..1 on each axis, y NOT yet aspect-scaled
};

constexpr int ROOM_N = 26;
constexpr RoomLight ROOM_MAP[ROOM_N] = {
    {"4e7dd4", 0.000f, 0.145f},   // A
    {"585cc4", 0.167f, 0.145f},   // B
    {"4e7e40", 0.332f, 0.145f},   // C
    {"5739d0", 0.456f, 0.079f},   // D
    {"5882e0", 0.694f, 0.079f},   // E
    {"4e7df0", 0.877f, 0.000f},   // F
    {"4e7e00", 1.000f, 0.000f},   // G
    {"4e7df8", 0.000f, 0.324f},   // H
    {"4e7e48", 0.165f, 0.324f},   // I
    {"588b30", 0.333f, 0.324f},   // J
    {"4e7e30", 0.456f, 0.336f},   // K
    {"4e7e38", 0.694f, 0.331f},   // L
    {"4e7dc0", 0.879f, 0.312f},   // M
    {"4e7de0", 1.000f, 0.312f},   // N
    {"4e7dfc", 0.328f, 0.535f},   // O
    {"56a9a0", 0.464f, 0.535f},   // P
    {"56a55c", 0.599f, 0.535f},   // Q
    {"4e84d0", 0.708f, 0.535f},   // R
    {"4e84e8", 0.328f, 0.715f},   // S
    {"4e894c", 0.464f, 0.712f},   // T
    {"4e84d8", 0.599f, 0.715f},   // U
    {"4e7dac", 0.710f, 0.715f},   // V
    {"4e8240", 0.599f, 0.837f},   // W
    {"56ab0c", 0.717f, 0.853f},   // X
    {"4e88f8", 0.294f, 1.000f},   // Y
    {"4e8244", 0.396f, 1.000f},   // Z
};

// This light's row in ROOM_MAP, or -1 if its MAC is not on the map. Looked up
// once; a device only ever has one MAC.
inline int room_self(const char *mac) {
  static int idx = -2;
  if (idx == -2) {
    idx = -1;
    for (int i = 0; i < ROOM_N; i++) {
      if (strcasecmp(ROOM_MAP[i].mac, mac) == 0) { idx = i; break; }
    }
  }
  return idx;
}

// Where this light sits, in true room units (x 0..1, y 0..ROOM_ASPECT).
inline void light_xy(const char *mac, float &x, float &y) {
  int i = room_self(mac);
  if (i >= 0) {
    x = ROOM_MAP[i].x;
    y = ROOM_MAP[i].y * ROOM_ASPECT;
    return;
  }
  uint32_t h = mac_hash32(mac);
  x = (h & 0xFFFFFFu) / 16777216.0f;
  y = ((h >> 8) ^ 0x9E3779B9u) % 16777216u / 16777216.0f * ROOM_ASPECT;
}

// The cell this light displays in a shared simulation. An unmapped light
// borrows whichever mapped cell is nearest its scattered position.
inline int room_index(const char *mac) {
  int i = room_self(mac);
  if (i >= 0) return i;
  static int nearest = -1;
  if (nearest < 0) {
    float px, py;
    light_xy(mac, px, py);
    float best = 1e9f;
    for (int k = 0; k < ROOM_N; k++) {
      float dx = ROOM_MAP[k].x - px, dy = ROOM_MAP[k].y * ROOM_ASPECT - py;
      float d = dx * dx + dy * dy;
      if (d < best) { best = d; nearest = k; }
    }
  }
  return nearest;
}
