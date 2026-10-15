#include "menu_inject.hpp"

#include "xdbot_api.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace at {
namespace {

struct RawVector {
    std::uintptr_t begin;
    std::uintptr_t end;
    std::uintptr_t capacity;
};

bool sane(MenuEntry const& e) {
    return e.label.size() <= 64 && e.key.size() <= 64 && e.type >= 0 && e.type <= 16 && e.scale > 0.f &&
           e.scale < 4.f;
}

} // namespace

bool installAudiotrackRow(void (*callback)(void*, void*)) {
    auto& api = XdBotApi::get();
    if (!api.attach()) {
        log::warn("Audiotrack: menu row not added: {}", api.status());
        return false;
    }
    auto const& layout = api.layout();
    auto* table = reinterpret_cast<MenuTable*>(api.address(layout.pageTable));

    // Check the raw vector-of-vectors before touching anything.
    auto const* outer = reinterpret_cast<RawVector const*>(table);
    if (outer->begin == 0 || outer->end <= outer->begin || (outer->end - outer->begin) % sizeof(MenuPage) != 0) {
        log::error("Audiotrack: xdBot menu table has an unexpected shape");
        return false;
    }
    auto const pages = (outer->end - outer->begin) / sizeof(MenuPage);
    if (pages > 64) {
        log::error("Audiotrack: xdBot menu table has {} pages, expected a handful", pages);
        return false;
    }
    for (auto const& page : *table) {
        auto const* raw = reinterpret_cast<RawVector const*>(&page);
        if (raw->begin == 0 || raw->end <= raw->begin || (raw->end - raw->begin) % sizeof(MenuEntry) != 0 ||
            page.size() > layout.rowsPerPage) {
            log::error("Audiotrack: xdBot menu page has an unexpected shape");
            return false;
        }
        for (auto const& entry : page) {
            if (!sane(entry)) {
                log::error("Audiotrack: xdBot menu entry failed the sanity check");
                return false;
            }
            if (entry.key == kAudiotrackRowKey) return true; // already installed
        }
    }

    MenuEntry row{"Audiotrack", kAudiotrackRowKey, 2, 0.4f, reinterpret_cast<void*>(callback), false};
    if (table->back().size() < layout.rowsPerPage) {
        table->back().push_back(std::move(row));
    } else {
        table->emplace_back();
        table->back().push_back(std::move(row));
    }
    log::info("Audiotrack: added menu row on page {} of {}", table->size(), table->size());
    return true;
}

} // namespace at
