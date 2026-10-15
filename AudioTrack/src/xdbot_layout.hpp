#pragma once

// Offsets inside zilko.xdbot.dll (Windows). Same method as xdpc/XdBotBridgePC:
// a build is only used when the PE TimeDateStamp and SizeOfImage match and the
// first bytes of every listed function are identical. The menu entries
// (pageTable, addRow, loadPage) were derived by disassembling
// RecordLayer::setup (0x177e10) of v2.7.7.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace at {

struct Layout {
    // Function RVAs
    std::uint32_t globalGet = 0;       // Global* Global::get()
    std::uint32_t getCurrentFrame = 0; // int Global::getCurrentFrame(bool)
    std::uint32_t getTps = 0;          // float Global::getTPS()
    std::uint32_t addRow = 0;          // RecordLayer::addRow(Entry*, float y)
    std::uint32_t loadPage = 0;        // RecordLayer::loadPage(int)

    // Data RVAs
    std::uint32_t globalInstance = 0;
    std::uint32_t macroVtable = 0;
    std::uint32_t pageTable = 0; // std::vector<std::vector<MenuEntry>>, filled once at DLL load

    // Global (relative to the instance)
    std::uint32_t macro = 0;     // Macro Global::macro
    std::uint32_t state = 0;     // int: 0 none, 1 recording, 2 playing
    std::uint32_t pageIndex = 0; // int: current page of the Settings column

    // Macro (relative to Global::macro)
    std::uint32_t macroFramerate = 0; // float
    std::uint32_t macroInputs = 0;    // std::vector<input>

    // input
    std::uint32_t inputStride = 0;
    std::uint32_t inputFrame = 0; // uint32
    std::uint32_t inputDown = 0;  // bool

    std::uint32_t rowsPerPage = 6;
};

inline constexpr std::size_t kFunctionCount = 5;

struct KnownBuild {
    char const* description;
    std::uint32_t timeDateStamp;
    std::uint32_t sizeOfImage;
    Layout layout;
    // First 8 bytes of globalGet, getCurrentFrame, getTps, addRow, loadPage.
    std::array<std::array<std::uint8_t, 8>, kFunctionCount> prologues;
};

inline std::vector<KnownBuild> const& knownBuilds() {
    static std::vector<KnownBuild> const builds = {
        // zilko.xdbot.dll, TimeDateStamp 0x6a99c9fe
        KnownBuild{
            "xdBot v2.7.7 win64",
            0x6a99c9fe, 0x38f000,
            Layout{
                .globalGet = 0x10d00,
                .getCurrentFrame = 0x6ec60,
                .getTps = 0x6eb90,
                .addRow = 0x17c9b0,
                .loadPage = 0x169a90,
                .globalInstance = 0x33fc88,
                .macroVtable = 0x260468,
                .pageTable = 0x3487f0,
                .macro = 0x10,
                .state = 0x728,
                .pageIndex = 0x1040,
                .macroFramerate = 0x54,
                .macroInputs = 0xd0,
                .inputStride = 0x30,
                .inputFrame = 0x8,
                .inputDown = 0x15,
                .rowsPerPage = 6,
            },
            {{
                {0x55, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8d, 0x6c}, // globalGet
                {0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48}, // getCurrentFrame
                {0x55, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8d, 0x6c}, // getTps
                {0x55, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81}, // addRow
                {0x56, 0x57, 0x48, 0x81, 0xec, 0xf8, 0x00, 0x00}, // loadPage
            }},
        },
    };
    return builds;
}

inline std::array<std::uint32_t, kFunctionCount> functionRvas(Layout const& l) {
    return {l.globalGet, l.getCurrentFrame, l.getTps, l.addRow, l.loadPage};
}

} // namespace at
