#pragma once
#include <string>
namespace reagba {
struct AudioOutput {
    std::string mode = "system", track = "preview";
    int channel = 0;
    bool mono = false;
    bool operator==(const AudioOutput& other) const {
        return mode == other.mode && track == other.track && channel == other.channel && mono == other.mono;
    }
};
} // namespace reagba
