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
    std::string sizes;
    for (auto const& page : *table) {
        auto const* raw = reinterpret_cast<RawVector const*>(&page);
        bool const empty = raw->begin == 0 && raw->end == 0;
        auto const bytes = raw->end - raw->begin;
        sizes += empty ? "0 " : fmt::format("{}({:#x}) ", bytes / sizeof(MenuEntry), bytes);
        if (empty) continue; // an empty page is fine
        if (raw->begin == 0 || raw->end <= raw->begin || bytes % sizeof(MenuEntry) != 0 ||
            bytes / sizeof(MenuEntry) > 64) {
            log::error("Audiotrack: xdBot menu page has an unexpected shape (entries/bytes so far: {})", sizes);
            return false;
        }
        for (auto const& entry : page) {
            if (!sane(entry)) {
                log::error("Audiotrack: xdBot menu entry failed the sanity check (label size {}, key size {}, type {}, "
                           "scale {})",
                           entry.label.size(), entry.key.size(), entry.type, entry.scale);
                return false;
            }
            if (entry.key == kAudiotrackRowKey) return true; // already installed
        }
    }
    log::info("Audiotrack: xdBot menu table has {} pages, entries per page: {}", pages, sizes);

    // Pages can hold more than the six rows that are drawn, so only a page with
    // fewer than six entries has a free row; otherwise add a page of our own.
    MenuEntry row{"Audiotrack", kAudiotrackRowKey, 2, 0.4f, reinterpret_cast<void*>(callback), false};
    if (table->back().size() < layout.rowsPerPage) {
        table->back().push_back(std::move(row));
    } else {
        table->emplace_back();
        table->back().push_back(std::move(row));
    }
    log::info("Audiotrack: added the menu row on page {} of {}", table->size(), table->size());
    return true;
}

} // namespace at
