#pragma once

#include <cstddef>
#include <cstdint>

enum class PlaybackCoordinatorState
{
	NoTrace,
	BuildingTrace,
	PreparingAudio,
	Queued,
	Ready,
	Playing,
	Paused,
	Completed
};

// Owns app-thread playback intent and the fallback reference clock. It does
// not own the simulation, audio timeline worker, or SDL callback resources.
class PlaybackCoordinator
{
public:
	PlaybackCoordinator();

	void traceBuildRequested(bool playWhenReady);
	void traceBuildFailed();
	void traceBuildCompleted(bool hasTrace);

	void queuePlayback();
	void cancelQueuedPlayback();
	bool playbackQueued() const { return playbackQueued_; }
	bool consumeQueuedPlayback(bool traceBuilding, bool hasTrace);

	void setAudioPreparationPending(bool pending);
	void markAudioPreparationPending(bool pending) { audioPreparationPending_ = pending; }
	bool audioPreparationPending() const { return audioPreparationPending_; }
	bool audioPreparationBlocksStart() const { return audioPreparationGateEnabled_ && audioPreparationPending_; }

	bool acceptsAudioPlayhead(bool simulationPlaying, std::uint64_t feedbackGeneration,
		std::uint64_t feedbackTransportRevision, std::uint64_t currentGeneration,
		std::uint64_t currentTransportRevision, std::uint64_t durationFrames) const;

	bool observeAudioClockAvailability(bool available);
	void setReferenceElapsedSeconds(double elapsedSeconds) { referenceElapsedSeconds_ = elapsedSeconds; }
	void advanceReferenceClock(double deltaSeconds);
	double referenceElapsedSeconds() const { return referenceElapsedSeconds_; }

	PlaybackCoordinatorState state(bool traceBuilding, bool hasTrace, bool simulationPlaying,
		std::size_t cursor, std::size_t eventCount) const;

private:
	bool playbackQueued_;
	bool audioPreparationGateEnabled_;
	bool audioPreparationPending_;
	bool previousAudioClockAvailable_;
	double referenceElapsedSeconds_;
};
