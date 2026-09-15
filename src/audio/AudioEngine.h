#pragma once
#include "app/EmulatorManager.h"
#include <SDL.h>
namespace reagba {
class AudioEngine {
    EmulatorManager &manager_;
    SDL_AudioDeviceID device_ = 0;
    static void Callback(void *, Uint8 *, int);

  public:
    explicit AudioEngine(EmulatorManager &);
    ~AudioEngine();
    bool Available() const {
        return device_ != 0;
    }
};
} // namespace reagba
