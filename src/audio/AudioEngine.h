#pragma once
#include "app/EmulatorManager.h"
#include "audio/AudioResampler.h"
#include <SDL.h>
#include <chrono>
namespace reagba {
class AudioEngine {
    EmulatorManager &manager_;
    SDL_AudioDeviceID device_ = 0;
    bool sdlAudio_ = false;
    AudioResampler systemResampler_;
    std::chrono::steady_clock::time_point lastSystemCallback_{};
    struct Host;
    std::unique_ptr<Host> host_;
    AudioOutput output_;
    bool initialized_ = false;
    std::string error_;
    std::string state_;
    static void Callback(void *, Uint8 *, int);

  public:
    explicit AudioEngine(EmulatorManager &, void* (*getFunc)(const char*) = nullptr);
    ~AudioEngine();
    void Update(); // REAPER main thread only, including construction/destruction.
    bool Available() const;
    Json Outputs() const;
    Json Status() const;
};
} // namespace reagba
