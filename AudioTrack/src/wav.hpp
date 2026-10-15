#pragma once

// Minimal RIFF/WAVE reader. The editor uses it for the waveform and click
// detection, the mixer for the PCM it sends to "Audiotrack Out". Playback
// through GD itself goes through FMOD, which reads the file on its own.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace at {

struct Wav {
    static constexpr std::size_t kBlock = 256;

    int sampleRate = 0;
    int channels = 0;
    std::vector<std::int16_t> mono;      // channels averaged
    std::vector<std::int16_t> blockMin;  // min/max per kBlock samples
    std::vector<std::int16_t> blockMax;
    std::int16_t peak = 0;

    double duration() const { return sampleRate > 0 ? static_cast<double>(mono.size()) / sampleRate : 0.0; }

    // Fills `out` with min/max sample values for `columns` equal slices of
    // [t0, t1) seconds. Slices outside the file are {0, 0}.
    void columns(double t0, double t1, int columns, std::vector<std::pair<std::int16_t, std::int16_t>>& out) const;

    // Time (seconds) of the first transient above the noise floor, or 0.
    double firstClick() const;
};

std::optional<Wav> loadWav(std::filesystem::path const& file, std::string& error);

// Interleaved stereo float samples at a fixed sample rate.
struct StereoClip {
    int sampleRate = 48000;
    std::vector<float> samples; // L R L R ...
    std::size_t frames() const { return samples.size() / 2; }
};

// Decodes the whole file to stereo float and resamples it (linear) to `targetRate`.
std::shared_ptr<StereoClip> loadStereoClip(std::filesystem::path const& file, int targetRate, std::string& error);

} // namespace at
