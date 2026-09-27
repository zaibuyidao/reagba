#include "audio/AudioEngine.h"
#include "audio/AudioResampler.h"
#include "reaper_plugin.h"
#include <future>
#include <iostream>
#include <cstring>
#include <cmath>
using namespace reagba;
static void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static preview_register_t* active = nullptr;
static int projectTag, tags[3], plays = 0, stops = 0;
static int selected = 2;
static int outputs = 4;
static bool running = true;
static std::string deviceRate = "48000";
static int OutputCount() { return outputs; }
static const char* OutputName(int i) { static const char* names[] = {"Left", "Right", "Headphones L", "Headphones R"}; return names[i]; }
static int Running() { return running ? 1 : 0; }
static bool DeviceInfo(const char* key, char* text, int size) {
    std::strncpy(text, std::strcmp(key,"SRATE") == 0 ? deviceRate.c_str() : "Test device", size - 1); return running;
}
static std::string names[3] = {"ReaGBA Preview", "ReaGBA Preview", "Other"};
static ReaProject* Project(int, char*, int) { return reinterpret_cast<ReaProject*>(&projectTag); }
static MediaTrack* Track(ReaProject*, int i) { return reinterpret_cast<MediaTrack*>(&tags[i]); }
static MediaTrack* Selected(ReaProject* p, int) { return selected < 0 ? nullptr : Track(p, selected); }
static int Count(ReaProject*) { return 3; }
static bool Name(MediaTrack* t, char* text, int size) {
    for (int i = 0; i < 3; ++i) if (t == Track(nullptr, i)) { std::strncpy(text, names[i].c_str(), size - 1); return true; }
    return false;
}
static int Play(ReaProject* p, preview_register_t* v, int flags, double align) {
    Check(p == Project(0,nullptr,0) && flags == 0 && align == -1, "Preview must be immediate and unbuffered");
    Check(v->m_out_chan == -1 && v->src && !active, "Overlapping audio consumers");
    active = v; ++plays; return 1;
}
static int PlayOutput(preview_register_t* v, int flags, double align) {
    Check(v->m_out_chan>=0 && !v->preview_track && flags==0 && align==-1 && !active,"REAPER hardware output");
    active=v;++plays;return 1;
}
static int StopOutput(preview_register_t* v) { Check(active==v,"Stopping wrong output");active=nullptr;++stops;return 1; }
static int Stop(ReaProject*, preview_register_t* v) { Check(active == v, "Stopping wrong preview"); active = nullptr; ++stops; return 1; }
static void* Get(const char* name) {
#define API(n, f) if (std::strcmp(name,n)==0) return reinterpret_cast<void*>(f)
    API("EnumProjects",Project); API("CountTracks",Count); API("GetTrack",Track);
    API("GetSelectedTrack",Selected); API("GetTrackName",Name);
    API("PlayPreviewEx",PlayOutput); API("StopPreview",StopOutput);
    API("PlayTrackPreview2Ex",Play); API("StopTrackPreview2",Stop);
    API("GetNumAudioOutputs",OutputCount); API("GetOutputChannelName",OutputName);
    API("Audio_IsRunning",Running); API("GetAudioDeviceInfo",DeviceInfo);
#undef API
    return nullptr;
}
static Json Call(EmulatorManager& manager, Json settings) {
    std::promise<Json> promise; auto future = promise.get_future();
    manager.Submit({{"action", "set_settings"}, {"settings", settings}}, [&](Json value) { promise.set_value(value); });
    Check(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Settings timeout");
    return future.get();
}
template<class Sample = double>
static void Drift(double rate, int frames, double ppm, double seconds = 600) {
    AudioBuffer ring; AudioResampler resampler;
    std::vector<int16_t> pcm(1200, 8192);
    std::vector<Sample> output(size_t(frames) * 2);
    const Sample expected = std::is_integral_v<Sample> ? Sample(8192) : Sample(.25);
    double next = 0, produced = 0, fraction = 0;
    size_t low = AudioBuffer::Capacity, high = 0;
    for (double now = 0; now < seconds; now += frames / rate) {
        // Whole GBA frames arrive in bursts, with deterministic scheduling jitter.
        while (next <= now) {
            fraction += 32768.0 / NativeFPS;
            const int count = int(fraction); fraction -= count;
            Check(ring.Push(pcm.data(), size_t(count) * 2) == size_t(count) * 2, "Clock drift overflowed buffer");
            const double speed = 1 + (now < seconds / 2 ? ppm : -ppm) / 1e6;
            produced += 1.0 / (NativeFPS * speed);
            next = produced + std::sin(produced * 7.0) * .002;
        }
        resampler.Render(ring, output.data(), frames, 2, rate, 1);
        Check(std::abs(resampler.Correction()) <= .00500001, "Clock correction exceeded limit");
        if (now > 1) {
            for (auto sample : output) Check(sample == expected, "Drift introduced a dropout or changed gain");
            low = std::min(low, ring.Available() / 2); high = std::max(high, ring.Available() / 2);
        }
    }
    std::cout << "Drift " << seconds << " s / " << rate << " Hz / " << frames << " frames / +/-" << std::abs(ppm) << " ppm: buffer " << low << ".." << high << '\n';
    Check(!resampler.Underruns() && !resampler.Resyncs(), "Stable clocks needed a discontinuous resync");
    Check(low > 100 && high < size_t(32768 * frames / rate) + 2400, "Clock drift caused unbounded latency");
    Check(std::abs(resampler.Correction() + ppm / 1e6) < .0005, "Clock servo did not track drift reversal");
    // Stalls recover by rebuffering, excess latency is discarded by the consumer.
    ring.Discard(); resampler.Render(ring, output.data(), frames, 2, rate, 1);
    Check(resampler.Underruns() == 1, "Missing producer did not rebuffer");
    std::vector<int16_t> backlog(AudioBuffer::Capacity, 8192); ring.Push(backlog.data(), backlog.size());
    resampler.Render(ring, output.data(), frames, 2, rate, 1);
    Check(resampler.Resyncs() == 1 && output.back() == expected, "Callback stall did not recover");
}
int main() {
    try {
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        for (double rate : {32768., 44100., 48000., 96000., 192000.}) {
            AudioBuffer ring; AudioResampler resampler;
            int16_t input[4096]; for (int i = 0; i < 2048; ++i) { input[i*2] = int16_t(i*8); input[i*2+1] = int16_t(-i*8); }
            ring.Push(input, 4096);
            double output[256*4]; double position = 0;
            for (int block = 0; block < 3; ++block) {
                resampler.Render(ring, output, 256, 4, rate, .5);
                for (int i = 0; i < 256; ++i) {
                    const double expected = position * 8 * .5 / 32768.;
                    Check(std::abs(output[i*4]-expected)<1e-10, "Resampling rate or block continuity");
                    Check(output[i*4+1] == -output[i*4] && output[i*4+2] == 0 && output[i*4+3] == 0, "Stereo/channel isolation");
                    position += 32768. / rate * (1 + resampler.Correction());
                }
            }
            ring.Discard(); resampler.Reset(); resampler.Render(ring,output,256,4,rate,1);
            for (double sample : output) Check(sample==0, "Underrun must produce silence");
            ring.Push(input,4096);resampler.Render(ring,output,256,1,rate,1);
            for (int i=0;i<256;++i) Check(output[i]==0,"Mono downmix");
        }
        Drift(44100, 128, 1000); Drift(48000, 512, -3000);
        Drift(96000, 64, 3000); Drift(44100, 4096, -1000);
        Drift<int16_t>(32768, 512, 3000);
        Drift(48000, 512, 200, 7200);
        const auto root=fs::temp_directory_path()/("reagba-audio-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root/"config");
        WriteJSON(root/"config/preferences.json",{{"audio_output","reaper_track"},{"audio_track","preview"}});
        {
            EmulatorManager manager(root/"roms",root);
            // No SDL audio initialization: persisted REAPER modes cannot require a hardware device.
            AudioEngine engine(manager,Get);
            Check(!SDL_WasInit(SDL_INIT_AUDIO),"REAPER output initialized SDL audio");
            Check(engine.Available() && active->preview_track==Track(nullptr,0),"Persisted track output");
            auto set=[&](const char* mode,const char* target) {
                Check(Call(manager,{{"audio_output",mode},{"audio_track",target}}).value("ok",false),"Audio settings rejected");engine.Update();
            };
            set("reaper_track","preview");Check(active->preview_track==Track(nullptr,0),"First named track wins");
            const int before=plays;engine.Update();Check(plays==before,"Unchanged output restarted");
            names[0]="Renamed";engine.Update();Check(active->preview_track==Track(nullptr,1),"Next named track");
            names[1]="Renamed too";engine.Update();Check(active->preview_track==Track(nullptr,2),"Selected fallback");
            selected=-1;engine.Update();Check(!engine.Available() && engine.Status()["audio_error"]=="","No target must be quietly silent");
            selected=0;engine.Update();Check(engine.Available() && active->preview_track==Track(nullptr,0),"Selection recovery");
            names[1]="ReaGBA Preview";set("reaper_track","selected");Check(active->preview_track==Track(nullptr,0),"Explicit selection ignores named track");
            selected=2;engine.Update();Check(active->preview_track==Track(nullptr,2),"Follow selection");
            Check(!Call(manager,{{"audio_output",false}}).value("ok",true),"Invalid output accepted");
            Check(!Call(manager,{{"audio_track","bogus"}}).value("ok",true),"Invalid target accepted");
            engine.Update();Check(active->preview_track==Track(nullptr,2),"Invalid settings changed route");
            double samples[128*2]; PCM_source_transfer_t block{};
            block.samples=samples;block.length=128;block.nch=2;block.samplerate=48000;
            active->src->GetSamples(&block);Check(block.samples_out==128,"Silent live source ended");
            for(double sample:samples)Check(sample==0,"Paused source not silent");
            set("reaper_output","preview");Check(active && active->m_out_chan==0,"REAPER output switch");
            Check(engine.Outputs()["reaper_device"]["channels"].size()==4,"Output inventory missing");
            Check(Call(manager,{{"audio_channel",2}}).value("ok",false),"Stereo selection rejected");engine.Update();
            Check(active && active->m_out_chan==2,"Output selection was ignored");
            outputs=2;engine.Update();Check(!active && engine.Status()["audio_state"]=="channel_unavailable","Missing output silently rerouted");
            outputs=4;engine.Update();Check(active && active->m_out_chan==2,"Hardware reconnect failed");
            Check(Call(manager,{{"audio_channel",3},{"audio_mono",true}}).value("ok",false),"Mono selection rejected");engine.Update();
            Check(active && active->m_out_chan==(1024|3),"Mono channel mapping");
            running=false;engine.Update();Check(!active && engine.Status()["audio_state"]=="engine_stopped","Closed hardware not detected");
            running=true;engine.Update();Check(active,"Hardware engine did not recover");
            int previous=plays;deviceRate="96000";engine.Update();Check(plays==previous+1,"Device format change did not restart source");
            for (const auto& value : {Json(-1),Json(1024),Json(1.5),Json(true),Json("0")})
                Check(!Call(manager,{{"audio_channel",value}}).value("ok",true),"Invalid channel accepted");
            Check(!Call(manager,{{"audio_mono",1}}).value("ok",true),"Invalid mono flag accepted");
            set("system","preview");Check(!active,"System output leaves preview running");
            Check(engine.Available() && SDL_WasInit(SDL_INIT_AUDIO),"System device unavailable");
            set("reaper_track","preview");Check(active,"Switch back to REAPER");
            Check(!SDL_WasInit(SDL_INIT_AUDIO),"REAPER output retained SDL audio");
        }
        Check(!active && plays==stops,"Preview leaked at destruction");
        {
            EmulatorManager manager(root/"roms",root);
            Check(manager.GetAudioOutput().mode=="reaper_track","Output persistence");
            Check(manager.GetAudioOutput().channel==3 && manager.GetAudioOutput().mono,"Hardware output persistence");
            AudioEngine engine(manager);Check(!engine.Available() && engine.Status()["audio_error"]!="","Unavailable host silently fell back to SDL");
        }
        fs::remove_all(root);
        std::cout<<"PASS: adaptive clock recovery, rate conversion, channel selection, device recovery, target precedence, persistence and preview lifecycle\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
