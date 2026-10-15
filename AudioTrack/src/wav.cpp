#include "wav.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace at {
namespace {

std::uint32_t rd32(unsigned char const* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint16_t rd16(unsigned char const* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::int16_t clamp16(double value) {
    return static_cast<std::int16_t>(std::clamp(value, -32768.0, 32767.0));
}

// A parsed file: the bytes plus where the samples are.
struct Raw {
    std::vector<unsigned char> bytes;
    std::uint16_t format = 0; // 1 = PCM, 3 = float
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t rate = 0;
    std::size_t dataPos = 0;
    std::size_t frames = 0;

    bool isFloat() const { return format == 3; }

    // One sample normalised to [-1, 1].
    double sample(std::size_t frame, int channel) const {
        auto const bytesPerSample = static_cast<std::size_t>(bits / 8);
        auto const* p = bytes.data() + dataPos + (frame * channels + channel) * bytesPerSample;
        if (isFloat() && bits == 32) {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        if (isFloat()) {
            double d;
            std::memcpy(&d, p, 8);
            return d;
        }
        if (bits == 8) return (static_cast<int>(p[0]) - 128) / 128.0;
        if (bits == 16) return static_cast<std::int16_t>(rd16(p)) / 32768.0;
        if (bits == 24) {
            std::int32_t s = (p[0] << 8) | (p[1] << 16) | (static_cast<std::int32_t>(p[2]) << 24);
            return (s >> 8) / 8388608.0;
        }
        return static_cast<std::int32_t>(rd32(p)) / 2147483648.0;
    }
};

bool parse(std::filesystem::path const& file, Raw& raw, std::string& error) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "cannot open the file";
        return false;
    }
    auto const size = static_cast<std::size_t>(stream.tellg());
    if (size < 44 || size > (std::size_t{1} << 30)) {
        error = "file is too small or too large for a wav";
        return false;
    }
    raw.bytes.resize(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(raw.bytes.data()), static_cast<std::streamsize>(size));
    if (!stream) {
        error = "cannot read the file";
        return false;
    }
    auto const& bytes = raw.bytes;
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }

    std::size_t dataLen = 0;
    for (std::size_t pos = 12; pos + 8 <= size;) {
        auto const* chunk = bytes.data() + pos;
        std::size_t len = rd32(chunk + 4);
        auto const body = pos + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && len >= 16 && body + 16 <= size) {
            raw.format = rd16(chunk + 8);
            raw.channels = rd16(chunk + 10);
            raw.rate = rd32(chunk + 12);
            raw.bits = rd16(chunk + 22);
            if (raw.format == 0xFFFE && len >= 26 && body + 26 <= size) raw.format = rd16(chunk + 32); // extensible
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            raw.dataPos = body;
            dataLen = std::min(len, size - body);
            break;
        }
        pos = body + len + (len & 1);
    }
    if (raw.dataPos == 0 || raw.channels == 0 || raw.rate == 0) {
        error = "missing fmt or data chunk";
        return false;
    }
    bool const isFloat = raw.format == 3;
    if (!(raw.format == 1 || isFloat) ||
        !(raw.bits == 8 || raw.bits == 16 || raw.bits == 24 || raw.bits == 32 || (isFloat && raw.bits == 64))) {
        error = "unsupported wav encoding (format " + std::to_string(raw.format) + ", " + std::to_string(raw.bits) +
                " bit)";
        return false;
    }
    raw.frames = dataLen / (static_cast<std::size_t>(raw.bits / 8) * raw.channels);
    if (raw.frames == 0) {
        error = "the wav has no samples";
        return false;
    }
    return true;
}

} // namespace

std::optional<Wav> loadWav(std::filesystem::path const& file, std::string& error) {
    Raw raw;
    if (!parse(file, raw, error)) return std::nullopt;

    Wav wav;
    wav.sampleRate = static_cast<int>(raw.rate);
    wav.channels = raw.channels;
    wav.mono.resize(raw.frames);
    for (std::size_t i = 0; i < raw.frames; ++i) {
        double sum = 0.0;
        for (int c = 0; c < raw.channels; ++c) sum += raw.sample(i, c);
        wav.mono[i] = clamp16(sum / raw.channels * 32767.0);
    }

    auto const blocks = (raw.frames + Wav::kBlock - 1) / Wav::kBlock;
    wav.blockMin.assign(blocks, 0);
    wav.blockMax.assign(blocks, 0);
    for (std::size_t b = 0; b < blocks; ++b) {
        auto const from = b * Wav::kBlock;
        auto const to = std::min(raw.frames, from + Wav::kBlock);
        auto lo = wav.mono[from], hi = wav.mono[from];
        for (std::size_t i = from; i < to; ++i) {
            lo = std::min(lo, wav.mono[i]);
            hi = std::max(hi, wav.mono[i]);
        }
        wav.blockMin[b] = lo;
        wav.blockMax[b] = hi;
        wav.peak = std::max<std::int16_t>(wav.peak, std::max<int>(std::abs(lo), std::abs(hi)));
    }
    return wav;
}

std::shared_ptr<StereoClip> loadStereoClip(std::filesystem::path const& file, int targetRate, std::string& error) {
    Raw raw;
    if (!parse(file, raw, error)) return nullptr;

    auto clip = std::make_shared<StereoClip>();
    clip->sampleRate = targetRate;
    auto const outFrames = static_cast<std::size_t>(static_cast<double>(raw.frames) * targetRate / raw.rate);
    clip->samples.resize(outFrames * 2);

    // One source frame as (left, right): mono is duplicated, extra channels are ignored.
    auto const frameAt = [&](std::size_t i) {
        double const left = raw.sample(i, 0);
        double const right = raw.channels > 1 ? raw.sample(i, 1) : left;
        return std::pair<double, double>(left, right);
    };

    double const step = static_cast<double>(raw.rate) / targetRate; // source frames per output frame
    for (std::size_t o = 0; o < outFrames; ++o) {
        double const position = static_cast<double>(o) * step;
        auto const i0 = static_cast<std::size_t>(position);
        auto const i1 = std::min(i0 + 1, raw.frames - 1);
        double const t = position - static_cast<double>(i0);
        auto const a = frameAt(i0);
        auto const b = frameAt(i1);
        clip->samples[o * 2] = static_cast<float>(a.first + (b.first - a.first) * t);
        clip->samples[o * 2 + 1] = static_cast<float>(a.second + (b.second - a.second) * t);
    }
    return clip;
}

void Wav::columns(double t0, double t1, int count, std::vector<std::pair<std::int16_t, std::int16_t>>& out) const {
    out.assign(static_cast<std::size_t>(std::max(count, 0)), {0, 0});
    if (count <= 0 || t1 <= t0 || mono.empty()) return;
    double const perColumn = (t1 - t0) * sampleRate / count; // samples per column
    for (int c = 0; c < count; ++c) {
        double const s0 = t0 * sampleRate + c * perColumn;
        double const s1 = s0 + perColumn;
        if (s1 <= 0 || s0 >= static_cast<double>(mono.size())) continue;
        auto const a = static_cast<std::size_t>(std::max(0.0, std::floor(s0)));
        auto const b = std::min(mono.size(), static_cast<std::size_t>(std::max(1.0, std::ceil(s1))));
        if (b <= a) continue;
        std::int16_t lo = 32767, hi = -32768;
        if (b - a >= kBlock * 2) {
            auto const ba = a / kBlock, bb = std::min(blockMin.size(), (b + kBlock - 1) / kBlock);
            for (auto i = ba; i < bb; ++i) {
                lo = std::min(lo, blockMin[i]);
                hi = std::max(hi, blockMax[i]);
            }
        } else {
            for (auto i = a; i < b; ++i) {
                lo = std::min(lo, mono[i]);
                hi = std::max(hi, mono[i]);
            }
        }
        out[c] = {lo, hi};
    }
}

double Wav::firstClick() const {
    if (mono.empty() || peak <= 0) return 0.0;
    int const threshold = std::max(500, static_cast<int>(peak * 0.2));
    std::size_t at = mono.size();
    for (std::size_t i = 0; i < mono.size(); ++i) {
        if (std::abs(static_cast<int>(mono[i])) >= threshold) {
            at = i;
            break;
        }
    }
    if (at == mono.size()) return 0.0;
    // Walk back to where the transient starts rising (at most 5 ms).
    auto const limit = static_cast<std::size_t>(sampleRate * 0.005);
    std::size_t start = at;
    while (start > 0 && at - start < limit && std::abs(static_cast<int>(mono[start - 1])) > threshold / 10) --start;
    return static_cast<double>(start) / sampleRate;
}

} // namespace at
