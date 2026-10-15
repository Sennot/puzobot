#pragma once

// Read-only access to the loaded xdBot (Windows). Main thread only.

#include "xdbot_layout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace at {

enum class BotState : int { None = 0, Recording = 1, Playing = 2 };

struct MacroInfo {
    bool valid = false;
    std::size_t inputCount = 0;
    std::uint32_t firstFrame = 0; // frame of the first input
    std::uint32_t lastFrame = 0;  // frame of the last input
    double framerate = 240.0;
    std::uint64_t fingerprint = 0; // identifies the loaded macro (sidecar key)
};

class XdBotApi {
public:
    static XdBotApi& get();

    // Verifies the xdBot DLL against the known builds. Cheap once it succeeded;
    // after a failure it is not retried (the build will not change).
    bool attach();
    bool ready() const { return m_ready; }
    std::string const& status() const { return m_status; }

    Layout const& layout() const { return m_layout; }
    std::uintptr_t address(std::uint32_t rva) const { return m_base + rva; }

    BotState state() const;
    int currentFrame() const;
    float tps() const;

    // False until xdBot has created its Global (first macro operation).
    bool globalReady() const;
    MacroInfo macro() const;
    // Frames where a button went down (any player), ascending, at most `limit`.
    std::vector<std::uint32_t> pressFrames(std::size_t limit = 40000) const;

private:
    XdBotApi() = default;

    template <class T>
    T& field(std::uint32_t offset) const {
        return *reinterpret_cast<T*>(m_global + offset);
    }

    bool m_ready = false;
    bool m_fatal = false;
    std::string m_status = "xdBot not attached";
    Layout m_layout;
    std::uintptr_t m_base = 0;
    std::uintptr_t m_global = 0;
};

} // namespace at
