#include "audio/AudioEngine.h"
#include "audio/AudioResampler.h"
#include "reaper_plugin.h"
#include <algorithm>
#include <chrono>
#include <cstring>
namespace reagba {
struct AudioEngine::Host {
    ReaProject* (*enumProjects)(int, char*, int) = nullptr;
    int (*countTracks)(ReaProject*) = nullptr;
    MediaTrack* (*getTrack)(ReaProject*, int) = nullptr;
    MediaTrack* (*getSelected)(ReaProject*, int) = nullptr;
    bool (*trackName)(MediaTrack*, char*, int) = nullptr;
    int (*play)(ReaProject*, preview_register_t*, int, double) = nullptr;
    int (*stop)(ReaProject*, preview_register_t*) = nullptr;
    int (*playOutput)(preview_register_t*, int, double) = nullptr;
    int (*stopOutput)(preview_register_t*) = nullptr;
    int (*countOutputs)() = nullptr;
    const char* (*outputName)(int) = nullptr;
    bool (*deviceInfo)(const char*, char*, int) = nullptr;
    int (*isRunning)() = nullptr;
    Json device = Json::object();
    uint64_t revision = 0;
    preview_register_t preview{};
    ReaProject* project = nullptr;
    MediaTrack* track = nullptr;
    bool playing = false;
    bool outputPreview = false;
    std::chrono::steady_clock::time_point lastAttempt{};
    class Source final : public PCM_source {
        EmulatorManager& manager_;
        AudioResampler resampler_;
        std::chrono::steady_clock::time_point last_{};
      public:
        explicit Source(EmulatorManager& manager) : manager_(manager) {}
        void Reset() { resampler_.Reset(); last_ = {}; }
        PCM_source* Duplicate() override { return nullptr; }
        bool IsAvailable() override { return true; }
        const char* GetType() override { return "REAGBA_LIVE"; }
        bool SetFileName(const char*) override { return false; }
        int GetNumChannels() override { return 2; }
        double GetSampleRate() override { return SampleRate; }
        double GetLength() override { return 86400.0; }
        int PropertiesWindow(HWND) override { return 0; }
        void GetSamples(PCM_source_transfer_t* block) override {
            block->samples_out = 0;
            if (!block->samples || block->length <= 0 || block->nch <= 0) return;
            const auto now = std::chrono::steady_clock::now();
            const auto gap = std::chrono::duration<double>(std::max(0.2, block->length * 4.0 / std::max(8000.0, block->samplerate)));
            if (!manager_.audible.load() || (last_.time_since_epoch().count() && now - last_ > gap)) {
                manager_.audio.Discard(); resampler_.Reset();
            }
            last_ = now;
            if (manager_.audible.load())
                resampler_.Render(manager_.audio, block->samples, block->length, block->nch, block->samplerate, manager_.volume.load());
            else std::fill_n(block->samples, size_t(block->length) * block->nch, 0.0);
            // A live source stays alive across underruns, pause and fast-forward.
            block->samples_out = block->length;
        }
        void GetPeakInfo(PCM_source_peaktransfer_t*) override {}
        void SaveState(ProjectStateContext*) override {}
        int LoadState(const char*, ProjectStateContext*) override { return -1; }
        void Peaks_Clear(bool) override {}
        int PeaksBuild_Begin() override { return 0; }
        int PeaksBuild_Run() override { return 0; }
        void PeaksBuild_Finish() override {}
    } source;
    Host(EmulatorManager& manager, void* (*get)(const char*)) : source(manager) {
        if (get) {
#define LOAD(member, name) member = reinterpret_cast<decltype(member)>(get(name))
            LOAD(enumProjects, "EnumProjects"); LOAD(countTracks, "CountTracks");
            LOAD(getTrack, "GetTrack");
            LOAD(getSelected, "GetSelectedTrack");
            LOAD(trackName, "GetTrackName");
            LOAD(play, "PlayTrackPreview2Ex"); LOAD(stop, "StopTrackPreview2");
            LOAD(playOutput, "PlayPreviewEx"); LOAD(stopOutput, "StopPreview");
            LOAD(countOutputs, "GetNumAudioOutputs"); LOAD(outputName, "GetOutputChannelName");
            LOAD(deviceInfo, "GetAudioDeviceInfo"); LOAD(isRunning, "Audio_IsRunning");
#undef LOAD
        }
#ifdef _WIN32
        InitializeCriticalSection(&preview.cs);
#else
        pthread_mutex_init(&preview.mutex, nullptr);
#endif
        preview.src = &source; preview.volume = 1; preview.loop = true; preview.m_out_chan = -1;
    }
    bool Supported() const { return enumProjects && countTracks && getTrack && getSelected && trackName && play && stop && playOutput && stopOutput && countOutputs && outputName && deviceInfo && isRunning; }
    bool RefreshDevice() {
        Json next = {{"running", isRunning && isRunning() != 0}, {"channels", Json::array()}};
        for (const auto* key : {"MODE", "IDENT_OUT", "SRATE", "BSIZE"}) {
            char text[1024]{};
            next[key] = deviceInfo && deviceInfo(key, text, sizeof(text)) ? text : "";
        }
        if (countOutputs && outputName) for (int i = 0, n = std::clamp(countOutputs(), 0, 1024); i < n; ++i) {
            const char* name = outputName(i);
            next["channels"].push_back(name ? name : "");
        }
        if (next == device) return false;
        device = std::move(next); ++revision; return true;
    }
    void Stop() {
        if (playing) {
            if (outputPreview) stopOutput(&preview);
            else stop(project, &preview);
        }
        playing = false;
        // REAPER uses this lock for callbacks. Drain any callback before reset/destruction.
#ifdef _WIN32
        EnterCriticalSection(&preview.cs); LeaveCriticalSection(&preview.cs);
#else
        pthread_mutex_lock(&preview.mutex); pthread_mutex_unlock(&preview.mutex);
#endif
        source.Reset();
    }
    ~Host() {
        Stop();
#ifdef _WIN32
        DeleteCriticalSection(&preview.cs);
#else
        pthread_mutex_destroy(&preview.mutex);
#endif
    }
};
AudioEngine::AudioEngine(EmulatorManager& m, void* (*get)(const char*)) : manager_(m), host_(std::make_unique<Host>(m, get)) { Update(); }
AudioEngine::~AudioEngine() {
    if (device_) SDL_CloseAudioDevice(device_);
    if (sdlAudio_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    host_->Stop();
}
void AudioEngine::Update() {
    const auto desired = manager_.GetAudioOutput();
    auto& host = *host_;
    const bool deviceChanged = host.RefreshDevice() && desired.mode != "system";
    ReaProject* project = nullptr;
    MediaTrack* track = nullptr;
    if (desired.mode == "reaper_track" && host.Supported()) {
        project = host.enumProjects(-1, nullptr, 0);
        if (project) {
            if (desired.track == "selected") track = host.getSelected(project, 0);
            else for (int i = 0; i < host.countTracks(project); ++i) {
                auto* candidate = host.getTrack(project, i);
                char name[1024]{};
                if (candidate && host.trackName(candidate, name, sizeof(name)) && std::strcmp(name, "ReaGBA Preview") == 0) { track = candidate; break; }
            }
            if (!track) track = host.getSelected(project, 0);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    const bool systemStopped = device_ && SDL_GetAudioDeviceStatus(device_) == SDL_AUDIO_STOPPED;
    if (initialized_ && desired == output_ && project == host.project && track == host.track &&
        !deviceChanged && !systemStopped &&
        (error_.empty() || now - host.lastAttempt < std::chrono::seconds(1))) return;
    host.lastAttempt = now;
    if (device_) { SDL_CloseAudioDevice(device_); device_ = 0; }
    if (sdlAudio_) { SDL_QuitSubSystem(SDL_INIT_AUDIO); sdlAudio_ = false; }
    host.Stop();
    systemResampler_.Reset(); lastSystemCallback_ = {};
    manager_.audio.Discard(); // Both consumers are stopped before handing over the SPSC queue.
    output_ = desired; initialized_ = true; error_.clear(); state_ = "active"; host.project = project; host.track = track;
    if (desired.mode == "system") {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) { state_ = "unavailable"; error_ = "System audio device unavailable"; return; }
        sdlAudio_ = true;
        SDL_AudioSpec spec{};
        spec.freq = SampleRate; spec.format = AUDIO_S16SYS; spec.channels = 2; spec.samples = 512;
        spec.callback = Callback; spec.userdata = this;
        device_ = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
        if (device_) SDL_PauseAudioDevice(device_, 0);
        else { state_ = "unavailable"; error_ = "System audio device unavailable"; }
    } else if (!host.Supported()) { state_ = "unavailable"; error_ = "REAPER audio output unavailable"; }
    else if (desired.mode == "reaper_track" && !track) state_ = "no_track";
    else if (!host.device["running"].get<bool>()) { state_ = "engine_stopped"; error_ = "REAPER audio device is stopped"; }
    else if (desired.mode == "reaper_output") {
        if (desired.channel + (desired.mono ? 1 : 2) > int(host.device["channels"].size())) {
            state_ = "channel_unavailable"; error_ = "Selected REAPER output channels unavailable"; return;
        }
        host.outputPreview = true;
        host.preview.preview_track = nullptr;
        host.preview.m_out_chan = desired.channel | (desired.mono ? 1024 : 0); host.preview.curpos = 0;
        host.playing = host.playOutput(&host.preview, 0, -1) != 0;
        if (!host.playing) { state_ = "unavailable"; error_ = "Could not start REAPER audio output"; }
    } else if (track) {
        host.outputPreview = false;
        host.preview.m_out_chan = -1;
        host.preview.preview_track = track; host.preview.curpos = 0;
        // flags=0 disables source prebuffering. Audio is pulled by REAPER's mixer.
        host.playing = host.play(project, &host.preview, 0, -1) != 0;
        if (!host.playing) { state_ = "unavailable"; error_ = "Could not start REAPER audio output"; }
    }
}
bool AudioEngine::Available() const { return device_ != 0 || host_->playing; }
Json AudioEngine::Status() const {
    return {{"audio_output", output_.mode}, {"audio_track", output_.track}, {"audio_channel", output_.channel},
            {"audio_mono", output_.mono}, {"audio_device", Available()}, {"audio_error", error_},
            {"audio_state", state_}, {"audio_outputs_revision", host_->revision}};
}
Json AudioEngine::Outputs() const {
    return {{"reaper_available", host_->Supported()}, {"reaper_device", host_->device}, {"status", Status()}};
}
void AudioEngine::Callback(void* userdata, Uint8* output, int bytes) {
    auto& engine = *static_cast<AudioEngine*>(userdata);
    auto& m = engine.manager_;
    std::memset(output, 0, bytes);
    const auto now = std::chrono::steady_clock::now();
    const bool audible = m.audible.load();
    if (!audible || (engine.lastSystemCallback_.time_since_epoch().count() &&
                    now - engine.lastSystemCallback_ > std::chrono::milliseconds(200))) {
        m.audio.Discard(); engine.systemResampler_.Reset();
    }
    engine.lastSystemCallback_ = now;
    if (audible) engine.systemResampler_.Render(m.audio, reinterpret_cast<int16_t*>(output), bytes / 4, 2, SampleRate, m.volume.load());
}
} // namespace reagba
