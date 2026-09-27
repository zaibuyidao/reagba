#include "core/gba/GBACore.h"
#include "core/gb/GBPCM.h"
#include "rom/ROMManager.h"
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <mgba/core/blip_buf.h>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <mutex>
namespace reagba {
namespace {
struct CloseVF {
    void operator()(VFile *f) const {
        if (f)
            f->close(f);
    }
};
using VF = std::unique_ptr<VFile, CloseVF>;
struct GBPCMStream : mAVStream {
    GBPCM pcm;
    GBPCMStream() : mAVStream{} {
        postAudioFrame = [](mAVStream* stream, int16_t left, int16_t right) {
            static_cast<GBPCMStream*>(stream)->pcm.Push(left, right);
        };
    }
};
} // namespace
struct GBACore::Impl {
    mCore *core = nullptr;
    Frame pixels{};
    EmulatorSystem system = EmulatorSystem::GBA;
    bool config = false;
    GBPCMStream gbAudio;
    ~Impl() {
        if (core) {
            if (config)
                mCoreConfigDeinit(&core->config);
            core->deinit(core);
        }
    }
};
GBACore::GBACore() : impl_(std::make_unique<Impl>()) {
    static std::once_flag once;
    std::call_once(once, [] {
        static mLogger logger{[](mLogger *, int, mLogLevel level, const char *format, va_list args) {
                                  if (level & (mLOG_FATAL | mLOG_ERROR)) {
                                      std::vfprintf(stderr, format, args);
                                      std::fputc('\n', stderr);
                                  }
                              },
                              nullptr};
        mLogSetDefaultLogger(&logger);
    });
}
GBACore::~GBACore() = default;
void GBACore::LoadROM(const fs::path &path, const fs::path &bios) {
    const auto romInfo = InspectROM(path, false);
    impl_ = std::make_unique<Impl>();
    impl_->system = romInfo.system;
    const bool gba = impl_->system == EmulatorSystem::GBA;
    auto *c = impl_->core = mCoreCreate(gba ? mPLATFORM_GBA : mPLATFORM_GB);
    if (!c)
        throw std::runtime_error("Could not allocate mGBA");
    if (!c->init(c)) {
        std::free(c);
        impl_->core = nullptr;
        throw std::runtime_error("Could not initialize mGBA");
    }
    mCoreInitConfig(c, "reagba");
    impl_->config = true;
    mCoreConfigSetDefaultIntValue(&c->config, "sampleRate", SampleRate);
    mCoreConfigSetDefaultIntValue(&c->config, "audioBuffers", 1024);
    mCoreConfigSetDefaultIntValue(&c->config, "volume", 0x100);
    mCoreConfigSetDefaultIntValue(&c->config, "useBios", gba && !bios.empty() ? 1 : 0);
    mCoreConfigSetDefaultIntValue(&c->config, "skipBios", 1);
    if (!gba) {
        mCoreConfigSetDefaultIntValue(&c->config, "sgb.borders", 0);
        mCoreConfigSetDefaultValue(&c->config, "gb.model", "DMG");
        mCoreConfigSetDefaultValue(&c->config, "sgb.model", "DMG");
    }
    mCoreLoadConfig(c);
    static_assert(sizeof(color_t) == sizeof(uint32_t), "ReaGBA requires a 32-bit mGBA framebuffer");
    // Keep the existing 240x160 transport. GB/GBC occupy its centered 160x144 area.
    c->setVideoBuffer(c, reinterpret_cast<color_t *>(impl_->pixels.data()) + (gba ? 0 : 8 * Width + 40), Width);
    c->setAudioBufferSize(c, 2048);
    if (!gba) c->setAVStream(c, &impl_->gbAudio);
    auto bytes = ReadBytes(path, 32 * 1024 * 1024);
    VF rom(VFileMemChunk(bytes.data(), bytes.size()));
    if (!rom || !c->loadROM(c, rom.get()))
        throw std::runtime_error("mGBA rejected ROM");
    rom.release();
    // Attach writable backing before reset/detection. Without it, mGBA's
    // savedataRestore can succeed while discarding a pre-boot battery save.
    VF save(VFileMemChunk(nullptr, 0));
    if (!save || !c->loadSave(c, save.get()))
        throw std::runtime_error("Could not attach cartridge save memory");
    save.release();
    if (gba && !bios.empty()) {
        auto bytes = ReadBytes(bios, 16384);
        if (bytes.size() != 16384)
            throw std::runtime_error("GBA BIOS must be exactly 16384 bytes");
        VF vf(VFileMemChunk(bytes.data(), bytes.size()));
        if (!vf || !c->loadBIOS(c, vf.get(), 0))
            throw std::runtime_error("mGBA rejected BIOS");
        vf.release();
    }
    Reset();
}
void GBACore::Reset() {
    auto *c = impl_->core;
    c->reset(c);
    for (int i = 0; i < 2; ++i) {
        auto *b = c->getAudioChannel(c, i);
        blip_set_rates(b, c->frequency(c), SampleRate);
        blip_clear(b);
    }
    c->setKeys(c, 0);
    if (impl_->system != EmulatorSystem::GBA) impl_->gbAudio.pcm.Reset();
}
void GBACore::RunFrame() {
    auto *c = impl_->core;
    c->runFrame(c);
}
void GBACore::SetInput(uint32_t mask) {
    auto *c = impl_->core;
    c->setKeys(c, mask & (impl_->system == EmulatorSystem::GBA ? 1023 : 255));
}
const Frame &GBACore::GetFrame() const {
    return impl_->pixels;
}
uint32_t GBACore::ReadInput() const {
    auto *c = impl_->core;
    if (impl_->system != EmulatorSystem::GBA)
        return c->getKeys(c) & 255;
    return (~c->busRead16(c, 0x04000130)) & 1023; // GBA KEYINPUT is active-low.
}
std::vector<int16_t> GBACore::DrainAudio() {
    auto *c = impl_->core;
    auto *l = c->getAudioChannel(c, 0);
    auto *r = c->getAudioChannel(c, 1);
    if (impl_->system != EmulatorSystem::GBA) {
        // Keep mGBA's legacy buffers drained while using its pre-blip GB PCM.
        std::array<int16_t, 2048> discarded{};
        for (auto* channel : {l, r})
            while (const auto count = blip_samples_avail(channel))
                blip_read_samples(channel, discarded.data(), std::min(count, int(discarded.size())), 0);
        return impl_->gbAudio.pcm.Drain();
    }
    int count = std::min(blip_samples_avail(l), blip_samples_avail(r));
    std::vector<int16_t> samples(size_t(count) * 2);
    if (count) {
        blip_read_samples(l, samples.data(), count, 1);
        blip_read_samples(r, samples.data() + 1, count, 1);
    }
    return samples;
}
std::vector<uint8_t> GBACore::SaveState() {
    VF vf(VFileMemChunk(nullptr, 0));
    if (!vf || !mCoreSaveStateNamed(impl_->core, vf.get(), SAVESTATE_SAVEDATA | SAVESTATE_RTC))
        throw std::runtime_error("mGBA save-state failed");
    auto size = vf->size(vf.get());
    if (size <= 0 || size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid core state size");
    std::vector<uint8_t> bytes(size);
    vf->seek(vf.get(), 0, SEEK_SET);
    if (vf->read(vf.get(), bytes.data(), bytes.size()) != size)
        throw std::runtime_error("Incomplete core state");
    return bytes;
}
void GBACore::LoadState(const std::vector<uint8_t> &bytes) {
    VF vf(VFileMemChunk(bytes.data(), bytes.size()));
    if (!vf || !mCoreLoadStateNamed(impl_->core, vf.get(), SAVESTATE_SAVEDATA | SAVESTATE_RTC))
        throw std::runtime_error("mGBA could not restore state");
    for (int i = 0; i < 2; ++i)
        blip_clear(impl_->core->getAudioChannel(impl_->core, i));
    if (impl_->system != EmulatorSystem::GBA) impl_->gbAudio.pcm.Reset();
}
std::vector<uint8_t> GBACore::SaveGame() {
    void *data = nullptr;
    auto *c = impl_->core;
    size_t size = c->savedataClone(c, &data);
    std::vector<uint8_t> bytes;
    if (data && size)
        bytes.assign(static_cast<uint8_t *>(data), static_cast<uint8_t *>(data) + size);
    std::free(data);
    return bytes;
}
void GBACore::LoadGameSave(const std::vector<uint8_t> &bytes) {
    auto *c = impl_->core;
    if (impl_->system != EmulatorSystem::GBA) {
        // Reattach GB SRAM so memory banks and RTC footer use the restored file.
        VF save(VFileMemChunk(bytes.data(), bytes.size()));
        if (!save || !c->loadSave(c, save.get()))
            throw std::runtime_error("mGBA rejected battery save");
        save.release();
        return;
    }
    if (!c->savedataRestore(c, bytes.data(), bytes.size(), true))
        throw std::runtime_error("mGBA rejected battery save");
}
std::string GBACore::Version() const {
    return "mGBA/0.10.5;ReaGBA-state/1";
}
EmulatorSystem GBACore::GetSystem() const {
    return impl_->system;
}
} // namespace reagba
