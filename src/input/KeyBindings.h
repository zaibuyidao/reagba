#pragma once
#include "rom/ROMManager.h"
namespace reagba {
// mGBA bit order: A, B, Select, Start, Right, Left, Up, Down, R, L.
inline Json DefaultKeys() {
    return {"J", "K", "Space", "Return", "D", "A", "W", "S", "Q", "O"};
}
inline void NormalizeKeys(Json &settings) {
    const Json legacy = {"X", "Z", "Backspace", "Return", "Right", "Left", "Up", "Down", "W", "Q"};
    if (!settings.contains("keys") || settings["keys"] == legacy)
        settings["keys"] = DefaultKeys();
    const auto turbo = settings.value("turbo_keys", Json());
    if (!turbo.is_array() || turbo.size() != 2 || !turbo[0].is_string() || !turbo[1].is_string() ||
        turbo[0].get<std::string>().size() > 64 || turbo[1].get<std::string>().size() > 64)
        settings["turbo_keys"] = Json::array({"", ""});
    if (!settings.contains("fast_forward_key"))
        settings["fast_forward_key"] = "L";
}
}
