#pragma once
#include "core/IEmulatorCore.h"
#include <atomic>
namespace reagba {
// Exactly one producer (emulation) and one consumer (native render loop).
class FrameBuffer {
    std::array<Frame, 3> frames_{};
    unsigned back_ = 0, front_ = 2;
    std::atomic<unsigned> middle_{1};
    static constexpr unsigned Dirty = 4;

  public:
    void Publish(const Frame &frame) {
        frames_[back_] = frame;
        back_ = middle_.exchange(back_ | Dirty, std::memory_order_acq_rel) & 3;
    }
    bool Consume() {
        if (!(middle_.load(std::memory_order_acquire) & Dirty))
            return false;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & 3;
        return true;
    }
    const Frame &Front() const {
        return frames_[front_];
    }
};
} // namespace reagba
