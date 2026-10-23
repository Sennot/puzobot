#include "playback.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <chrono>
#include <thread>

using namespace geode::prelude;

namespace at {

double audioTimeForFrame(Track const& track, MacroInfo const& macro, double frame) {
    return track.anchorSeconds + (frame - static_cast<double>(macro.firstFrame)) / macro.framerate;
}

double macroEndAudioTime(Track const& track, MacroInfo const& macro) {
    double const endFrame = track.macroEndFrame >= 0 ? static_cast<double>(track.macroEndFrame)
                                                     : static_cast<double>(macro.lastFrame);
    return audioTimeForFrame(track, macro, endFrame);
}

Playback& Playback::get() {
    static Playback instance;
    return instance;
}

Track const* Playback::current() const {
    if (m_preview) return &m_previewTrack;
    return m_track ? &*m_track : nullptr;
}

void Playback::refresh() {
    auto const info = XdBotApi::get().macro();
    if (!info.valid || info.fingerprint == m_fingerprint) return;
    m_fingerprint = info.fingerprint;
    m_track = loadTrack(m_fingerprint);
}

void Playback::assign(Track track) {
    if (m_fingerprint == 0) return;
    saveTrack(m_fingerprint, track);
    m_track = std::move(track);
}

bool Playback::load(std::string const& path) {
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) return false;
    if (m_sound && m_soundPath == path) return true;
    m_channel = nullptr;
    if (m_sound) {
        m_sound->release();
        m_sound = nullptr;
    }
    if (engine->m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &m_sound) != FMOD_OK || !m_sound) {
        m_sound = nullptr;
        m_soundPath.clear();
        log::warn("Audiotrack: FMOD could not open {}", path);
        return false;
    }
    float frequency = 44100.f;
    m_sound->getDefaults(&frequency, nullptr);
    m_frequency = frequency > 0.f ? frequency : 44100.f;
    m_soundPath = path;
    return true;
}

bool Playback::begin(double seconds, double delay) {
    // Everything that follows may take a while on the first start of a track (loading and decoding
    // the wav). The clock already runs, so the time spent is skipped in the wav instead of being
    // heard as clicks that come late.
    auto const started = std::chrono::steady_clock::now();

    auto const* track = current();
    if (!track || !load(track->wavPath)) return false;
    auto* engine = FMODAudioEngine::sharedEngine();
    bool const toMic = track->toMic && Mixer::get().running();
    if (toMic) {
        std::string error;
        Mixer::get().prepare(track->wavPath, error); // decode now, before the late time is measured
    }

    // Keep the preview flag across the stop below.
    bool const preview = m_preview;
    stopCurrent();
    m_preview = preview;

    double const late = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    double const remaining = delay - late;
    if (remaining > 0.0) {
        m_delay = remaining; // tick() starts the sound once the delay is over
        return true;
    }
    // The wav starts at `seconds`, or at its beginning after a delay, plus whatever time was lost.
    double const position = (delay > 0.0 ? 0.0 : seconds) + std::max(0.0, late - delay);

    FMOD::Channel* channel = nullptr;
    if (engine->m_system->playSound(m_sound, nullptr, true, &channel) != FMOD_OK || !channel) return false;
    channel->setPosition(static_cast<unsigned int>(std::max(0.0, position) * m_frequency), FMOD_TIMEUNIT_PCM);
    // With "in game" off the channel still runs (it is the clock) but is silent.
    channel->setVolume(track->local ? track->volume : 0.f);
    channel->setPitch(preview ? 1.f : CCDirector::get()->getScheduler()->getTimeScale());
    channel->setPaused(false);
    m_channel = channel;

    // The same wave goes into the microphone stream, from the same position.
    if (toMic) m_mixVoice = Mixer::get().play(track->wavPath, position, track->volume, track->audioEndSeconds);
    return true;
}

// Loads the wav ahead of time (FMOD and the mixer's decoded copy) so the first attempt does not stall.
void Playback::preload() {
    auto& api = XdBotApi::get();
    if (!api.attach()) return;
    refresh();
    if (!m_track || !m_track->enabled || m_track->wavPath.empty()) return;
    load(m_track->wavPath);
    if (m_track->toMic && Mixer::get().running()) {
        std::string path = m_track->wavPath;
        std::thread([path] {
            std::string error;
            Mixer::get().prepare(path, error);
        }).detach();
    }
}

void Playback::stopCurrent() {
    if (m_channel) {
        m_channel->stop();
        m_channel = nullptr;
    }
    Mixer::get().stopVoice(m_mixVoice);
    m_mixVoice = 0;
    m_delay = 0.0;
    m_tail = false;
    m_preview = false;
}

void Playback::stop() {
    stopCurrent();
    for (auto& ring : m_rings) ring.channel->stop();
    m_rings.clear();
    Mixer::get().stopAll();
}

bool Playback::preview(Track const& track, double seconds) {
    stop();
    m_previewTrack = track;
    m_preview = true;
    if (!begin(seconds, 0.0)) {
        m_preview = false;
        return false;
    }
    return true;
}

void Playback::updatePreview(Track const& track) {
    if (m_preview) m_previewTrack = track;
}

double Playback::position() const {
    if (!m_channel) return -1.0;
    unsigned int pcm = 0;
    if (m_channel->getPosition(&pcm, FMOD_TIMEUNIT_PCM) != FMOD_OK) return -1.0;
    return static_cast<double>(pcm) / m_frequency;
}

void Playback::onReset() {
    auto& api = XdBotApi::get();
    if (!api.attach()) return;
    if (m_preview) return; // the editor is previewing, leave it alone
    refresh();
    if (!m_track || !m_track->enabled || m_track->wavPath.empty()) return;

    if (api.state() != BotState::Playing) {
        stop();
        return;
    }
    // The previous attempt ended (death) and its click wave is still running
    // out until the audio end marker: let it ring out on its own channel and
    // start this attempt's wave from the beginning. A wave that is still
    // inside the macro (practice restart) is simply cut by begin().
    if (m_channel && m_tail) {
        m_rings.push_back({m_channel, m_track->audioEndSeconds});
        m_channel = nullptr;
        m_mixVoice = 0; // its microphone voice keeps playing out on its own
        m_tail = false;
    }

    auto const info = api.macro();
    if (!info.valid) return;
    double const start = audioTimeForFrame(*m_track, info, api.currentFrame());
    m_macroEnd = macroEndAudioTime(*m_track, info);
    if (start < 0.0) begin(0.0, -start); // the first click is after the start of the audio
    else begin(start, 0.0);
}

void Playback::applyLive(bool paused) {
    auto const* track = current();
    if (!m_channel || !track) return;
    m_channel->setPaused(paused);
    m_channel->setVolume(track->local ? track->volume : 0.f);
    m_channel->setPitch(m_preview ? 1.f : CCDirector::get()->getScheduler()->getTimeScale());
}

void Playback::tickRings() {
    if (m_rings.empty()) return;
    auto* play = PlayLayer::get();
    bool const paused = play && play->m_isPaused;
    float const pitch = CCDirector::get()->getScheduler()->getTimeScale();
    float const volume = (m_track && m_track->local) ? m_track->volume : 0.f;
    for (auto it = m_rings.begin(); it != m_rings.end();) {
        bool playing = false;
        if (it->channel->isPlaying(&playing) != FMOD_OK || !playing) {
            it = m_rings.erase(it);
            continue;
        }
        it->channel->setPaused(paused);
        it->channel->setVolume(volume);
        it->channel->setPitch(pitch);
        unsigned int pcm = 0;
        if (it->end > 0.0 && it->channel->getPosition(&pcm, FMOD_TIMEUNIT_PCM) == FMOD_OK &&
            static_cast<double>(pcm) / m_frequency >= it->end) {
            it->channel->stop();
            it = m_rings.erase(it);
            continue;
        }
        ++it;
    }
}

void Playback::tick(float dt) {
    auto& mixer = Mixer::get();
    if (mixer.running()) {
        auto* play = PlayLayer::get();
        bool const paused = !m_preview && play && play->m_isPaused;
        mixer.setTransport(paused, m_preview ? 1.f : CCDirector::get()->getScheduler()->getTimeScale());
    }
    tickRings();
    if (!m_channel && m_delay <= 0.0) return;
    auto const* track = current();
    if (!track) return;
    auto* play = PlayLayer::get();
    bool const paused = !m_preview && play && play->m_isPaused;

    if (!m_channel) {
        if (paused) return;
        m_delay -= dt * CCDirector::get()->getScheduler()->getTimeScale();
        if (m_delay <= 0.0) begin(-m_delay, 0.0); // the delay ended somewhere inside this tick
        return;
    }

    bool playing = false;
    if (m_channel->isPlaying(&playing) != FMOD_OK || !playing) {
        // Natural end of the file.
        m_channel = nullptr;
        m_tail = false;
        return;
    }
    applyLive(paused);

    double const position = this->position();
    if (position < 0.0) return;
    if (!m_preview) m_tail = position >= m_macroEnd;
    // The audio end marker is strict: the wave is cut exactly there.
    if (track->audioEndSeconds > 0.0 && position >= track->audioEndSeconds) stopCurrent();
}

} // namespace at
