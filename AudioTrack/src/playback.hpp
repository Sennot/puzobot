#pragma once

// Plays the Audiotrack wav through GD's FMOD while a macro is playing.
//
// There is no per-frame sync: the wav starts once at the beginning of the
// attempt (anchored so that its first click lands on the macro's first input)
// and then just runs. After the macro ends or the player dies the wave keeps
// playing until the user's "audio end" marker.

#include "mixer.hpp"
#include "track.hpp"
#include "xdbot_api.hpp"

#include <optional>
#include <string>
#include <vector>

namespace FMOD {
class Sound;
class Channel;
} // namespace FMOD

namespace at {

// Position in the wav (seconds) that corresponds to a macro frame.
double audioTimeForFrame(Track const& track, MacroInfo const& macro, double frame);
// Position in the wav (seconds) where the macro ends (death / last input).
double macroEndAudioTime(Track const& track, MacroInfo const& macro);

class Playback {
public:
    static Playback& get();

    // Loads the sidecar of the currently loaded macro if it changed.
    void refresh();
    std::optional<Track>& track() { return m_track; }
    std::uint64_t fingerprint() const { return m_fingerprint; }
    // Replaces the track of the current macro and saves it.
    void assign(Track track);

    // PlayLayer::resetLevel, after xdBot's own reset logic.
    void onReset();
    // Every scheduler tick (raw dt).
    void tick(float dt);
    void stop();

    // Editor preview of an unsaved track: plays from `seconds` until the audio
    // end marker, independent of the macro.
    bool preview(Track const& track, double seconds);
    void updatePreview(Track const& track);
    bool previewing() const { return m_preview && m_channel != nullptr; }
    // Seconds in the wav that are playing right now, or -1.
    double position() const;
    // True while playing past the macro end marker.
    bool inTail() const { return m_tail; }

private:
    Playback() = default;

    // A click wave of an attempt that ended (death) and is still ringing out
    // until its audio end marker while the next attempt already plays its own.
    struct Ring {
        FMOD::Channel* channel;
        double end; // audio end marker (seconds), 0 = until the end of the file
    };

    Track const* current() const;
    bool load(std::string const& path);
    bool begin(double seconds, double delay);
    void applyLive(bool paused);
    void stopCurrent(); // the attempt's own channel only, rings keep going
    void tickRings();

    std::optional<Track> m_track;
    Track m_previewTrack;
    std::uint64_t m_fingerprint = 0;

    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;
    Mixer::VoiceId m_mixVoice = 0; // the same wave in the microphone cable (0 = none)
    std::string m_soundPath;
    float m_frequency = 44100.f;

    double m_delay = 0.0; // game seconds before the wav starts (first click is later than frame 0)
    bool m_tail = false;
    bool m_preview = false;
    double m_macroEnd = 0.0; // audio-time of the macro end, cached at start
    std::vector<Ring> m_rings;
};

} // namespace at
