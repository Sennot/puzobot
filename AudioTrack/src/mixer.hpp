#pragma once

// The Audiotrack mixer: reads the real microphone, mixes the click wave into
// it and plays the result to the playback device of a virtual cable (VB-Cable's
// "CABLE Input"), which loops it back into the cable's capture device. Apps
// (OBS, Discord, ShadowPlay) then use that capture device as their microphone.
//
//   real mic --WASAPI capture--> ring --+
//                                       +--> mix --WASAPI render--> Audiotrack Out
//   wav voices (stereo float 48 kHz) ---+
//
// Everything runs on two worker threads; the public methods are thread-safe.

#include "wav.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace at {

class Mixer {
public:
    using VoiceId = std::uint64_t;

    static Mixer& get();

    // Starts the worker threads. Returns false (see status()) when the
    // Audiotrack devices are not installed or cannot be opened.
    bool start();
    void stop();
    bool running() const { return m_running.load(); }
    std::string status() const;

    // True when a virtual cable's playback device exists: VB-Cable's "CABLE Input"
    // (or whatever setCableName() says), or Audiotrack's own driver.
    bool cableAvailable() const;
    // Part of the cable's playback device name. Default "CABLE Input". Takes effect on the next start().
    void setCableName(std::string name);

    // Name (substring) of the real microphone to pass through. Empty = the
    // default capture device. Takes effect on the next start().
    void setMicName(std::string name);
    // Real microphone -> cable. Off = only the wav is sent.
    void setPassthrough(bool on) { m_passthrough = on; }

    // Decodes a wav ahead of time (otherwise play() does it).
    bool prepare(std::string const& wavUtf8, std::string& error);
    // Starts a voice at `fromSeconds` inside the wav. It ends by itself at
    // `endSeconds` (0 = end of file). Returns 0 when nothing was started.
    VoiceId play(std::string const& wavUtf8, double fromSeconds, float volume, double endSeconds);
    void stopVoice(VoiceId id);
    void stopAll();
    // Applies to all voices: game pause and the game's time scale (speedhack).
    void setTransport(bool paused, float rate);

private:
    Mixer() = default;
    ~Mixer();

    struct Voice {
        VoiceId id = 0;
        std::shared_ptr<const StereoClip> clip;
        double position = 0.0; // in clip frames
        double endFrame = 0.0;
        float volume = 1.f;
    };

    std::string cableName() const;
    void captureThread(std::string micName);
    void renderThread();
    void mixInto(float* out, std::uint32_t frames);
    void setStatus(std::string text);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_passthrough{true};
    std::atomic<bool> m_paused{false};
    std::atomic<float> m_rate{1.f};
    std::atomic<VoiceId> m_nextId{1};

    std::thread m_capture;
    std::thread m_render;
    std::string m_micName;
    std::string m_cableName = "CABLE Input";

    mutable std::mutex m_mutex; // guards voices, clip cache and status
    std::vector<Voice> m_voices;
    std::string m_clipPath;
    std::shared_ptr<const StereoClip> m_clip;
    std::string m_status = "not started";

    // Real microphone -> render thread (single producer / single consumer).
    static constexpr std::uint32_t kMicRingFrames = 48000;
    std::vector<float> m_micRing = std::vector<float>(kMicRingFrames * 2, 0.f);
    std::atomic<std::uint64_t> m_micWritten{0};
    std::atomic<std::uint64_t> m_micRead{0};
    bool m_micPrimed = false; // render thread only
};

} // namespace at
