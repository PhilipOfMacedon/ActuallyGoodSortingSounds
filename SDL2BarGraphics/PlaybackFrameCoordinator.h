#pragma once

#include <string>

#include "AppController.h"
#include "AudioPresentationBridge.h"
#include "SdlAudioOutput.h"

// Coordinates the app-thread handoff between simulation snapshots, prepared
// timelines, callback acknowledgements, and playhead feedback for one frame.
class PlaybackFrameCoordinator
{
public:
	PlaybackFrameCoordinator(LatestAudioStateMailbox& mailbox, AudioPresentationBridge& bridge,
		SdlAudioOutput& output);

	bool audioClockAvailable() const;
	std::string updateAudioPresentation(AppController& app);

private:
	LatestAudioStateMailbox& mailbox_;
	AudioPresentationBridge& bridge_;
	SdlAudioOutput& output_;
	std::string status() const;
};
