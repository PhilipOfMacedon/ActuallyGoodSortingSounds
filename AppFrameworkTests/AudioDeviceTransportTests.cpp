// Exercises the real SDL callback, bridge, controller, and worker together.
// SDL's dummy driver keeps this deterministic and avoids using the user's device.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "../SDL2BarGraphics/AppController.h"
#include "../SDL2BarGraphics/AudioPresentationBridge.h"
#include "../SDL2BarGraphics/SdlAudioOutput.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class MemorySettings : public ISettingsPersistence {
public:
    UserPreferences value;
    mutable unsigned writes = 0;
    UserPreferences load() const override { return value; }
    bool save(const UserPreferences& v, std::string* error = nullptr) const override {
        (void)v; ++writes; if (error) error->clear(); return true;
    }
};
class Session {
public:
    MemorySettings settings;
    AppController app;
    LatestAudioStateMailbox mailbox;
    AudioPresentationBridge bridge;
    SdlAudioOutput output;
    std::chrono::steady_clock::time_point last;
    unsigned preparations = 0;
    bool wasPreparing = false;
    explicit Session(const UserPreferences& preferences) : settings(), app(seed(preferences)), mailbox(), bridge(), output(mailbox), last(std::chrono::steady_clock::now()) {
        require(output.available(), "SDL dummy audio device did not open");
        std::cout << "Audio backend: " << SDL_GetCurrentAudioDriver() << "; " << output.status() << '\n';
    }
    ISettingsPersistence& seed(const UserPreferences& preferences) { settings.value = preferences; return settings; }
    void frame() {
        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now-last).count(); last=now;
        app.update(elapsed, output.available() && bridge.playheadReady(output.timelineAcknowledgement()));
        bridge.update(app.simulation(), app.snapshot(), app.effectiveAudio(), mailbox,
            app.effectiveReconstructedAudio(), app.preferences().simulation.playbackTimingMode,
            output.available());
        const AudioTimelineAcknowledgement acknowledgement=output.timelineAcknowledgement();
        const bool ready=bridge.playheadReady(acknowledgement);
        app.setAudioPreparationPending(bridge.timelinePreparing() || (output.available() && bridge.timelineReady() && !ready));
        bridge.collectRetiredTimelines(acknowledgement, output.available());
        SoundEngine::AudioPlayheadFeedback feedback;
        if (output.available() && ready && output.readPlayheadFeedback(feedback))
            app.applyAudioPlayhead(feedback, bridge.playbackRevision());
        const bool preparing=bridge.timelinePreparing();
        if (preparing && !wasPreparing) ++preparations;
        wasPreparing=preparing;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void pump(unsigned milliseconds) {
        const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
        do { frame(); } while(std::chrono::steady_clock::now()<until);
    }
    void settle() {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        auto stableSince=std::chrono::steady_clock::now();
        do {
            frame();
            if (!bridge.playheadReady(output.timelineAcknowledgement())) stableSince=std::chrono::steady_clock::now();
            else if (std::chrono::steady_clock::now()-stableSince>std::chrono::milliseconds(60)) return;
        } while(std::chrono::steady_clock::now()<deadline);
        std::cerr << "preparations=" << preparations << " transport_revision=" << bridge.transportRevision() << '\n';
        throw std::runtime_error("audio preparation never settled after inspecting an event");
    }
    void action(AppActionKind kind) { require(app.dispatch(AppAction(kind)).accepted,"transport action was rejected"); frame(); }
    void seek(float fraction) { require(app.dispatch(AppAction(AppActionKind::SeekFraction,fraction)).accepted,"seek action was rejected"); frame(); settle(); }
    void resume() {
        const std::size_t before=app.snapshot().cursor;
        action(AppActionKind::TogglePlayback); pump(70);
        require(app.snapshot().playing,"Play was cancelled by stale paused feedback");
        require(app.snapshot().cursor>before,"audio playhead did not advance after resume");
    }
};
void scenario(SoundEngine::PlaybackTimingMode mode, SoundEngine::ReconstructedIdlePolicy policy, int items=80, unsigned trials=15) {
    UserPreferences preferences;
    preferences.simulation.itemCount=items;
    preferences.simulation.durationSeconds=120;
    preferences.simulation.playbackTimingMode=mode;
    preferences.reconstructedAudio.idlePolicy=policy;
    Session session(preferences);
    session.action(AppActionKind::Run);
    const auto traceDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
    while(!session.app.snapshot().hasTrace && std::chrono::steady_clock::now()<traceDeadline) session.frame();
    if(!session.app.snapshot().hasTrace) std::cerr << "Trace status: " << session.app.snapshot().status << '\n';
    require(session.app.snapshot().hasTrace,"trace construction timed out"); session.settle();
    session.seek(.25f);
    for(unsigned trial=0;trial<trials;++trial) {
        session.resume();
        session.action(AppActionKind::TogglePlayback); session.settle();
        const auto cursor=session.app.snapshot().cursor;
        const auto revision=session.bridge.transportRevision();
        const auto preparations=session.preparations;
        session.pump(35);
        require(!session.app.snapshot().playing && session.app.snapshot().cursor==cursor,"paused cursor drifted");
        require(session.bridge.transportRevision()==revision && session.preparations==preparations,"pause restarted audio preparation");
        session.action(trial%2 ? AppActionKind::PreviousEvent : AppActionKind::NextEvent);
        session.settle();
        if(trial%5==4) session.seek(.25f);
    }
    session.resume();
    session.action(AppActionKind::Reset); session.settle();
    require(session.app.snapshot().cursor==0 && !session.app.snapshot().playing,"Reset failed after repeated pause/step/resume");
    require(session.settings.writes==0,"transport smoke scenarios wrote preferences");
    require(session.output.outputOverloadCount()==0,"default-profile transport generated an output overload");
    std::cout << "PASS timing=" << static_cast<int>(mode) << " idle=" << static_cast<int>(policy)
        << " items=" << items << " pause/resume=" << trials << " stepped=" << trials << " preparations=" << session.preparations << '\n';
}
}
int main(int argc, char** argv) {
    const bool physical=argc==2 && std::string(argv[1])=="--physical-device";
    if (argc>1 && !physical) { std::cerr << "Use --physical-device or no arguments.\n"; return 1; }
    std::cout << std::unitbuf;
    SDL_SetMainReady();
    if (!physical) SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    try {
        if (physical) {
            scenario(SoundEngine::PlaybackTimingMode::FixedDuration,SoundEngine::ReconstructedIdlePolicy::HoldSelection,80,5);
            scenario(SoundEngine::PlaybackTimingMode::FixedDuration,SoundEngine::ReconstructedIdlePolicy::ReleaseSelection,80,5);
            scenario(SoundEngine::PlaybackTimingMode::ReferenceCadence,SoundEngine::ReconstructedIdlePolicy::HoldSelection,80,5);
            scenario(SoundEngine::PlaybackTimingMode::ReferenceCadence,SoundEngine::ReconstructedIdlePolicy::ReleaseSelection,80,5);
            std::cout << "Physical-device audio transport integration tests passed.\n"; return 0;
        }
        scenario(SoundEngine::PlaybackTimingMode::FixedDuration,SoundEngine::ReconstructedIdlePolicy::HoldSelection);
        scenario(SoundEngine::PlaybackTimingMode::FixedDuration,SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
        scenario(SoundEngine::PlaybackTimingMode::ReferenceCadence,SoundEngine::ReconstructedIdlePolicy::HoldSelection);
        scenario(SoundEngine::PlaybackTimingMode::ReferenceCadence,SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
        scenario(SoundEngine::PlaybackTimingMode::FixedDuration,SoundEngine::ReconstructedIdlePolicy::HoldSelection,800,5);
        scenario(SoundEngine::PlaybackTimingMode::ReferenceCadence,SoundEngine::ReconstructedIdlePolicy::ReleaseSelection,800,5);
        std::cout << "Audio device transport integration tests passed.\n"; return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
