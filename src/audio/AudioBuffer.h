#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
namespace reagba {
class AudioBuffer {
  public:
    static constexpr size_t Capacity = 32768; // 500 ms maximum, interleaved stereo
  private:
    std::array<int16_t, Capacity> samples_{};
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};

  public:
    // Consumer only. Sampling both cursors from a third thread is not a coherent snapshot.
    size_t Available() const {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed);
    }
    void Discard(size_t count) {
        auto r = read_.load(std::memory_order_relaxed);
        auto available = write_.load(std::memory_order_acquire) - r;
        read_.store(r + (count < available ? count : available), std::memory_order_release);
    }
    size_t Push(const int16_t *data, size_t count) {
        auto w = write_.load(std::memory_order_relaxed);
        auto r = read_.load(std::memory_order_acquire);
        count = count < Capacity - (w - r) ? count : Capacity - (w - r);
        for (size_t i = 0; i < count; ++i)
            samples_[(w + i) % Capacity] = data[i];
        write_.store(w + count, std::memory_order_release);
        return count;
    }
    size_t Pop(int16_t *data, size_t count) {
        auto r = read_.load(std::memory_order_relaxed);
        auto w = write_.load(std::memory_order_acquire);
        count = count < w - r ? count : w - r;
        for (size_t i = 0; i < count; ++i)
            data[i] = samples_[(r + i) % Capacity];
        read_.store(r + count, std::memory_order_release);
        return count;
    }
    // Consumer only. The producer must never alter the read cursor.
    void Discard() {
        read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
    }
};
} // namespace reagba
