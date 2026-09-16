#pragma once
#include "core/IEmulatorCore.h"
#include "rom/ROMManager.h"
#include "rom/CoverManager.h"
#include "save/SaveManager.h"
#include "video/FrameBuffer.h"
#include "audio/AudioBuffer.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
namespace reagba {
class EmulatorManager {
  public:
    using Reply = std::function<void(Json)>;
    EmulatorManager(fs::path romDir, fs::path dataDir);
    ~EmulatorManager();
    void Submit(Json command, Reply reply);
    void Shutdown();
    void SetInput(uint32_t mask) {
        keys_.store(mask & 1023);
    }
    void SetFastForward(bool held) { fastForward_.store(held); }
    FrameBuffer frames;
    AudioBuffer audio;
    std::atomic<float> volume{0.3f};
    std::atomic<bool> audible{false};

  private:
    struct Request {
        Json command;
        Reply reply;
    };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    std::thread worker_;
    bool quit_ = false;
    std::atomic<uint32_t> keys_{0};
    std::atomic<bool> fastForward_{false};
    uint32_t appliedInput_ = 0, observedInput_ = 0;
    double EffectiveSpeed() const { return fastForward_.load() ? 4.0 : speed_; }
    std::unique_ptr<IEmulatorCore> core_;
    ROMInfo current_;
    SaveManager saves_;
    CoverManager covers_;
    fs::path romDir_;
    Json preferences_ = Json::object(), library_ = Json::array();
    bool running_ = false;
    double speed_ = 1.0, fps_ = 0;
    uint64_t frameCount_ = 0;
    int frameSkip_ = 0;
    std::string lastError_;
    void Run();
    Json Handle(const Json &);
    Json State() const;
    void Persist();
};
} // namespace reagba
