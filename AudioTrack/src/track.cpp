#include "track.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace at {

std::string pathToUtf8(std::filesystem::path const& path) {
    auto const text = path.u8string();
    return std::string(reinterpret_cast<char const*>(text.data()), text.size());
}

std::filesystem::path pathFromUtf8(std::string const& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<char8_t const*>(text.data()), text.size()));
}

std::filesystem::path trackFile(std::uint64_t fingerprint) {
    return Mod::get()->getSaveDir() / "tracks" / fmt::format("{:016x}.json", fingerprint);
}

std::optional<Track> loadTrack(std::uint64_t fingerprint) {
    auto const path = trackFile(fingerprint);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;
    auto text = utils::file::readString(path);
    if (!text) return std::nullopt;
    auto json = matjson::parse(text.unwrap());
    if (!json) return std::nullopt;
    auto const& value = json.unwrap();

    Track track;
    track.wavPath = value["wav"].asString().unwrapOr("");
    track.anchorSeconds = value["anchor"].asDouble().unwrapOr(0.0);
    track.audioEndSeconds = value["audio_end"].asDouble().unwrapOr(0.0);
    track.macroEndFrame = static_cast<std::int64_t>(value["macro_end_frame"].asDouble().unwrapOr(-1.0));
    track.volume = static_cast<float>(value["volume"].asDouble().unwrapOr(1.0));
    track.enabled = value["enabled"].asBool().unwrapOr(true);
    track.toMic = value["to_mic"].asBool().unwrapOr(false);
    track.local = value["local"].asBool().unwrapOr(true);
    return track;
}

bool saveTrack(std::uint64_t fingerprint, Track const& track) {
    auto const path = trackFile(fingerprint);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    auto value = matjson::Value::object();
    value["wav"] = track.wavPath;
    value["anchor"] = track.anchorSeconds;
    value["audio_end"] = track.audioEndSeconds;
    value["macro_end_frame"] = static_cast<double>(track.macroEndFrame);
    value["volume"] = static_cast<double>(track.volume);
    value["enabled"] = track.enabled;
    value["to_mic"] = track.toMic;
    value["local"] = track.local;
    return utils::file::writeStringSafe(path, value.dump()).isOk();
}

} // namespace at
