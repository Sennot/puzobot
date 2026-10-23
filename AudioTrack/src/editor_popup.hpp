#pragma once

#include "playback.hpp"
#include "track.hpp"
#include "wav.hpp"
#include "xdbot_api.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>

#include <optional>
#include <vector>

namespace at {

// The Audiotrack editor: waveform of the wav with three markers.
//   Anchor    - the click that lines up with the macro's first input
//   Macro end - where the macro ends (death / last input), shown on the wave
//   Audio end - where the click wave stops after the macro is over
// The macro's own button presses are drawn as ticks above the wave.
class AudiotrackPopup : public geode::Popup {
public:
    // Opens the editor, or explains what is missing (no macro loaded, ...).
    static void open();

protected:
    enum class Marker { Anchor, MacroEnd, AudioEnd };

    bool init();
    void onClose(cocos2d::CCObject*) override;

    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override;

    void onLoad(cocos2d::CCObject*);
    void onAutoClick(cocos2d::CCObject*);
    void onSelect(cocos2d::CCObject*);
    void onNudge(cocos2d::CCObject*);
    void onZoom(cocos2d::CCObject*);
    void onPan(cocos2d::CCObject*);
    void onPreview(cocos2d::CCObject*);
    void onStop(cocos2d::CCObject*);
    void onEnabled(cocos2d::CCObject*);
    void onToMic(cocos2d::CCObject*);
    void onLocal(cocos2d::CCObject*);
    void onCalibrate(cocos2d::CCObject*);
    void onInstall(cocos2d::CCObject*);
    void onVolume(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void tick(float dt);

    bool openWav(std::string const& path, bool detectClick);
    void resetView();
    double markerTime(Marker marker) const;
    void setMarker(Marker marker, double seconds);
    double timeAt(float x) const;
    float xAt(double seconds) const;
    bool inWave(cocos2d::CCPoint const& p) const;
    void refreshAll();
    void redraw();
    void setStatus(std::string const& text, bool error = false);

    MacroInfo m_macro;
    Track m_track;
    std::optional<Wav> m_wav;
    std::vector<std::uint32_t> m_presses;

    double m_viewStart = 0.0;
    double m_viewLen = 6.0;
    Marker m_selected = Marker::Anchor;
    bool m_dragging = false;

    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCDrawNode* m_head = nullptr;
    cocos2d::CCLabelBMFont* m_file = nullptr;
    cocos2d::CCLabelBMFont* m_info = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_viewLabel = nullptr;
    cocos2d::CCLabelBMFont* m_valueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_volumeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_enabledLabel = nullptr;
    cocos2d::CCLabelBMFont* m_toMicLabel = nullptr;
    cocos2d::CCLabelBMFont* m_localLabel = nullptr;
    cocos2d::CCLabelBMFont* m_mixerLabel = nullptr;
    bool m_driverPresent = false; // "Audiotrack Out/Mic" exist, checked when the editor opens
    geode::NineSlice* m_markerButtons[3] = {nullptr, nullptr, nullptr};

    geode::async::TaskHolder<geode::utils::file::PickResult> m_picker;
};

} // namespace at
