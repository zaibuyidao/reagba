#include "core/gba/GBACore.h"
#include "app/EmulatorManager.h"
#include "bridge/CoreCommands.h"
#include "save/SaveManager.h"
#include "input/KeyBindings.h"
#include "extension/RuntimePaths.h"
#include <iostream>
#include <future>
#include <set>
#include <chrono>
#include <cstring>
#include <fstream>
using namespace reagba;
static void Require(bool b, const std::string &message) {
    if (!b)
        throw std::runtime_error(message);
}
static Json Call(EmulatorManager &manager, Json cmd);
static std::string FrameHash(const Frame &frame) {
    auto *data = reinterpret_cast<const uint8_t *>(frame.data());
    return SHA256({data, data + sizeof(frame)});
}
static void SelfTest() {
    Require(SHA256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            "SHA-256 empty vector");
    Require(SHA256({'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 abc");
    AudioBuffer ring;
    std::vector<int16_t> in(10000), out(10000);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = int16_t(i);
    Require(ring.Push(in.data(), in.size()) == 8192, "Ring capacity");
    Require(ring.Pop(out.data(), 4000) == 4000, "Ring read");
    Require(std::equal(out.begin(), out.begin() + 4000, in.begin()), "Ring data");
    Require(ring.Push(in.data(), 4000) == 4000, "Ring wrap");
    Require(ring.Pop(out.data(), 8192) == 8192, "Wrapped ring read");
    Require(std::equal(out.begin(), out.begin() + 4192, in.begin() + 4000), "Ring preserves unread tail");
    Require(std::equal(out.begin() + 4192, out.begin() + 8192, in.begin()), "Ring preserves wrapped samples");
    FrameBuffer frames;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        Frame f{};
        for (uint32_t i = 1; i <= 10000; ++i) {
            f.fill(i);
            frames.Publish(f);
        }
        done = true;
    });
    uint32_t previous = 0;
    bool intact = true;
    while (!done || frames.Consume()) {
        if (frames.Consume()) {
            auto &f = frames.Front();
            intact &= f.front() >= previous &&
                      std::all_of(f.begin(), f.end(), [&](auto p) { return p == f.front(); });
            previous = f.front();
        }
    }
    producer.join();
    Require(intact, "Triple buffer tear or old frame");
    const auto settingsRoot=fs::temp_directory_path()/("reagba-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto data=ResolveRuntimePaths(settingsRoot).data;
    Require(data==settingsRoot/"Scripts"/"zaibuyidao Scripts"/"Modules"/"ReaGBA","Runtime data path mismatch");
    const auto defaultROM=data/"roms",customROM=settingsRoot/"Games";
    fs::create_directories(customROM);
    {
        auto instance=std::make_unique<EmulatorManager>(defaultROM,data);
        auto &manager=*instance;
        for (const auto *folder : {"config", "cache", "saves", "screenshots", "states", "roms"})
            Require(fs::is_directory(data/folder),"Missing runtime folder: "+std::string(folder));
        Require(!fs::exists(settingsRoot/"Scripts"/"zaibuyidao Scripts"/"ReaGBA"),"Legacy runtime directory created");
        Require(!fs::exists(data/"ROM"),"Uppercase ROM directory created");
        auto settings=Call(manager,{{"action","get_settings"}}).at("result");
        Require(settings.at("rom_directory")=="","ROM folder must remain empty until selected");
        Require(settings.at("auto_download_covers")==false,"Covers must default to offline");
        Require(settings.at("keys")==DefaultKeys() && settings.at("fast_forward_key")=="L","Default keyboard mismatch");
        Require(std::abs(manager.volume.load()-.3f)<.0001f,"Volume must default to 30 percent");
        auto customKeys=DefaultKeys();customKeys[0]="F";customKeys[8]="E";
        Require(Call(manager,{{"action","set_settings"},{"settings",{{"keys",customKeys},{"fast_forward_key","R"}}}}).value("ok",false),"Custom keys rejected");
        Require(Call(manager,{{"action","set_volume"},{"value",.45}}).value("ok",false),"Custom volume rejected");
        Require(!Call(manager,{{"action","set_settings"},{"settings",{{"auto_download_covers","true"}}}}).value("ok",true),"Invalid cover setting accepted");
        Require(Call(manager,{{"action","set_settings"},{"settings",{{"auto_download_covers",true}}}}).at("result").at("auto_download_covers")==true,"Cover setting rejected");
        Require(Call(manager,{{"action","set_settings"},{"settings",{{"rom_directory",customROM.u8string()}}}}).value("ok",false),"ROM folder setting rejected");
        Require(!Call(manager,{{"action","set_settings"},{"settings",{{"rom_directory",""}}}}).value("ok",true),"Empty ROM folder accepted");
        std::ofstream(customROM/"broken.gba",std::ios::binary).put('\0');
        Require(!Call(manager,{{"action","load_rom"},{"path",(customROM/"broken.gba").u8string()}}).value("ok",true),"Invalid ROM accepted");
        manager.Shutdown();
    }
    {
        auto instance=std::make_unique<EmulatorManager>(defaultROM,data);
        auto &manager=*instance;
        const auto settings=Call(manager,{{"action","get_settings"}}).at("result");
        Require(fs::u8path(settings.at("rom_directory").get<std::string>())==customROM,"ROM folder did not survive restart");
        Require(fs::u8path(settings.at("last_rom_directory").get<std::string>())==customROM,"Last opened ROM folder did not survive restart");
        Require(settings.at("keys")[0]=="F" && settings.at("keys")[8]=="E" && settings.at("fast_forward_key")=="R","Custom keys did not survive restart");
        Require(std::abs(manager.volume.load()-.45f)<.0001f,"Custom volume did not survive restart");
        Require(settings.at("auto_download_covers")==true,"Cover setting did not survive restart");
    }
    fs::remove_all(settingsRoot);
    std::cout << "PASS: SHA-256, bounded audio ring, concurrent triple buffer, persisted ROM folders, input and audio settings\n";
}
static Json Call(EmulatorManager &manager, Json cmd) {
    std::promise<Json> promise;
    auto future = promise.get_future();
    manager.Submit(cmd, [&](Json j) { promise.set_value(j); });
    Require(future.wait_for(std::chrono::seconds(20)) == std::future_status::ready,
            "Manager response timeout");
    return future.get();
}
static int Verify(const fs::path &path, const fs::path &output, int totalFrames) {
    SelfTest();
    fs::create_directories(output);
    auto rom = InspectROM(path);
    SaveManager saves(output / "isolated-data");
    GBACore core;
    core.LoadROM(path);
    std::set<std::string> hashes;
    uint64_t sampleCount = 0, nonzero = 0;
    int peak = 0;
    std::vector<int16_t> audio;
    Json checkpoints = Json::array();
    auto began = std::chrono::steady_clock::now();
    for (int i = 0; i < totalFrames; ++i) {
        // Repeated Start/A pulses advance boot, title, file selection and intro.
        uint32_t keys = 0;
        if (i >= 600 && i % 180 < 8)
            keys = 1u << unsigned(Button::Start);
        if (i >= 780 && i % 90 >= 30 && i % 90 < 38)
            keys |= 1u << unsigned(Button::A);
        if (i >= 2200 && i % 240 < 60)
            keys |= 1u << unsigned(Button::Right);
        core.SetInput(keys);
        core.RunFrame();
        auto pcm = core.DrainAudio();
        sampleCount += pcm.size() / 2;
        for (auto s : pcm) {
            if (s)
                ++nonzero;
            peak = std::max(peak, std::abs(int(s)));
        }
        if (audio.size() < SampleRate * 2 * 10)
            audio.insert(audio.end(), pcm.begin(), pcm.end());
        if (i % 60 == 59)
            hashes.insert(FrameHash(core.GetFrame()));
        if (i == 299 || i == 899 || i == 1799 || i == totalFrames - 1) {
            auto name = "frame-" + std::to_string(i + 1) + ".bmp";
            WriteScreenshot(output / name, core.GetFrame());
            checkpoints.push_back(
                {{"frame", i + 1}, {"image", name}, {"sha256", FrameHash(core.GetFrame())}});
        }
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    Require(hashes.size() > 5, "ROM did not produce changing video");
    Require(nonzero > 1000 && peak > 100, "ROM did not produce non-silent PCM");
    core.SetInput(0);
    auto baseline = core.SaveState();
    saves.SaveSlot(core, rom, 1);
    for (int i = 0; i < 60; ++i) {
        core.RunFrame();
        core.DrainAudio();
    }
    auto expected = FrameHash(core.GetFrame());
    saves.LoadSlot(core, rom, 1);
    core.SetInput(0);
    for (int i = 0; i < 60; ++i) {
        core.RunFrame();
        core.DrainAudio();
    }
    Require(expected == FrameHash(core.GetFrame()), "State replay framebuffer differs");
    // Input must affect actual emulated state, not only a frontend key variable.
    core.LoadState(baseline);
    core.SetInput(0);
    for (int i = 0; i < 12; ++i)
        core.RunFrame();
    auto released = core.SaveState();
    core.LoadState(baseline);
    core.SetInput((1 << unsigned(Button::Start)) | (1 << unsigned(Button::Right)));
    for (int i = 0; i < 12; ++i)
        core.RunFrame();
    auto pressed = core.SaveState();
    Require(released != pressed, "GBA keypad input did not reach emulated state");
    core.SetInput(0);
    saves.SaveBattery(core, rom);
    auto battery = core.SaveGame();
    Require(!battery.empty(), "Expected cartridge save memory");
    GBACore reopened;
    reopened.LoadROM(path);
    saves.LoadBattery(reopened, rom);
    Require(battery == reopened.SaveGame(), "Battery save did not survive reopening ROM");
    bool rejected = false;
    auto wrong = rom;
    wrong.hash = std::string(64, '0');
    auto source = saves.Root() / "states" / (rom.hash + "-1.state"),
         destination = saves.Root() / "states" / (wrong.hash + "-1.state");
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    try {
        saves.LoadSlot(core, wrong, 1);
    } catch (const std::exception &) {
        rejected = true;
    }
    Require(rejected, "Wrong ROM state accepted");
    auto corrupt = ReadBytes(source);
    corrupt.back() ^= 1;
    AtomicWrite(saves.Root() / "states" / (rom.hash + "-2.state"), corrupt);
    rejected = false;
    try {
        saves.LoadSlot(core, rom, 2);
    } catch (const std::exception &) {
        rejected = true;
    }
    Require(rejected, "Corrupt state accepted");
    EmulatorManager manager(path.parent_path(), output / "manager-data");
    auto load = Call(manager, {{"action", "load_rom"}, {"path", path.u8string()}});
    Require(load.value("ok", false), "Manager load failed");
    Require(Call(manager, {{"action", "pause"}}).value("ok", false), "Manager pause failed");
    auto before = Call(manager, {{"action", "get_emulator_state"}})["result"]["frames"];
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Require(before == Call(manager, {{"action", "get_emulator_state"}})["result"]["frames"],
            "Paused core advanced");
    Require(!Call(manager, {{"action", "set_speed"}, {"value", -1}}).value("ok", true),
            "Invalid speed accepted");
    Call(manager, {{"action", "set_speed"}, {"value", 2}});
    manager.SetFastForward(true);
    Require(Call(manager, {{"action", "get_emulator_state"}})["result"]["speed"] == 4,
            "Hold boost uses 4x");
    manager.SetFastForward(false);
    Require(Call(manager, {{"action", "get_emulator_state"}})["result"]["speed"] == 2,
            "Release boost restores selected speed");
    Call(manager, {{"action", "set_speed"}, {"value", 1}});
    Require(!Call(manager, {{"action", "save_state"}, {"slot", 10}}).value("ok", true),
            "Invalid slot accepted");
    Require(Call(manager, {{"action", "save_state"}, {"slot", 1}}).value("ok", false), "Manager save state");
    Require(Call(manager, {{"action", "load_state"}, {"slot", 1}}).value("ok", false), "Manager load state");
    Require(Call(manager, {{"action", "stop"}}).value("ok", false), "Manager stop");
    manager.Shutdown();
    std::vector<uint8_t> wav(44 + audio.size() * 2);
    auto put = [&](int p, uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i)
            wav[p + i] = uint8_t(v >> (8 * i));
    };
    std::memcpy(wav.data(), "RIFF", 4);
    put(4, uint32_t(wav.size() - 8), 4);
    std::memcpy(wav.data() + 8, "WAVEfmt ", 8);
    put(16, 16, 4);
    put(20, 1, 2);
    put(22, 2, 2);
    put(24, SampleRate, 4);
    put(28, SampleRate * 4, 4);
    put(32, 4, 2);
    put(34, 16, 2);
    std::memcpy(wav.data() + 36, "data", 4);
    put(40, uint32_t(audio.size() * 2), 4);
    std::memcpy(wav.data() + 44, audio.data(), audio.size() * 2);
    AtomicWrite(output / "audio-first-10s.wav", wav);
    Json report = {{"passed", true},
                   {"rom", rom.ToJson()},
                   {"core", core.Version()},
                   {"frames", totalFrames},
                   {"emulated_seconds", totalFrames / NativeFPS},
                   {"wall_seconds", seconds},
                   {"unthrottled_fps", totalFrames / seconds},
                   {"distinct_sampled_frames", hashes.size()},
                   {"audio_sample_frames", sampleCount},
                   {"nonzero_audio_samples", nonzero},
                   {"audio_peak", peak},
                   {"battery_bytes", battery.size()},
                   {"state_deterministic_replay", true},
                   {"input_changes_emulated_state", true},
                   {"battery_reopen_roundtrip", true},
                   {"wrong_rom_state_rejected", true},
                   {"corrupted_state_rejected", true},
                   {"manager_commands_passed", true},
                   {"screenshots", checkpoints}};
    WriteJSON(output / "report.json", report);
    std::cout << report.dump(2) << '\n';
    return 0;
}
static int Entry(const std::vector<std::string> &args) {
    try {
        if (args.size() == 2 && args[1] == "--self-test") {
            SelfTest();
            return 0;
        }
        if (args.size() < 3) {
            std::cerr << "Usage: reagba_verify ROM.gba OUTPUT_DIRECTORY [frames=3600]\n";
            return 2;
        }
        int frames = args.size() > 3 ? std::stoi(args[3]) : 3600;
        Require(frames >= 1800 && frames <= 1000000, "Frames must be 1800–1000000");
        return Verify(fs::u8path(args[1]), fs::u8path(args[2]), frames);
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return Entry(args);
}
#else
int main(int argc, char **argv) {
    return Entry({argv, argv + argc});
}
#endif
