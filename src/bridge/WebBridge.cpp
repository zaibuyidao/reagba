#include "bridge/WebBridge.h"
#include <set>
namespace reagba {
void WebBridge::Request(const std::string &text, EmulatorManager::Reply reply) {
    try {
        if (text.size() > 65536)
            throw std::runtime_error("Request exceeds 64 KiB");
        auto command = Json::parse(text);
        if (!command.is_object() || !command.contains("action") || !command["action"].is_string())
            throw std::runtime_error("Expected an action object");
        static const std::set<std::string> actions = {"load_rom",
                                                      "start",
                                                      "pause",
                                                      "reset",
                                                      "stop",
                                                      "save_state",
                                                      "load_state",
                                                      "save_game",
                                                      "screenshot",
                                                      "set_volume",
                                                      "set_speed",
                                                      "set_frame_skip",
                                                      "get_game_info",
                                                      "get_emulator_state",
                                                      "get_save_states",
                                                      "scan_roms",
                                                      "get_cover",
                                                      "favorite",
                                                      "get_settings",
                                                      "set_settings"};
        if (!actions.count(command["action"].get<std::string>()))
            throw std::runtime_error("Unsupported bridge action");
        manager_.Submit(std::move(command), std::move(reply));
    } catch (const std::exception &e) {
        reply({{"ok", false}, {"error", e.what()}});
    }
}
} // namespace reagba
