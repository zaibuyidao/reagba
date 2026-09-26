#pragma once
#include "core/IEmulatorCore.h"
#include <atomic>
namespace reagba {
// Exactly one producer (emulation) and one consumer (core frame API).
class FrameBuffer {
  public:
    struct Sink { void (*publish)(void*, const Frame&); void* context; };
  private:
    std::atomic<Sink*> sink_{nullptr};
    std::array<Frame, 3> frames_{};
    unsigned back_ = 0, front_ = 2;
    std::atomic<unsigned> middle_{1};
    static constexpr unsigned Dirty = 4;

  public:
    // Configure before publishing. Keep the sink alive until the producer joins.
    void SetSink(Sink* sink) { sink_.store(sink, std::memory_order_release); }
    void Publish(const Frame &frame) {
        if (auto* sink = sink_.load(std::memory_order_acquire)) sink->publish(sink->context, frame);
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
