#include "input/InputManager.h"
namespace reagba {
InputManager::~InputManager() {
    if (pad_)
        SDL_GameControllerClose(pad_);
}
void InputManager::Configure(const Json &config) {
    Clear();
    if (config.contains("keys") && config["keys"].is_array() && config["keys"].size() == 10) {
        for (size_t i = 0; i < 10; ++i)
            if (config["keys"][i].is_string()) {
                auto key = SDL_GetScancodeFromName(config["keys"][i].get<std::string>().c_str());
                if (key != SDL_SCANCODE_UNKNOWN)
                    mapping_[i] = key;
            }
    }
    auto boost = SDL_GetScancodeFromName(config.value("fast_forward_key", std::string("L")).c_str());
    if (boost != SDL_SCANCODE_UNKNOWN)
        mapping_[10] = boost;
}
uint32_t InputManager::Poll(bool active) {
    SDL_PumpEvents();
    if (pad_ && !SDL_GameControllerGetAttached(pad_)) {
        SDL_GameControllerClose(pad_);
        pad_ = nullptr;
    }
    auto now = SDL_GetTicks();
    if (!pad_ && now - lastScan_ > 1000) {
        lastScan_ = now;
        for (int i = 0; i < SDL_NumJoysticks(); ++i)
            if (SDL_IsGameController(i)) {
                pad_ = SDL_GameControllerOpen(i);
                break;
            }
    }
    SDL_GameControllerUpdate();
    if (!active) {
        Clear();
        return 0;
    }
    uint32_t keys = 0;
    for (size_t i = 0; i < 10; ++i)
        if (held_[mapping_[i]] || pressed_[mapping_[i]])
            keys |= 1u << i;
    fastForward_ = held_[mapping_[10]] || pressed_[mapping_[10]];
    pressed_.fill(false);
    static constexpr SDL_GameControllerButton buttons[] = {SDL_CONTROLLER_BUTTON_A,
                                                           SDL_CONTROLLER_BUTTON_B,
                                                           SDL_CONTROLLER_BUTTON_BACK,
                                                           SDL_CONTROLLER_BUTTON_START,
                                                           SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                                           SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                                                           SDL_CONTROLLER_BUTTON_DPAD_UP,
                                                           SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                                           SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                                                           SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
    if (pad_) {
        for (size_t i = 0; i < 10; ++i)
            if (SDL_GameControllerGetButton(pad_, buttons[i]))
                keys |= 1u << i;
        auto x = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTX),
             y = SDL_GameControllerGetAxis(pad_, SDL_CONTROLLER_AXIS_LEFTY);
        if (x > 16000)
            keys |= 1 << 4;
        if (x < -16000)
            keys |= 1 << 5;
        if (y < -16000)
            keys |= 1 << 6;
        if (y > 16000)
            keys |= 1 << 7;
    }
    if ((keys & 0x30) == 0x30)
        keys &= ~0x30u;
    if ((keys & 0xc0) == 0xc0)
        keys &= ~0xc0u;
    return keys;
}
} // namespace reagba
