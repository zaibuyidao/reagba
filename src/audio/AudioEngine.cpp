#include "audio/AudioEngine.h"
#include <algorithm>
#include <cstring>
namespace reagba {
AudioEngine::AudioEngine(EmulatorManager &m) : manager_(m) {
    SDL_AudioSpec spec{};
    spec.freq = SampleRate;
    spec.format = AUDIO_S16SYS;
    spec.channels = 2;
    spec.samples = 512;
    spec.callback = Callback;
    spec.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
    if (device_)
        SDL_PauseAudioDevice(device_, 0);
}
AudioEngine::~AudioEngine() {
    if (device_)
        SDL_CloseAudioDevice(device_);
}
void AudioEngine::Callback(void *userdata, Uint8 *output, int bytes) {
    auto &m = static_cast<AudioEngine *>(userdata)->manager_;
    std::memset(output, 0, bytes);
    if (!m.audible.load()) {
        m.audio.Discard();
        return;
    }
    auto *pcm = reinterpret_cast<int16_t *>(output);
    size_t n = m.audio.Pop(pcm, size_t(bytes) / 2);
    float gain = m.volume.load();
    for (size_t i = 0; i < n; ++i)
        pcm[i] = int16_t(pcm[i] * gain);
}
} // namespace reagba
