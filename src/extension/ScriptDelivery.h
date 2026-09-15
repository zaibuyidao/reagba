#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace reagba {
inline std::string DeliveryScript(const std::string& json) {
    // Quote the complete message as data, including Unicode line separators.
    return "window.ReaGBAReceive && window.ReaGBAReceive(JSON.parse(" +
        nlohmann::json(json).dump(-1,' ',true,nlohmann::json::error_handler_t::replace) + "));";
}
}

