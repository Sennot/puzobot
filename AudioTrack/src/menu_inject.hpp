#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace at {

// One row of xdBot's Settings column. Layout found in RecordLayer::addRow
// (zilko.xdbot.dll v2.7.7): 88 bytes.
struct MenuEntry {
    std::string label;   // +0x00
    std::string key;     // +0x20  saved-setting key (unused for button rows)
    std::int32_t type;   // +0x40  2 = label + gear button, 0/1 = checkbox rows
    float scale;         // +0x44  label scale
    void* callback;      // +0x48  member-function pointer of RecordLayer: fn(layer, sender)
    bool flag;           // +0x50
};
static_assert(sizeof(MenuEntry) == 0x58, "MenuEntry layout differs from xdBot's");
static_assert(offsetof(MenuEntry, key) == 0x20 && offsetof(MenuEntry, type) == 0x40 &&
                  offsetof(MenuEntry, callback) == 0x48 && offsetof(MenuEntry, flag) == 0x50,
              "MenuEntry layout differs from xdBot's");

using MenuPage = std::vector<MenuEntry>;
using MenuTable = std::vector<MenuPage>;

constexpr char const* kAudiotrackRowKey = "audiotrack_button";

// Appends the "Audiotrack" gear row to xdBot's Settings column. `callback` is
// called with (RecordLayer*, CCObject* sender) when the gear is pressed.
bool installAudiotrackRow(void (*callback)(void* layer, void* sender));

} // namespace at
