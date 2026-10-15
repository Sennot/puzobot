#include "xdbot_api.hpp"

#include <Geode/Geode.hpp>

#include <Windows.h>

#include <cstring>

using namespace geode::prelude;

namespace at {
namespace {

constexpr char const* kXdBotId = "zilko.xdbot";

struct RawVector {
    std::uintptr_t begin;
    std::uintptr_t end;
    std::uintptr_t capacity;
};

using GetCurrentFrameFn = int (*)(bool);
using GetTpsFn = float (*)();

HMODULE findXdBotModule() {
    if (auto* module = GetModuleHandleW(L"zilko.xdbot.dll")) return module;
    if (auto* mod = Loader::get()->getLoadedMod(kXdBotId))
        return GetModuleHandleW(mod->getBinaryPath().wstring().c_str());
    return nullptr;
}

} // namespace

XdBotApi& XdBotApi::get() {
    static XdBotApi instance;
    return instance;
}

bool XdBotApi::attach() {
    if (m_ready) return true;
    if (m_fatal) return false;

    auto* mod = Loader::get()->getLoadedMod(kXdBotId);
    if (!mod) {
        m_status = "xdBot (zilko.xdbot) is not loaded";
        return false;
    }
    auto* module = findXdBotModule();
    if (!module) {
        m_status = "zilko.xdbot.dll is not loaded";
        return false;
    }
    auto const base = reinterpret_cast<std::uintptr_t>(module);
    auto const* dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    auto const stamp = nt->FileHeader.TimeDateStamp;
    auto const size = nt->OptionalHeader.SizeOfImage;

    KnownBuild const* build = nullptr;
    for (auto const& candidate : knownBuilds())
        if (candidate.timeDateStamp == stamp && candidate.sizeOfImage == size) build = &candidate;
    if (!build) {
        m_status = fmt::format("unsupported xdBot build {} (dll stamp {:08x})", mod->getVersion().toVString(), stamp);
        m_fatal = true;
        log::error("Audiotrack: {}", m_status);
        return false;
    }

    auto const rvas = functionRvas(build->layout);
    for (std::size_t i = 0; i < rvas.size(); ++i) {
        if (rvas[i] >= size ||
            std::memcmp(reinterpret_cast<void const*>(base + rvas[i]), build->prologues[i].data(), 8) != 0) {
            m_status = fmt::format("xdBot code differs from the verified {} (function #{})", build->description, i);
            m_fatal = true;
            log::error("Audiotrack: {}", m_status);
            return false;
        }
    }

    m_layout = build->layout;
    m_base = base;
    m_global = base + m_layout.globalInstance;
    m_status = std::string("attached to ") + build->description;
    m_ready = true;
    log::info("Audiotrack: {}", m_status);
    return true;
}

bool XdBotApi::globalReady() const {
    if (!m_ready) return false;
    auto const vptr = field<std::uintptr_t>(m_layout.macro);
    return vptr == m_base + m_layout.macroVtable;
}

BotState XdBotApi::state() const {
    if (!globalReady()) return BotState::None;
    auto const value = field<int>(m_layout.state);
    if (value == 1) return BotState::Recording;
    if (value == 2) return BotState::Playing;
    return BotState::None;
}

int XdBotApi::currentFrame() const {
    if (!globalReady()) return 0;
    return reinterpret_cast<GetCurrentFrameFn>(m_base + m_layout.getCurrentFrame)(false);
}

float XdBotApi::tps() const {
    if (!globalReady()) return 240.0f;
    return reinterpret_cast<GetTpsFn>(m_base + m_layout.getTps)();
}

MacroInfo XdBotApi::macro() const {
    MacroInfo info;
    if (!globalReady()) return info;
    auto const& v = field<RawVector>(m_layout.macro + m_layout.macroInputs);
    if (v.begin == 0 || v.end < v.begin || (v.end - v.begin) % m_layout.inputStride != 0) return info;
    auto const count = (v.end - v.begin) / m_layout.inputStride;
    if (count == 0 || count > 5'000'000) return info;

    auto const frameAt = [&](std::size_t i) {
        return *reinterpret_cast<std::uint32_t const*>(v.begin + i * m_layout.inputStride + m_layout.inputFrame);
    };
    info.inputCount = count;
    info.firstFrame = frameAt(0);
    info.lastFrame = frameAt(count - 1);
    auto const fps = field<float>(m_layout.macro + m_layout.macroFramerate);
    info.framerate = (fps >= 1.f && fps <= 10000.f) ? fps : 240.0;

    // The same macro must map to the same sidecar file, a different macro must not.
    std::uint64_t hash = 1469598103934665603ull;
    auto const mix = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(count);
    mix(static_cast<std::uint64_t>(info.framerate * 1000.0));
    for (std::size_t i = 0; i < count && i < 64; ++i) mix(frameAt(i));
    for (std::size_t i = count > 64 ? count - 64 : 0; i < count; ++i) mix(frameAt(i));
    info.fingerprint = hash;
    info.valid = true;
    return info;
}

std::vector<std::uint32_t> XdBotApi::pressFrames(std::size_t limit) const {
    std::vector<std::uint32_t> out;
    if (!globalReady()) return out;
    auto const& v = field<RawVector>(m_layout.macro + m_layout.macroInputs);
    if (v.begin == 0 || v.end < v.begin || (v.end - v.begin) % m_layout.inputStride != 0) return out;
    auto const count = (v.end - v.begin) / m_layout.inputStride;
    if (count > 5'000'000) return out;
    for (std::size_t i = 0; i < count && out.size() < limit; ++i) {
        auto const element = v.begin + i * m_layout.inputStride;
        if (*reinterpret_cast<std::uint8_t const*>(element + m_layout.inputDown) != 0)
            out.push_back(*reinterpret_cast<std::uint32_t const*>(element + m_layout.inputFrame));
    }
    return out;
}

} // namespace at
