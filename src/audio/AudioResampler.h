#pragma once
#include "audio/AudioBuffer.h"
#include <algorithm>
#include <cmath>
#include <type_traits>
namespace reagba {
// Owned by the single active audio consumer. No allocation or host calls.
class AudioResampler {
    int16_t left_[2]{}, right_[2]{};
    double phase_ = 0;
    bool primed_ = false;
    double rate_ = 0, filtered_ = 0, integral_ = 0, correction_ = 0;
    uint64_t underruns_ = 0, resyncs_ = 0;
  public:
    void Reset() { phase_ = 0; primed_ = false; rate_ = filtered_ = integral_ = correction_ = 0; }
    double Correction() const { return correction_; }
    uint64_t Underruns() const { return underruns_; }
    uint64_t Resyncs() const { return resyncs_; }
    template<class Sample>
    void Render(AudioBuffer& input, Sample* output, int frames, int channels, double rate, double gain) {
        if (frames <= 0 || channels < 1) return;
        std::fill_n(output, size_t(frames) * channels, Sample{});
        if (!std::isfinite(rate) || rate < 8000 || rate > 768000) return;
        if (rate_ && rate_ != rate) Reset();
        rate_ = rate;
        const double demand = frames * 32768.0 / rate;
        const double target = 1536.0 + demand;
        const auto start = size_t(std::ceil(1536.0 + demand * 1.005)) + 2;
        if (start > AudioBuffer::Capacity / 2) return;
        auto queued = input.Available() / 2;
        // Recover from suspended callbacks/overload without playing a long stale backlog.
        if (queued > start + 2048) {
            input.Discard((queued - start) * 2);
            Reset(); rate_ = rate; queued = start; ++resyncs_;
        }
        if (!primed_ && queued < start) return;
        const double dt = frames / rate;
        // Low-pass bursty frame delivery before the bounded PI clock servo.
        const double error = (double(queued) - target) / 32768.0;
        filtered_ += (1.0 - std::exp(-dt / 0.5)) * (error - filtered_);
        integral_ = std::clamp(integral_ + filtered_ * dt * 0.02, -0.005, 0.005);
        const double wanted = std::clamp(filtered_ * 0.25 + integral_, -0.005, 0.005);
        correction_ += std::clamp(wanted - correction_, -dt * 0.002, dt * 0.002);
        const double step = 32768.0 / rate * (1.0 + correction_);
        const auto convert = [](double value) -> Sample {
            if constexpr (std::is_integral_v<Sample>) return Sample(std::clamp(value * 32768.0, -32768.0, 32767.0));
            else return value;
        };
        for (int i = 0; i < frames; ++i) {
            if (!primed_) {
                if (input.Pop(left_, 2) != 2 || input.Pop(right_, 2) != 2) { Reset(); ++underruns_; return; }
                primed_ = true;
            }
            while (phase_ >= 1) {
                left_[0] = right_[0]; left_[1] = right_[1];
                if (input.Pop(right_, 2) != 2) { Reset(); ++underruns_; return; }
                phase_ -= 1;
            }
            const double l = (left_[0] + (right_[0] - left_[0]) * phase_) * gain / 32768.0;
            const double r = (left_[1] + (right_[1] - left_[1]) * phase_) * gain / 32768.0;
            output[size_t(i) * channels] = convert(channels == 1 ? (l + r) * 0.5 : l);
            if (channels > 1) output[size_t(i) * channels + 1] = convert(r);
            phase_ += step;
        }
    }
};
} // namespace reagba
