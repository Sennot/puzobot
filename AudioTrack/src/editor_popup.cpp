#include "editor_popup.hpp"

#include "installer.hpp"
#include "mixer.hpp"

#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace at {
namespace {

// Waveform area inside the popup's main layer.
constexpr float kWaveX = 20.f;
constexpr float kWaveY = 142.f;
constexpr float kWaveW = 420.f;
constexpr float kWaveH = 100.f;
constexpr float kStripH = 12.f; // macro press ticks above the wave

// xdBot palette (from its mint "xdBot" title gradient).
constexpr ccColor3B kMint{27, 222, 148};
constexpr ccColor3B kLight{206, 252, 144};
constexpr ccColor3B kAmber{255, 190, 80};
constexpr ccColor3B kRose{255, 110, 120};
constexpr ccColor3B kText{220, 230, 245};
constexpr ccColor3B kMuted{148, 164, 194};

ccColor4F rgba(ccColor3B c, int a = 255) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, a / 255.f};
}

CCLabelBMFont* label(CCNode* parent, std::string const& text, float x, float y, float scale,
                     ccColor3B color = kText, float width = 400.f, bool left = false) {
    auto l = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    if (left) l->setAnchorPoint({0.f, .5f});
    l->setPosition({x, y});
    l->setColor(color);
    l->limitLabelWidth(width, scale, 0.12f);
    parent->addChild(l);
    return l;
}

CCMenuItemSpriteExtra* button(CCMenu* menu, CCObject* target, SEL_MenuHandler cb, char const* text, float x, float y,
                              float width, ccColor3B color, int tag = 0, NineSlice** bgOut = nullptr) {
    auto bg = NineSlice::create("GJ_square02.png");
    bg->setContentSize({width, 24.f});
    bg->setColor(color);
    label(bg, text, width / 2, 12.f, .32f, {255, 255, 255}, width - 10.f);
    auto item = CCMenuItemSpriteExtra::create(bg, target, cb);
    item->setPosition({x, y});
    item->setTag(tag);
    menu->addChild(item);
    if (bgOut) *bgOut = bg;
    return item;
}

void fillRect(CCDrawNode* node, float x0, float y0, float x1, float y1, ccColor4F color) {
    CCPoint v[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    node->drawPolygon(v, 4, color, 0.f, color);
}

float niceStep(double viewLen) {
    static constexpr double steps[] = {0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0};
    for (auto s : steps)
        if (viewLen / s <= 12.0) return static_cast<float>(s);
    return 120.f;
}

} // namespace

void AudiotrackPopup::open() {
    auto& api = XdBotApi::get();
    if (!api.attach() || !api.globalReady() || !api.macro().valid) {
        FLAlertLayer::create("Audiotrack",
                             "Load a macro in xdBot first (Macro > Load), then open Audiotrack again.", "OK")
            ->show();
        return;
    }
    auto* popup = new AudiotrackPopup();
    if (popup->init()) {
        popup->autorelease();
        popup->show();
        return;
    }
    delete popup;
}

bool AudiotrackPopup::init() {
    if (!Popup::init(460.f, 338.f, "GJ_square02.png")) return false;
    m_bgSprite->setColor({18, 22, 28});
    setTitle("AUDIOTRACK", "bigFont.fnt", .6f, 21.f);
    m_title->setColor(kMint);

    auto& api = XdBotApi::get();
    auto& playback = Playback::get();
    playback.refresh();
    m_macro = api.macro();
    m_presses = api.pressFrames();
    m_track = playback.track().value_or(Track{});

    m_info = label(m_mainLayer, "", 230.f, 298.f, .27f, kMuted, 420.f);
    m_statusLabel = label(m_mainLayer, "", 230.f, 284.f, .27f, kMint, 420.f);
    m_file = label(m_mainLayer, "", 24.f, 266.f, .34f, kText, 240.f, true);

    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onAutoClick), "Auto click", 318.f, 266.f, 84.f,
           {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onLoad), "Load WAV", 406.f, 266.f, 84.f,
           {29, 135, 123});

    m_draw = CCDrawNode::create();
    m_mainLayer->addChild(m_draw, 1);
    m_head = CCDrawNode::create();
    m_mainLayer->addChild(m_head, 2);

    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onZoom), "-", 40.f, 124.f, 32.f, {49, 79, 103}, 0);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onZoom), "+", 78.f, 124.f, 32.f, {49, 79, 103}, 1);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onPan), "<", 120.f, 124.f, 32.f, {49, 79, 103}, -1);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onPan), ">", 158.f, 124.f, 32.f, {49, 79, 103}, 1);
    m_viewLabel = label(m_mainLayer, "", 440.f, 124.f, .27f, kMuted, 240.f);
    m_viewLabel->setAnchorPoint({1.f, .5f});

    char const* markerNames[3] = {"Anchor", "Macro end", "Audio end"};
    float const markerX[3] = {70.f, 175.f, 280.f};
    for (int i = 0; i < 3; ++i)
        button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onSelect), markerNames[i], markerX[i], 98.f, 98.f,
               {49, 79, 103}, i, &m_markerButtons[i]);
    m_valueLabel = label(m_mainLayer, "", 440.f, 98.f, .3f, kText, 150.f);
    m_valueLabel->setAnchorPoint({1.f, .5f});

    struct Nudge {
        char const* text;
        int ms;
    };
    Nudge const nudges[6] = {{"-100ms", -100}, {"-10ms", -10}, {"-1ms", -1}, {"+1ms", 1}, {"+10ms", 10}, {"+100ms", 100}};
    for (int i = 0; i < 6; ++i)
        button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onNudge), nudges[i].text, 50.f + i * 72.f, 72.f,
               62.f, {40, 50, 74}, nudges[i].ms);

    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onPreview), "Preview", 50.f, 46.f, 76.f,
           {29, 135, 123});
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onStop), "Stop", 132.f, 46.f, 60.f, {40, 50, 74});
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onEnabled), "", 214.f, 46.f, 70.f, {49, 79, 103});
    m_enabledLabel = label(m_mainLayer, "", 214.f, 46.f, .3f, {255, 255, 255}, 62.f);
    m_enabledLabel->setZOrder(5);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onVolume), "-", 280.f, 46.f, 32.f, {40, 50, 74}, -1);
    m_volumeLabel = label(m_mainLayer, "", 318.f, 46.f, .3f, kText, 44.f);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onVolume), "+", 356.f, 46.f, 32.f, {40, 50, 74}, 1);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onSave), "Save", 420.f, 46.f, 70.f, {29, 135, 123});

    // Microphone cable row.
    m_driverPresent = Mixer::driverInstalled();
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onToMic), "", 70.f, 20.f, 108.f, {49, 79, 103});
    m_toMicLabel = label(m_mainLayer, "", 70.f, 20.f, .3f, kMuted, 100.f);
    m_toMicLabel->setZOrder(5);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onLocal), "", 188.f, 20.f, 108.f, {49, 79, 103});
    m_localLabel = label(m_mainLayer, "", 188.f, 20.f, .3f, kMuted, 100.f);
    m_localLabel->setZOrder(5);
    button(m_buttonMenu, this, menu_selector(AudiotrackPopup::onInstall), "Install driver", 306.f, 20.f, 100.f,
           {40, 50, 74});
    m_mixerLabel = label(m_mainLayer, "", 446.f, 20.f, .26f, kMuted, 84.f);
    m_mixerLabel->setAnchorPoint({1.f, .5f});

    if (!m_track.wavPath.empty()) {
        if (!openWav(m_track.wavPath, false)) m_track.wavPath.clear();
    }
    if (!m_wav) setStatus("Load a .wav with your click sounds");
    refreshAll();
    schedule(schedule_selector(AudiotrackPopup::tick));
    return true;
}

void AudiotrackPopup::onClose(CCObject* sender) {
    if (Playback::get().previewing()) Playback::get().stop();
    Popup::onClose(sender);
}

// ---- model -------------------------------------------------------------------------------------

double AudiotrackPopup::markerTime(Marker marker) const {
    switch (marker) {
        case Marker::Anchor: return m_track.anchorSeconds;
        case Marker::MacroEnd: return macroEndAudioTime(m_track, m_macro);
        case Marker::AudioEnd:
            if (m_track.audioEndSeconds > 0.0) return m_track.audioEndSeconds;
            return m_wav ? m_wav->duration() : 0.0;
    }
    return 0.0;
}

void AudiotrackPopup::setMarker(Marker marker, double seconds) {
    double const duration = m_wav ? m_wav->duration() : 0.0;
    switch (marker) {
        case Marker::Anchor:
            m_track.anchorSeconds = std::clamp(seconds, 0.0, std::max(duration, 0.0));
            break;
        case Marker::MacroEnd: {
            double const frame = m_macro.firstFrame + std::round((seconds - m_track.anchorSeconds) * m_macro.framerate);
            m_track.macroEndFrame = static_cast<std::int64_t>(std::max<double>(frame, m_macro.firstFrame));
            break;
        }
        case Marker::AudioEnd:
            m_track.audioEndSeconds = std::clamp(seconds, 0.0, std::max(duration, 0.0));
            break;
    }
}

bool AudiotrackPopup::openWav(std::string const& path, bool detectClick) {
    std::string error;
    auto wav = loadWav(pathFromUtf8(path), error);
    if (!wav) {
        setStatus("Cannot read the wav: " + error, true);
        return false;
    }
    m_wav = std::move(wav);
    m_track.wavPath = path;
    if (detectClick) {
        m_track.anchorSeconds = m_wav->firstClick();
        m_track.audioEndSeconds = 0.0;
        m_track.macroEndFrame = -1;
    }
    resetView();
    setStatus(detectClick ? "First click detected - fine-tune the Anchor marker if needed"
                          : "Drag the markers on the wave; Save when done");
    return true;
}

void AudiotrackPopup::resetView() {
    double const duration = m_wav ? m_wav->duration() : 6.0;
    m_viewStart = std::max(0.0, m_track.anchorSeconds - 0.5);
    m_viewLen = std::clamp(duration - m_viewStart, 0.1, 6.0);
}

double AudiotrackPopup::timeAt(float x) const { return m_viewStart + (x - kWaveX) / kWaveW * m_viewLen; }
float AudiotrackPopup::xAt(double seconds) const {
    return kWaveX + static_cast<float>((seconds - m_viewStart) / m_viewLen * kWaveW);
}
bool AudiotrackPopup::inWave(CCPoint const& p) const {
    return p.x >= kWaveX && p.x <= kWaveX + kWaveW && p.y >= kWaveY - 4.f && p.y <= kWaveY + kWaveH + kStripH + 2.f;
}

void AudiotrackPopup::setStatus(std::string const& text, bool error) {
    m_statusLabel->setString(text.c_str());
    m_statusLabel->setColor(error ? kRose : kMint);
    m_statusLabel->limitLabelWidth(420.f, .27f, .12f);
}

// ---- drawing -----------------------------------------------------------------------------------

void AudiotrackPopup::refreshAll() {
    m_info->setString(fmt::format("Macro: {} inputs   first input frame {}   last frame {}   {:.0f} fps",
                                  m_macro.inputCount, m_macro.firstFrame, m_macro.lastFrame, m_macro.framerate)
                          .c_str());
    m_info->limitLabelWidth(420.f, .27f, .12f);

    if (m_wav) {
        auto const name = pathToUtf8(pathFromUtf8(m_track.wavPath).filename());
        m_file->setString(fmt::format("{}  ({:.2f} s, {} Hz)", name, m_wav->duration(), m_wav->sampleRate).c_str());
    } else {
        m_file->setString("No wav loaded");
    }
    m_file->limitLabelWidth(240.f, .34f, .12f);

    m_viewLabel->setString(fmt::format("view {:.3f} - {:.3f} s", m_viewStart, m_viewStart + m_viewLen).c_str());

    char const* names[3] = {"Anchor", "Macro end", "Audio end"};
    auto const index = static_cast<int>(m_selected);
    double const time = markerTime(m_selected);
    std::string value = fmt::format("{}: {:.3f} s", names[index], time);
    if (m_selected == Marker::MacroEnd) {
        auto const frame = m_track.macroEndFrame >= 0 ? m_track.macroEndFrame : static_cast<std::int64_t>(m_macro.lastFrame);
        value = fmt::format("frame {}  {:.3f} s", frame, time);
    } else if (m_selected == Marker::AudioEnd && m_track.audioEndSeconds <= 0.0) {
        value += " (file end)";
    }
    m_valueLabel->setString(value.c_str());
    m_valueLabel->limitLabelWidth(150.f, .3f, .12f);

    for (int i = 0; i < 3; ++i)
        if (m_markerButtons[i]) m_markerButtons[i]->setColor(i == index ? ccColor3B{29, 135, 123} : ccColor3B{49, 79, 103});

    m_enabledLabel->setString(m_track.enabled ? "Enabled" : "Disabled");
    m_enabledLabel->setColor(m_track.enabled ? kLight : kRose);
    m_volumeLabel->setString(fmt::format("{:.0f}%", m_track.volume * 100.f).c_str());

    m_toMicLabel->setString(m_track.toMic ? "To mic: ON" : "To mic: OFF");
    m_toMicLabel->setColor(m_track.toMic ? kLight : kMuted);
    m_localLabel->setString(m_track.local ? "In game: ON" : "In game: OFF");
    m_localLabel->setColor(m_track.local ? kLight : kMuted);
    std::string mixer = "driver not installed";
    if (Mixer::get().running()) mixer = "mixer running";
    else if (m_driverPresent) mixer = Mixer::get().status();
    m_mixerLabel->setString(mixer.c_str());
    m_mixerLabel->setColor(Mixer::get().running() ? kMint : kMuted);
    m_mixerLabel->limitLabelWidth(84.f, .26f, .12f);

    Playback::get().updatePreview(m_track);
    redraw();
}

void AudiotrackPopup::redraw() {
    m_draw->clear();
    float const top = kWaveY + kWaveH;
    float const mid = kWaveY + kWaveH / 2.f;
    fillRect(m_draw, kWaveX, kWaveY, kWaveX + kWaveW, top, rgba({28, 35, 44}));
    fillRect(m_draw, kWaveX, top, kWaveX + kWaveW, top + kStripH, rgba({22, 28, 36}));

    // time grid
    float const step = niceStep(m_viewLen);
    for (double t = std::ceil(m_viewStart / step) * step; t < m_viewStart + m_viewLen; t += step)
        m_draw->drawSegment({xAt(t), kWaveY}, {xAt(t), top}, .35f, rgba({60, 72, 88}, 160));
    m_draw->drawSegment({kWaveX, mid}, {kWaveX + kWaveW, mid}, .4f, rgba({60, 72, 88}));

    if (m_wav) {
        std::vector<std::pair<std::int16_t, std::int16_t>> columns;
        m_wav->columns(m_viewStart, m_viewStart + m_viewLen, static_cast<int>(kWaveW), columns);
        float const half = kWaveH / 2.f - 2.f;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            auto const [lo, hi] = columns[c];
            if (lo == 0 && hi == 0) continue;
            float const x = kWaveX + static_cast<float>(c) + .5f;
            m_draw->drawSegment({x, mid + lo / 32768.f * half}, {x, mid + std::max<int>(hi, lo + 1) / 32768.f * half},
                                .5f, rgba(kMint));
        }
    }

    // The macro's own button presses, positioned through the anchor.
    if (m_macro.valid) {
        for (auto frame : m_presses) {
            float const x = xAt(audioTimeForFrame(m_track, m_macro, frame));
            if (x < kWaveX || x > kWaveX + kWaveW) continue;
            m_draw->drawSegment({x, top + 1.f}, {x, top + kStripH - 1.f}, .5f, rgba(kLight));
        }
    }

    ccColor3B const colors[3] = {kMint, kAmber, kRose};
    for (int i = 0; i < 3; ++i) {
        auto const marker = static_cast<Marker>(i);
        float const x = xAt(markerTime(marker));
        if (x < kWaveX - 1.f || x > kWaveX + kWaveW + 1.f) continue;
        bool const selected = marker == m_selected;
        // The default audio end (file end) is drawn faded: it is not set yet.
        int const alpha = (marker == Marker::AudioEnd && m_track.audioEndSeconds <= 0.0) ? 120 : 255;
        m_draw->drawSegment({x, kWaveY - 3.f}, {x, top + kStripH + 1.f}, selected ? 1.2f : .7f, rgba(colors[i], alpha));
        CCPoint tri[3] = {{x - 4.f, top + kStripH + 5.f}, {x + 4.f, top + kStripH + 5.f}, {x, top + kStripH}};
        m_draw->drawPolygon(tri, 3, rgba(colors[i], alpha), 0.f, rgba(colors[i], alpha));
    }
}

void AudiotrackPopup::tick(float) {
    m_head->clear();
    auto& playback = Playback::get();
    if (!playback.previewing()) return;
    double const position = playback.position();
    if (position < 0.0) return;
    float const x = xAt(position);
    if (x < kWaveX || x > kWaveX + kWaveW) return;
    m_head->drawSegment({x, kWaveY}, {x, kWaveY + kWaveH + kStripH}, .8f, rgba({255, 255, 255}));
}

// ---- input -------------------------------------------------------------------------------------

bool AudiotrackPopup::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    auto const p = m_mainLayer->convertToNodeSpace(touch->getLocation());
    if (m_wav && inWave(p)) {
        // Grab the nearest marker if the press is close to one, otherwise move the selected one.
        float best = 10.f;
        for (int i = 0; i < 3; ++i) {
            float const distance = std::abs(xAt(markerTime(static_cast<Marker>(i))) - p.x);
            if (distance < best) {
                best = distance;
                m_selected = static_cast<Marker>(i);
            }
        }
        m_dragging = true;
        setMarker(m_selected, timeAt(p.x));
        refreshAll();
        return true;
    }
    return Popup::ccTouchBegan(touch, event);
}

void AudiotrackPopup::ccTouchMoved(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return Popup::ccTouchMoved(touch, event);
    auto const p = m_mainLayer->convertToNodeSpace(touch->getLocation());
    setMarker(m_selected, timeAt(std::clamp(p.x, kWaveX, kWaveX + kWaveW)));
    refreshAll();
}

void AudiotrackPopup::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return Popup::ccTouchEnded(touch, event);
    m_dragging = false;
}

void AudiotrackPopup::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    if (!m_dragging) return Popup::ccTouchCancelled(touch, event);
    m_dragging = false;
}

void AudiotrackPopup::onLoad(CCObject*) {
    m_picker.spawn(file::pick(file::PickMode::OpenFile, {{}, {{"WAV audio", {"*.wav"}}}}),
                   [this](file::PickResult result) {
                       if (result.isErr()) {
                           setStatus("File picker failed: " + result.unwrapErr(), true);
                           return;
                       }
                       auto path = result.unwrap();
                       if (!path) return;
                       if (openWav(pathToUtf8(*path), true)) refreshAll();
                   });
}

void AudiotrackPopup::onAutoClick(CCObject*) {
    if (!m_wav) return setStatus("Load a wav first", true);
    m_track.anchorSeconds = m_wav->firstClick();
    m_selected = Marker::Anchor;
    resetView();
    setStatus("First click detected - fine-tune the Anchor marker if needed");
    refreshAll();
}

void AudiotrackPopup::onSelect(CCObject* sender) {
    m_selected = static_cast<Marker>(std::clamp(static_cast<CCNode*>(sender)->getTag(), 0, 2));
    refreshAll();
}

void AudiotrackPopup::onNudge(CCObject* sender) {
    if (!m_wav) return;
    int const ms = static_cast<CCNode*>(sender)->getTag();
    if (m_selected == Marker::MacroEnd) {
        // The macro end is a whole frame: at least one frame per nudge.
        double frames = std::round(ms / 1000.0 * m_macro.framerate);
        if (frames == 0.0) frames = ms < 0 ? -1.0 : 1.0;
        double const current = m_track.macroEndFrame >= 0 ? static_cast<double>(m_track.macroEndFrame)
                                                          : static_cast<double>(m_macro.lastFrame);
        m_track.macroEndFrame = static_cast<std::int64_t>(std::max<double>(current + frames, m_macro.firstFrame));
    } else {
        setMarker(m_selected, markerTime(m_selected) + ms / 1000.0);
    }
    refreshAll();
}

void AudiotrackPopup::onZoom(CCObject* sender) {
    double const center = m_viewStart + m_viewLen / 2.0;
    double const limit = std::max(1.0, m_wav ? m_wav->duration() : 6.0) * 1.2;
    m_viewLen = static_cast<CCNode*>(sender)->getTag() == 1 ? std::max(0.02, m_viewLen / 2.0)
                                                           : std::min(limit, m_viewLen * 2.0);
    m_viewStart = std::max(-0.5, center - m_viewLen / 2.0);
    refreshAll();
}

void AudiotrackPopup::onPan(CCObject* sender) {
    double const limit = m_wav ? m_wav->duration() : 6.0;
    m_viewStart = std::clamp(m_viewStart + static_cast<CCNode*>(sender)->getTag() * m_viewLen * .25, -0.5, limit);
    refreshAll();
}

void AudiotrackPopup::onPreview(CCObject*) {
    if (!m_wav) return setStatus("Load a wav first", true);
    if (!Playback::get().preview(m_track, std::max(0.0, m_track.anchorSeconds - 0.5)))
        setStatus("FMOD could not play this wav", true);
    else
        setStatus("Previewing from just before the Anchor");
}

void AudiotrackPopup::onStop(CCObject*) {
    if (Playback::get().previewing()) Playback::get().stop();
}

void AudiotrackPopup::onEnabled(CCObject*) {
    m_track.enabled = !m_track.enabled;
    refreshAll();
}

void AudiotrackPopup::onToMic(CCObject*) {
    if (!m_track.toMic) {
        // Turning it on needs the cable: start the mixer now so a problem shows up here.
        m_driverPresent = Mixer::driverInstalled();
        if (!Mixer::get().running() && !Mixer::get().start()) {
            setStatus(m_driverPresent ? "The mixer could not start: " + Mixer::get().status()
                                      : "Install the Audiotrack driver first (button below)",
                      true);
            return refreshAll();
        }
    }
    m_track.toMic = !m_track.toMic;
    refreshAll();
}

void AudiotrackPopup::onLocal(CCObject*) {
    m_track.local = !m_track.local;
    refreshAll();
}

void AudiotrackPopup::onInstall(CCObject*) {
    createQuickPopup("Audiotrack driver",
                     "Install the <cy>Audiotrack virtual audio driver</c>?\nIt adds the devices <cg>Audiotrack Out</c> "
                     "and <cg>Audiotrack Mic</c>. Windows will ask for administrator permission.",
                     "Cancel", "Install", [](FLAlertLayer*, bool install) {
                         if (!install) return;
                         std::string error;
                         if (!runDriverScript(true, error))
                             Notification::create("Audiotrack driver: " + error, NotificationIcon::Error, 5.f)->show();
                         else
                             Notification::create("Installing the Audiotrack driver...", NotificationIcon::Loading, 3.f)
                                 ->show();
                     });
}

void AudiotrackPopup::onVolume(CCObject* sender) {
    m_track.volume = std::clamp(m_track.volume + static_cast<CCNode*>(sender)->getTag() * 0.1f, 0.f, 1.5f);
    refreshAll();
}

void AudiotrackPopup::onSave(CCObject*) {
    if (m_track.wavPath.empty()) return setStatus("Load a wav first", true);
    Playback::get().assign(m_track);
    Notification::create("Audiotrack saved", NotificationIcon::Success)->show();
    setStatus("Saved for this macro");
}

} // namespace at
