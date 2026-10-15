#include "editor_popup.hpp"
#include "menu_inject.hpp"
#include "mixer.hpp"
#include "playback.hpp"
#include "xdbot_api.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {

// Called by xdBot's gear button on the "Audiotrack" row: (RecordLayer*, CCObject*).
void openAudiotrack(void*, void*) {
    at::AudiotrackPopup::open();
}

} // namespace

class $modify(AtScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        at::Playback::get().tick(dt);
    }
};

class $modify(AtPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        // Run after xdBot's own reset logic so its state (playing, frame) is current.
        (void)self.setHookPriorityBefore("PlayLayer::resetLevel", "zilko.xdbot");
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        at::Playback::get().onReset();
    }

    void onQuit() {
        at::Playback::get().stop();
        PlayLayer::onQuit();
    }
};

$on_mod(Loaded) {
    at::installAudiotrackRow(&openAudiotrack);

    auto* mod = Mod::get();
    at::Mixer::get().setMicName(mod->getSettingValue<std::string>("mic-device"));
    at::Mixer::get().setCableName(mod->getSettingValue<std::string>("cable-device"));
    if (mod->getSettingValue<bool>("start-mixer")) at::Mixer::get().start();
}
