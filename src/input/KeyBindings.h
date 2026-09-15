#pragma once
#include "rom/ROMManager.h"
namespace reagba {
// mGBA bit order: A, B, Select, Start, Right, Left, Up, Down, R, L.
inline Json DefaultKeys() {
    return {"J", "K", "Space", "Return", "D", "A", "W", "S", "E", "Q"};
}
inline void NormalizeKeys(Json &settings) {
    const Json legacy = {"X", "Z", "Backspace", "Return", "Right", "Left", "Up", "Down", "W", "Q"};
    if (!settings.contains("keys") || settings["keys"] == legacy)
        settings["keys"] = DefaultKeys();
    if (!settings.contains("fast_forward_key"))
        settings["fast_forward_key"] = "R";
}
}
