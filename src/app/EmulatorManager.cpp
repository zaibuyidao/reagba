#include "app/EmulatorManager.h"
#include "core/gba/GBACore.h"
#include "input/KeyBindings.h"
#include <chrono>
#include <cmath>
#ifndef REAGBA_VERSION
#define REAGBA_VERSION "dev"
#endif
namespace reagba {
using Clock = std::chrono::steady_clock;
EmulatorManager::EmulatorManager(fs::path romDir, fs::path dataDir)
    : saves_(std::move(dataDir)), covers_(saves_.Root() / "cache" / "covers"), romDir_(std::move(romDir)) {
    auto config = saves_.Root() / "config" / "preferences.json";
    if (fs::exists(config))
        try {
            preferences_ = Json::parse(ReadBytes(config, 1024 * 1024));
            if (!preferences_.is_object())
                preferences_ = Json::object();
        } catch (const std::exception &) {
        }
    if (preferences_.contains("volume") && preferences_["volume"].is_number())
        volume.store(std::clamp(preferences_["volume"].get<float>(), 0.f, 1.f));
    fs::create_directories(romDir_);
    if (!preferences_.contains("rom_directory") || !preferences_["rom_directory"].is_string() || preferences_["rom_directory"] == "")
        preferences_["rom_directory"] = romDir_.u8string();
    NormalizeKeys(preferences_);
    try {
        preferences_["gamepad_bindings"] = NormalizeGamepadBindings(preferences_.value("gamepad_bindings", Json::object()));
    } catch (const std::exception&) {
        preferences_["gamepad_bindings"] = DefaultGamepadBindings();
    }
    gamepad_.Configure(preferences_["gamepad_bindings"]);
    keyboardTurbo_.Configure({{"a", {{"target", "a"}, {"mode", "turbo"}}}, {"b", {{"target", "b"}, {"mode", "turbo"}}}});
    const auto mode = preferences_.value("audio_output", Json());
    if (mode != "system" && mode != "reaper_output" && mode != "reaper_track") preferences_["audio_output"] = "system";
    const auto track = preferences_.value("audio_track", Json());
    if (track != "preview" && track != "selected") preferences_["audio_track"] = "preview";
    const auto channel = preferences_.value("audio_channel", Json());
    if (!channel.is_number_integer() || channel < 0 || channel > 1023) preferences_["audio_channel"] = 0;
    if (!preferences_.value("audio_mono", Json()).is_boolean()) preferences_["audio_mono"] = false;
    audioOutput_ = {preferences_["audio_output"], preferences_["audio_track"], preferences_["audio_channel"], preferences_["audio_mono"]};
    if (!preferences_.contains("auto_download_covers") || !preferences_["auto_download_covers"].is_boolean())
        preferences_["auto_download_covers"] = false;
    covers_.Enable(preferences_["auto_download_covers"].get<bool>());
    worker_ = std::thread(&EmulatorManager::Run, this);
}
EmulatorManager::~EmulatorManager() {
    Shutdown();
}
void EmulatorManager::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    covers_.Shutdown();
}
void EmulatorManager::Submit(Json command, Reply reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (quit_)
        return;
    if (queue_.size() >= 128) {
        reply({{"ok", false}, {"error", "Command queue is full"}});
        return;
    }
    queue_.push_back({std::move(command), std::move(reply)});
    cv_.notify_one();
}
Json EmulatorManager::State() const {
    return {{"loaded", bool(core_)},
            {"running", running_},
            {"fps", fps_},
            {"frames", frameCount_},
            {"speed", EffectiveSpeed()},
            {"base_speed", speed_},
            {"input_mask", appliedInput_},
            {"observed_input", observedInput_},
            {"core_input_mask", core_ ? core_->ReadInput() : 0},
            {"volume", volume.load()},
            {"frame_skip", frameSkip_},
            {"game", core_ ? current_.ToJson() : Json(nullptr)},
            {"app_version", REAGBA_VERSION},
            {"core", "mGBA 0.10.5"},
            {"system", "GBA"},
            {"error", lastError_}};
}
void EmulatorManager::Persist() {
    preferences_["volume"] = volume.load();
    WriteJSON(saves_.Root() / "config" / "preferences.json", preferences_);
}
Json EmulatorManager::Handle(const Json &cmd) {
    const auto action = cmd.at("action").get<std::string>();
    if (action == "get_emulator_state" || action == "get_game_info")
        return State();
    if (action == "scan_roms") {
        auto path = cmd.contains("directory") ? fs::u8path(cmd.at("directory").get<std::string>())
                                               : fs::u8path(preferences_.value("rom_directory", std::string()));
        library_ = path.empty() ? Json::array() : ScanROMs(path);
        for (auto &r : library_) {
            auto key = r["path"].get<std::string>();
            r["favorite"] = preferences_.value("favorites", Json::object()).value(key, false);
            r["play_seconds"] = preferences_.value("play_seconds", Json::object()).value(key, 0.0);
            r["last_played"] = preferences_.value("last_played", Json::object()).value(key, int64_t(0));
        }
        return library_;
    }
    if (action == "get_settings")
        return preferences_;
    if (action == "get_cover")
        return covers_.Get(cmd.at("code").get<std::string>());
    if (action == "set_settings") {
        auto settings = cmd.at("settings");
        if (!settings.is_object())
            throw std::runtime_error("settings must be an object");
        if (settings.contains("audio_output") && settings["audio_output"] != "system" &&
            settings["audio_output"] != "reaper_output" && settings["audio_output"] != "reaper_track")
            throw std::runtime_error("Unknown audio output");
        if (settings.contains("audio_track") && settings["audio_track"] != "preview" && settings["audio_track"] != "selected")
            throw std::runtime_error("Unknown audio track target");
        if (settings.contains("audio_channel") && (!settings["audio_channel"].is_number_integer() ||
            settings["audio_channel"] < 0 || settings["audio_channel"] > 1023))
            throw std::runtime_error("Invalid audio output channel");
        if (settings.contains("audio_mono") && !settings["audio_mono"].is_boolean())
            throw std::runtime_error("Audio mono must be a boolean");
        if (settings.contains("auto_download_covers") && !settings["auto_download_covers"].is_boolean())
            throw std::runtime_error("Automatic cover download must be a boolean");
        for (const auto *key : {"rom_directory", "last_rom_directory"})
            if (settings.contains(key) &&
                (!settings[key].is_string() || settings[key].get<std::string>().empty()))
                throw std::runtime_error("ROM directories must be non-empty paths");
        if (settings.contains("turbo_keys")) {
            auto normalized = settings;
            NormalizeKeys(normalized);
            if (normalized["turbo_keys"] != settings["turbo_keys"])
                throw std::runtime_error("Invalid turbo key bindings");
        }
        if (settings.contains("gamepad_bindings"))
            settings["gamepad_bindings"] = NormalizeGamepadBindings(settings["gamepad_bindings"]);
        for (auto it = settings.begin(); it != settings.end(); ++it)
            if (it.key() == "gamepad_bindings" || it.key() == "turbo_keys" || it.key() == "keys" || it.key() == "bios" || it.key() == "fast_forward_key" ||
                it.key() == "rom_directory" || it.key() == "last_rom_directory" || it.key() == "auto_download_covers" ||
                it.key() == "audio_output" || it.key() == "audio_track" || it.key() == "audio_channel" || it.key() == "audio_mono")
                preferences_[it.key()] = it.value();
        Persist();
        {
            std::lock_guard<std::mutex> lock(audioOutputMutex_);
            audioOutput_ = {preferences_["audio_output"], preferences_["audio_track"], preferences_["audio_channel"], preferences_["audio_mono"]};
        }
        if (settings.contains("gamepad_bindings")) gamepad_.Configure(preferences_["gamepad_bindings"]);
        covers_.Enable(preferences_.value("auto_download_covers", false));
        return preferences_;
    }
    if (action == "favorite") {
        preferences_["favorites"][cmd.at("path").get<std::string>()] = cmd.at("value").get<bool>();
        Persist();
        return true;
    }
    if (action == "set_volume") {
        float value = cmd.at("value").get<float>();
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::runtime_error("Volume must be 0–1");
        volume.store(value);
        Persist();
        return State();
    }
    if (action == "set_speed") {
        double value = cmd.at("value").get<double>();
        if (value != 1 && value != 2 && value != 4)
            throw std::runtime_error("Speed must be 1, 2 or 4");
        speed_ = value;
        return State();
    }
    if (action == "set_frame_skip") {
        int value = cmd.at("value").get<int>();
        if (value < 0 || value > 5)
            throw std::runtime_error("Frame skip must be 0–5");
        frameSkip_ = value;
        return State();
    }
    if (action == "load_rom") {
        const auto selected=fs::absolute(fs::u8path(cmd.at("path").get<std::string>()));
        preferences_["last_rom_directory"]=selected.parent_path().u8string();
        Persist();
        auto rom = InspectROM(selected);
        auto next = std::make_unique<GBACore>();
        next->LoadROM(rom.path, fs::u8path(preferences_.value("bios", std::string())));
        saves_.LoadBattery(*next, rom);
        if (core_)
            saves_.SaveBattery(*core_, current_);
        core_ = std::move(next);
        current_ = std::move(rom);
        keys_ = 0;
        running_ = true;
        frameCount_ = 0;
        appliedInput_ = observedInput_ = 0;
        fps_ = 0;
        lastError_.clear();
        preferences_["last_played"][current_.path.u8string()] = std::time(nullptr);
        Persist();
        return State();
    }
    if (!core_)
        throw std::runtime_error("Load a GBA ROM first");
    if (action == "start")
        running_ = true;
    else if (action == "pause") {
        running_ = false;
        saves_.SaveBattery(*core_, current_);
        Persist();
    } else if (action == "reset") {
        saves_.SaveBattery(*core_, current_);
        core_->Reset();
        keys_ = 0;
    } else if (action == "stop") {
        saves_.SaveBattery(*core_, current_);
        Persist();
        core_.reset();
        running_ = false;
        keys_ = 0;
        Frame blank{};
        frames.Publish(blank);
    } else if (action == "save_state")
        saves_.SaveSlot(*core_, current_, cmd.at("slot").get<int>());
    else if (action == "load_state") {
        saves_.LoadSlot(*core_, current_, cmd.at("slot").get<int>());
        core_->SetInput(0);
        keys_ = 0;
        core_->RunFrame();
        core_->DrainAudio();
        frames.Publish(core_->GetFrame());
    } else if (action == "get_save_states")
        return saves_.Slots(current_);
    else if (action == "save_game")
        saves_.SaveBattery(*core_, current_);
    else if (action == "screenshot") {
        auto path = saves_.Root() / "screenshots" /
                    (current_.hash + "-" + std::to_string(std::time(nullptr)) + ".bmp");
        WriteScreenshot(path, core_->GetFrame());
        return path.u8string();
    } else
        throw std::runtime_error("Unknown action: " + action);
    return State();
}
void EmulatorManager::Run() {
    auto next = Clock::now(), meter = next, autosave = next;
    uint64_t measured = 0;
    while (true) {
        std::deque<Request> requests;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!running_ && queue_.empty() && !quit_)
                cv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
            if (quit_)
                break;
            requests.swap(queue_);
        }
        uint32_t gamepadKeys;
        {
            std::lock_guard<std::mutex> lock(gamepadMutex_);
            gamepadKeys = gamepad_.Apply(gamepadInput_,
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(), gamepadRawInputs_);
        }
        const auto turboKeys = keyboardTurbo_.Apply(turboKeys_.load(),
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
        gamepadFastForward_ = (gamepadKeys & GamepadFastForwardMask) != 0;
        for (auto &request : requests) {
            try {
                request.reply({{"ok", true}, {"result", Handle(request.command)}});
            } catch (const std::exception &e) {
                request.reply({{"ok", false}, {"error", e.what()}});
            }
        }
        const double speed = EffectiveSpeed();
        audible.store(running_ && speed == 1);
        if (!running_ || !core_) {
            next = Clock::now();
            meter = next;
            measured = 0;
            continue;
        }
        try {
            appliedInput_ = keys_.load() | turboKeys | (gamepadKeys & 1023);
            if ((appliedInput_ & 0x30) == 0x30) appliedInput_ &= ~0x30u;
            if ((appliedInput_ & 0xc0) == 0xc0) appliedInput_ &= ~0xc0u;
            core_->SetInput(appliedInput_);
            observedInput_ |= core_->ReadInput();
            core_->RunFrame();
            ++frameCount_;
            ++measured;
            if (frameCount_ % uint64_t(frameSkip_ + 1) == 0)
                frames.Publish(core_->GetFrame());
            auto samples = core_->DrainAudio();
            if (speed == 1)
                audio.Push(samples.data(), samples.size());
            auto key = current_.path.u8string();
            auto &time = preferences_["play_seconds"][key];
            if (!time.is_number())
                time = 0.0;
            time = time.get<double>() + 1.0 / (NativeFPS * speed);
            auto now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - meter).count();
            if (elapsed >= 1) {
                fps_ = measured / elapsed;
                meter = now;
                measured = 0;
            }
            if (now - autosave >= std::chrono::seconds(10)) {
                saves_.SaveBattery(*core_, current_);
                Persist();
                autosave = now;
            }
            next += std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / (NativeFPS * speed)));
            if (now - next > std::chrono::milliseconds(100))
                next = now;
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_until(lock, next, [&] { return quit_ || !queue_.empty(); });
        } catch (const std::exception &e) {
            running_ = false;
            audible = false;
            lastError_ = e.what();
        }
    }
    audible = false;
    try {
        if (core_)
            saves_.SaveBattery(*core_, current_);
        Persist();
    } catch (const std::exception &e) {
        lastError_ = e.what();
        WriteJSON(saves_.Root() / "shutdown-error.json", {{"error", lastError_}});
    }
    core_.reset();
}
} // namespace reagba
