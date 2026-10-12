#pragma once

// ---------------------------------------------------------------------------
// The front yard, as the Halloween effects see it. An EXAMPLE - copy it into
// your config directory, edit the rows, and point the `yard_map` substitution
// at your copy.
//
// Seven outdoor bulbs along a trick-or-treat route: two low lights in a bed by
// the street (S), three on the garage wall along the driveway (G) and two
// flanking the front door (D). Feet, measured off an aerial photo: x west ->
// east and y north -> south (toward the street), origin at G1. path_ft is the
// distance from the street along the route visitors walk.
//
// Each device names its row with the `yard_light` substitution ("G2"), not
// with its MAC as the room maps do: the bulbs can be swapped or replaced
// without touching this file, and the row still says where the FITTING is.
// The table stays the single copy of the layout, which is what lets an effect
// reason about a whole group (turn-taking needs to know who the neighbours
// are). A device whose name is not here still runs every effect, as a lone
// driveway light.
// ---------------------------------------------------------------------------

#include "halloween_fx.h"

constexpr int YARD_N = 7;
constexpr YardLight YARD_MAP[YARD_N] = {
    {"S1", YARD_S, -23.0f, 75.0f, 0.0f},
    {"S2", YARD_S, -18.5f, 75.0f, 4.0f},
    {"G3", YARD_G, 0.0f, 20.0f, 60.0f},
    {"G2", YARD_G, 0.0f, 10.0f, 70.0f},
    {"G1", YARD_G, 0.0f, 0.0f, 80.0f},
    {"D1", YARD_D, 32.0f, 22.0f, 118.0f},
    {"D2", YARD_D, 42.0f, 22.0f, 128.0f},
};

// This light, looked up once per effect (each lambda keeps a static copy).
inline HwSelf hw_me(const char *name, const char *mac) { return hw_lookup(YARD_MAP, YARD_N, name, mac); }
