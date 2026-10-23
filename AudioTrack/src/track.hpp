#pragma once

// The per-macro Audiotrack settings. Stored as a small json file in the mod's
// save directory, keyed by the macro fingerprint, so the macro file itself is
// never touched.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace at {

struct Track {
    std::string wavPath;
    // Time in the wav of the click that lines up with the macro's first input.
    double anchorSeconds = 0.0;
    // User marker: playback stops here. 0 = play to the end of the file.
    double audioEndSeconds = 0.0;
    // Frame where the macro "ends" (death / last input). -1 = last input.
    std::int64_t macroEndFrame = -1;
    float volume = 1.0f;
    bool enabled = true;
    // Send the wave into the virtual microphone cable (mixed with the real microphone).
    bool toMic = false;
    // Play it inside the game as well (FMOD). Off = silent in the game, mic only.
    bool local = true;
};

// Paths are stored (and handed to FMOD) as UTF-8; Windows paths with non-ASCII
// characters (e.g. Cyrillic user folders) must not go through the ANSI codepage.
std::string pathToUtf8(std::filesystem::path const& path);
std::filesystem::path pathFromUtf8(std::string const& text);

std::filesystem::path trackFile(std::uint64_t fingerprint);
std::optional<Track> loadTrack(std::uint64_t fingerprint);
bool saveTrack(std::uint64_t fingerprint, Track const& track);

} // namespace at
