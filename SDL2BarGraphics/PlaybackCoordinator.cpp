#include "PlaybackCoordinator.h"

namespace
{
	double nonnegative(double value)
	{
		return value > 0.0 ? value : 0.0;
	}
}

PlaybackCoordinator::PlaybackCoordinator()
	: playbackQueued_(false), audioPreparationGateEnabled_(false), audioPreparationPending_(false),
	  previousAudioClockAvailable_(false), referenceElapsedSeconds_(0.0)
{
}

void PlaybackCoordinator::traceBuildRequested(bool playWhenReady)
{
	playbackQueued_ = playWhenReady;
}

void PlaybackCoordinator::traceBuildFailed()
{
	playbackQueued_ = false;
}

void PlaybackCoordinator::traceBuildCompleted(bool hasTrace)
{
	referenceElapsedSeconds_ = 0.0;
	audioPreparationPending_ = hasTrace;
	if (!hasTrace) playbackQueued_ = false;
}

void PlaybackCoordinator::queuePlayback()
{
	playbackQueued_ = true;
}

void PlaybackCoordinator::cancelQueuedPlayback()
{
	playbackQueued_ = false;
}

bool PlaybackCoordinator::consumeQueuedPlayback(bool traceBuilding, bool hasTrace)
{
	if (!playbackQueued_ || traceBuilding) return false;
	if (!hasTrace)
	{
		playbackQueued_ = false;
		return false;
	}
	if (audioPreparationBlocksStart()) return false;
	playbackQueued_ = false;
	return true;
}

void PlaybackCoordinator::setAudioPreparationPending(bool pending)
{
	audioPreparationGateEnabled_ = true;
	audioPreparationPending_ = pending;
}

bool PlaybackCoordinator::acceptsAudioPlayhead(bool simulationPlaying, std::uint64_t feedbackGeneration,
	std::uint64_t feedbackTransportRevision, std::uint64_t durationFrames,
	const AudioPlaybackRevision& currentRevision) const
{
	return simulationPlaying && feedbackGeneration != 0 && feedbackGeneration == currentRevision.timelineGeneration &&
		feedbackTransportRevision == currentRevision.transportRevision && durationFrames != 0;
}

bool PlaybackCoordinator::observeAudioClockAvailability(bool available)
{
	const bool lost = previousAudioClockAvailable_ && !available;
	previousAudioClockAvailable_ = available;
	return lost;
}

void PlaybackCoordinator::advanceReferenceClock(double deltaSeconds)
{
	referenceElapsedSeconds_ += nonnegative(deltaSeconds);
}

PlaybackCoordinatorState PlaybackCoordinator::state(bool traceBuilding, bool hasTrace, bool simulationPlaying,
	std::size_t cursor, std::size_t eventCount) const
{
	if (playbackQueued_) return PlaybackCoordinatorState::Queued;
	if (traceBuilding) return PlaybackCoordinatorState::BuildingTrace;
	if (!hasTrace) return PlaybackCoordinatorState::NoTrace;
	if (simulationPlaying) return PlaybackCoordinatorState::Playing;
	if (audioPreparationBlocksStart()) return PlaybackCoordinatorState::PreparingAudio;
	if (cursor >= eventCount) return PlaybackCoordinatorState::Completed;
	return cursor == 0 ? PlaybackCoordinatorState::Ready : PlaybackCoordinatorState::Paused;
}
