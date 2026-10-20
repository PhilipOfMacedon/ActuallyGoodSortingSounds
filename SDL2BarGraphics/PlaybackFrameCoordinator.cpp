#include "PlaybackFrameCoordinator.h"

#include <string>

PlaybackFrameCoordinator::PlaybackFrameCoordinator(LatestAudioStateMailbox& mailbox,
	AudioPresentationBridge& bridge, SdlAudioOutput& output)
	: mailbox_(mailbox), bridge_(bridge), output_(output)
{
}

bool PlaybackFrameCoordinator::audioClockAvailable() const
{
	return output_.available() && bridge_.playheadReady(output_.timelineAcknowledgement());
}

std::string PlaybackFrameCoordinator::updateAudioPresentation(AppController& app,
	const sortinglogic::RenderSnapshot& snapshot)
{
	bridge_.update(app.simulation(), snapshot, app.effectiveAudio(), mailbox_,
		app.effectiveReconstructedAudio(), app.preferences().simulation.playbackTimingMode,
		output_.available());
	const AudioTimelineAcknowledgement acknowledgement = output_.timelineAcknowledgement();
	const bool playheadReady = bridge_.playheadReady(acknowledgement);
	app.setAudioPreparationPending(bridge_.timelinePreparing() ||
		(output_.available() && bridge_.timelineReady() && !playheadReady));
	bridge_.collectRetiredTimelines(acknowledgement, output_.available());
	SoundEngine::AudioPlayheadFeedback feedback;
	if (output_.available() && playheadReady && output_.readPlayheadFeedback(feedback))
		app.applyAudioPlayhead(feedback, bridge_.playbackRevision());
	return status();
}

std::string PlaybackFrameCoordinator::status() const
{
	std::string result = output_.status();
	if (output_.outputOverloadCount() > 0)
		result += " | output safety clamp: " + std::to_string(output_.outputOverloadCount()) + " samples";
	if (bridge_.timelinePreparing()) result = "Preparing audio timeline...";
	else if (!bridge_.timelineError().empty())
		result = "Timeline error: " + bridge_.timelineError() + " | sorting remains available";
	return result;
}
