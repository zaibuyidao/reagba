#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace reagba {
// mGBA's GB stream supplies 131072 Hz PCM. Low-pass before decimating to 32768 Hz.
class GBPCM {
    static constexpr size_t Taps = 129;
    std::array<std::array<int16_t, 2>, Taps> history_{};
    size_t cursor_ = 0;
    unsigned phase_ = 0;
    std::vector<int16_t> samples_;
    static const std::array<double, Taps>& Coefficients() {
        static const auto coefficients = [] {
            std::array<double, Taps> result{};
            constexpr double pi = 3.14159265358979323846, cutoff = 0.1;
            double sum = 0;
            for (size_t i = 0; i < Taps; ++i) {
                const auto x = double(i) - double(Taps - 1) / 2;
                const auto window = 0.42 - 0.5 * std::cos(2 * pi * i / (Taps - 1)) +
                                    0.08 * std::cos(4 * pi * i / (Taps - 1));
                result[i] = (x == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x)) * window;
                sum += result[i];
            }
            for (auto& value : result) value /= sum;
            return result;
        }();
        return coefficients;
    }
  public:
    void Reset() { history_ = {}; cursor_ = phase_ = 0; samples_.clear(); }
    void Push(int16_t left, int16_t right) {
        history_[cursor_] = {left, right};
        cursor_ = (cursor_ + 1) % Taps;
        if (++phase_ != 4) return;
        phase_ = 0;
        double l = 0, r = 0;
        size_t index = cursor_;
        const auto& coefficients = Coefficients();
        for (auto coefficient : coefficients) {
            index = index ? index - 1 : Taps - 1;
            l += history_[index][0] * coefficient;
            r += history_[index][1] * coefficient;
        }
        samples_.push_back(int16_t(std::clamp(std::lround(l), -32768L, 32767L)));
        samples_.push_back(int16_t(std::clamp(std::lround(r), -32768L, 32767L)));
    }
    std::vector<int16_t> Drain() {
        std::vector<int16_t> result;
        result.swap(samples_);
        return result;
    }
};
} // namespace reagba
