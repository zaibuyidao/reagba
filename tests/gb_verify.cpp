#include "core/gba/GBACore.h"
#include "app/EmulatorManager.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <set>

using namespace reagba;
static void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
static Json Call(EmulatorManager &manager, Json command) {
    std::promise<Json> promise;
    auto future = promise.get_future();
    manager.Submit(command, [&](Json reply) { promise.set_value(std::move(reply)); });
    Require(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "Manager timeout");
    auto reply = future.get();
    if (!reply.value("ok", false)) throw std::runtime_error(reply.dump());
    return reply.at("result");
}
// Original test cartridge: tile pattern, channel-1 tone, joypad reads into SRAM.
static std::vector<uint8_t> Cartridge(uint8_t color, uint8_t mapper = 3) {
    std::vector<uint8_t> rom(32768);
    const uint8_t logo[] = {
        0xce,0xed,0x66,0x66,0xcc,0x0d,0x00,0x0b,0x03,0x73,0x00,0x83,0x00,0x0c,0x00,0x0d,
        0x00,0x08,0x11,0x1f,0x88,0x89,0x00,0x0e,0xdc,0xcc,0x6e,0xe6,0xdd,0xdd,0xd9,0x99,
        0xbb,0xbb,0x67,0x63,0x6e,0x0e,0xec,0xcc,0xdd,0xdc,0x99,0x9f,0xbb,0xb9,0x33,0x3e};
    std::copy(std::begin(logo), std::end(logo), rom.begin() + 0x104);
    rom[0x100] = 0xc3; rom[0x101] = 0x50; rom[0x102] = 0x01;
    rom[0x143] = color; rom[0x147] = mapper; rom[0x149] = mapper ? 2 : 0;
    rom[0x146] = 3; rom[0x14b] = 0x33; // Exercise SGB-capable headers without a border.
    for (int i = 0x134; i <= 0x14c; ++i) rom[0x14d] = uint8_t(rom[0x14d] - rom[i] - 1);
    size_t pc = 0x150;
    auto emit = [&](std::initializer_list<uint8_t> bytes) { for (auto b : bytes) rom[pc++] = b; };
    auto store = [&](uint16_t address, uint8_t value) { emit({0x3e, value, 0xea, uint8_t(address), uint8_t(address >> 8)}); };
    emit({0xf3, 0x31, 0xfe, 0xff}); // DI, stack.
    store(0xff40, 0); store(0x0000, 0x0a); store(0xff47, 0xe4);
    for (int row = 0; row < 8; ++row) {
        store(uint16_t(0x8000 + row * 2), 0x55);
        store(uint16_t(0x8001 + row * 2), 0x33);
    }
    if (color) {
        store(0xff68, 0x80);
        for (auto value : {0xff,0x7f,0x1f,0x00,0xe0,0x03,0x00,0x7c}) store(0xff69, uint8_t(value));
    }
    store(0xff26, 0x80); store(0xff24, 0x77); store(0xff25, 0x11);
    store(0xff11, 0x80); store(0xff12, 0xf0); store(0xff13, 0); store(0xff14, 0x87);
    store(0xff40, 0x91);
    const auto loop = pc;
    store(0xff00, 0x20); emit({0xf0,0x00,0xea,0x00,0xa0});
    store(0xff00, 0x10); emit({0xf0,0x00,0xea,0x01,0xa0});
    emit({0xc3, uint8_t(loop), uint8_t(loop >> 8)});
    return rom;
}
static void Run(GBACore &core, int frames = 3) {
    for (int i = 0; i < frames; ++i) { core.RunFrame(); core.DrainAudio(); }
}
static void Verify(const fs::path &path, const fs::path &root, EmulatorSystem system) {
    const auto info = InspectROM(path);
    Require(info.system == system && info.headerChecksum && info.code.empty(), "GB ROM metadata");
    GBACore core;
    core.LoadROM(path, root / "nonexistent-gba-bios.bin");
    Require(core.GetSystem() == system, "Selected core system");
    size_t samples = 0, nonzero = 0;
    for (int i = 0; i < 120; ++i) {
        core.RunFrame();
        const auto pcm = core.DrainAudio(); samples += pcm.size() / 2;
        nonzero += std::count_if(pcm.begin(), pcm.end(), [](int16_t s) { return s != 0; });
    }
    Require(samples > 64000 && samples < 67000 && nonzero > 1000, "GB audio rate/tone");
    std::set<uint32_t> colors;
    const auto &frame = core.GetFrame();
    for (int y = 0; y < Height; ++y) for (int x = 0; x < Width; ++x) {
        if (x < 40 || x >= 200 || y < 8 || y >= 152) Require(frame[y * Width + x] == 0, "GB frame exceeded native bounds");
        else colors.insert(frame[y * Width + x]);
    }
    Require(colors.size() >= 4, "GB tile output");
    if (system == EmulatorSystem::GBC)
        Require(std::any_of(colors.begin(), colors.end(), [](uint32_t p) { return (p & 255) != ((p >> 8) & 255); }), "CGB color output");
    for (unsigned key = 0; key < 8; ++key) {
        core.SetInput((1u << key) | 768); Run(core);
        Require(core.ReadInput() == (1u << key), "GB key mask/LR filtering");
        const auto ram = core.SaveGame();
        Require(ram.size() >= 8192, "GB battery size");
        Require((ram[key < 4 ? 1 : 0] & 15) == (15 ^ (1u << (key % 4))), "Emulated JOYP input");
    }
    core.SetInput(0); Run(core);
    auto battery = core.SaveGame(); battery[123] = 0x5a;
    core.LoadGameSave(battery); Require(core.SaveGame()[123] == 0x5a, "GB SRAM restore mapping");
    SaveManager saves(root / path.stem());
    saves.SaveBattery(core, info); saves.SaveSlot(core, info, 1);
    auto bmp = ReadBytes(saves.Root() / "states" / (info.hash + "-1.bmp"));
    Require(bmp.size() == 54 + 160 * 144 * 3 && bmp[18] == 160 && bmp[22] == 144, "GB screenshot dimensions");
    battery[123] = 0xa5; core.LoadGameSave(battery);
    Run(core, 12); const auto expected = core.GetFrame();
    saves.LoadSlot(core, info, 1); Run(core, 12); Require(expected == core.GetFrame(), "GB state replay");
    Require(core.SaveGame()[123] == 0x5a, "GB state did not restore SRAM");
    battery[123] = 0x5a;
    auto wrong = info; wrong.system = EmulatorSystem::GBA;
    bool rejected = false;
    try { saves.LoadSlot(core, wrong, 1); } catch (...) { rejected = true; }
    Require(rejected, "Cross-system state accepted");
    GBACore reopened; reopened.LoadROM(path); saves.LoadBattery(reopened, info);
    Require(reopened.SaveGame() == battery, "GB battery reopen");
    reopened.Reset(); Run(reopened); Require(reopened.SaveGame()[123] == 0x5a, "GB reset lost SRAM");
    reopened.SetInput(1); Run(reopened); const auto pressed = reopened.SaveGame();
    reopened.SetInput(0); Run(reopened); Require(pressed[1] != reopened.SaveGame()[1], "GB release");
}
int main() {
    const auto root = fs::temp_directory_path() / ("reagba-gb-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root / "roms" / "nested");
        const auto gb = root / "roms" / "test.GB", gbc = root / "roms" / "nested" / "color.GbC";
        const auto hybrid = root / "roms" / "hybrid.gb", rtc = root / "roms" / "rtc.gb";
        AtomicWrite(gb, Cartridge(0)); AtomicWrite(gbc, Cartridge(0xc0));
        AtomicWrite(hybrid, Cartridge(0x80)); AtomicWrite(rtc, Cartridge(0, 0x10));
        AtomicWrite(root / "roms" / "invalid.gb", {1, 2, 3});
        Verify(gb, root, EmulatorSystem::GB); Verify(gbc, root, EmulatorSystem::GBC);
        Verify(hybrid, root, EmulatorSystem::GBC); Verify(rtc, root, EmulatorSystem::GB);
        const auto noSave = root / "roms" / "no-save.gb";
        AtomicWrite(noSave, Cartridge(0, 0));
        {
            GBACore core; core.LoadROM(noSave); Run(core);
            Require(core.SaveGame().empty(), "ROM-only cartridge allocated SRAM");
            const auto state = core.SaveState(); Run(core); core.LoadState(state);
        }
        // Minimal ARM loop preserves the existing GBA loading and save envelope path.
        std::vector<uint8_t> gba(1024); gba[0xb2] = 0x96;
        gba[0] = 0xfe; gba[1] = 0xff; gba[2] = 0xff; gba[3] = 0xea;
        const auto gbaPath = root / "roms" / "advance.gba"; AtomicWrite(gbaPath, gba);
        Require(ScanROMs(root / "roms").size() == 6, "Mixed recursive ROM scan");
        {
            auto manager = std::make_unique<EmulatorManager>(root / "roms", root / "manager");
            for (const auto &path : {gbaPath, gb, gbc, gbaPath}) {
                const auto info = InspectROM(path);
                const auto state = Call(*manager, {{"action", "load_rom"}, {"path", path.u8string()}});
                Require(state.at("system") == SystemName(info.system), "Manager system transition");
                Call(*manager, {{"action", "pause"}});
                Call(*manager, {{"action", "save_state"}, {"slot", 1}});
                Call(*manager, {{"action", "load_state"}, {"slot", 1}});
                Call(*manager, {{"action", "reset"}});
                Call(*manager, {{"action", "start"}});
            }
            Call(*manager, {{"action", "stop"}});
        }
        fs::remove_all(root);
        std::cout << "PASS: GB/GBC scan, video, audio, JOYP, SRAM/RTC, states and GBA transitions\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << " (fixtures: " << root.u8string() << ")\n";
        return 1;
    }
}
