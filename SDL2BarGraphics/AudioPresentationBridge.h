#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../SoundEngine/SoundEngine.h"
#include "../SoundEngine/ReconstructedSound.h"
#include "../SortingLogic/SortingLogic.h"

struct AudioPresentationState
{
	SoundEngine::Parameters parameters;
	SoundEngine::ReconstructedParameters reconstructedParameters;
	SoundEngine::EventAccentSummary eventAccents;
	SoundEngine::AudioComparison comparison;
	SoundEngine::BurstComparison burstComparison;
	SoundEngine::AudioTimelineGainSettings timelineGains;
	const SoundEngine::PreparedAudioTimeline* timeline;
	const SoundEngine::PreparedAudioSeekState* seekState;
	SoundEngine::AudioTransportState transport;
	std::uint64_t timelineGeneration;
	std::uint64_t seekRevision;
	bool traceLoaded;
	bool hasComparison;
	bool transportPlaying;
	bool releaseSelection;
	std::uint64_t revision;

	AudioPresentationState() : parameters(), reconstructedParameters(), eventAccents(), comparison(), burstComparison(), timelineGains(), timeline(nullptr), seekState(nullptr),
		transport(), timelineGeneration(0), seekRevision(0), traceLoaded(false), hasComparison(false), transportPlaying(false), releaseSelection(false), revision(0) {}
};

// A fixed-size triple buffer for one app-thread writer and one audio callback reader.
// The producer can replace unread state without waiting for the callback.
class LatestAudioStateMailbox
{
public:
	LatestAudioStateMailbox();
	void publish(const AudioPresentationState& state);
	bool consumeLatest(AudioPresentationState& state);

private:
	static const unsigned int DirtyBit = 4;
	static const unsigned int IndexMask = 3;
	AudioPresentationState slots_[3];
	std::atomic<unsigned int> middle_;
	unsigned int producerIndex_;
	unsigned int consumerIndex_;
};

class AudioPresentationBridge
{
public:
	AudioPresentationBridge();
	~AudioPresentationBridge();
	AudioPresentationBridge(const AudioPresentationBridge&) = delete;
	AudioPresentationBridge& operator=(const AudioPresentationBridge&) = delete;
	// Pass open-device availability here, even while a timeline/seek is preparing.
	void update(const sortinglogic::Simulation& simulation,
		const sortinglogic::RenderSnapshot& snapshot,
		const SoundEngine::Parameters& parameters,
		LatestAudioStateMailbox& mailbox,
		const SoundEngine::ReconstructedParameters& reconstructedParameters = SoundEngine::ReconstructedParameters(),
		SoundEngine::PlaybackTimingMode timingMode = SoundEngine::PlaybackTimingMode::FixedDuration,
		bool audioDeviceAvailable = false);
	void collectRetiredTimelines(std::uint64_t acknowledgedGeneration, bool callbackAvailable,
		std::uint64_t acknowledgedSeekRevision = 0);
	void republish(LatestAudioStateMailbox& mailbox);
	std::uint64_t timelineGeneration() const { return timelineGeneration_; }
	std::uint64_t transportRevision() const { return transportRevision_; }
	bool timelinePreparing() const { return timelinePreparing_; }
	bool timelineReady() const { return currentTimeline_ != nullptr && !timelinePreparing_ && timelineError_.empty(); }
	// Preparation readiness is not device health. Feedback from the temporary
	// drain state must stay fenced until the callback has installed the snapshot.
	bool playheadReady(std::uint64_t acknowledgedGeneration, std::uint64_t acknowledgedSeekRevision) const;
	const std::string& timelineError() const { return timelineError_; }

private:
	const sortinglogic::SortTrace* lastTrace_;
	std::size_t lastCursor_;
	SoundEngine::AudioComparison lastComparison_;
	SoundEngine::EventAccentSummary lastEventAccents_;
	bool hasComparison_;
	bool lastComparisonIsProbe_;
	std::map<std::uint64_t, float> valuesById_;
	std::map<std::string, std::size_t> workspaceSizes_;
	float minimumValue_;
	float maximumValue_;
	AudioPresentationState lastPublished_;
	bool hasPublished_;
	std::uint64_t revision_;
	std::vector<std::unique_ptr<SoundEngine::PreparedAudioTimeline> > timelines_;
	const SoundEngine::PreparedAudioTimeline* currentTimeline_;
	std::uint64_t timelineGeneration_;
	std::uint64_t seekRevision_;
	std::uint64_t audioParameterRevision_;
	std::uint64_t transportRevision_;
	std::size_t lastTransportCursor_;
	double lastTimelineDurationSeconds_;
	SoundEngine::PlaybackTimingMode lastTimingMode_;
	const SoundEngine::PreparedAudioSeekState* currentSeekState_;
	std::vector<std::unique_ptr<SoundEngine::PreparedAudioSeekState> > seekStates_;
	struct TimelineBuildRequest
	{
		bool prepareSeek;
		std::shared_ptr<const sortinglogic::SortTrace> trace;
		const SoundEngine::PreparedAudioTimeline* timeline;
		double durationSeconds;
		SoundEngine::PlaybackTimingMode timingMode;
		SoundEngine::Parameters parameters;
		SoundEngine::ReconstructedParameters reconstructedParameters;
		SoundEngine::AudioTimelineGainSettings timelineGains;
		SoundEngine::AudioTransportState transport;
		std::uint64_t parameterRevision;
		std::uint64_t seekRevision;
		std::uint64_t generation;
		TimelineBuildRequest() : prepareSeek(false), trace(), timeline(nullptr), durationSeconds(0.0),
			timingMode(SoundEngine::PlaybackTimingMode::FixedDuration), parameters(), reconstructedParameters(), timelineGains(),
			transport(), parameterRevision(0), seekRevision(0), generation(0) {}
	};
	struct TimelineBuildResult
	{
		std::unique_ptr<SoundEngine::PreparedAudioTimeline> timeline;
		std::unique_ptr<SoundEngine::PreparedAudioSeekState> seekState;
		std::string error;
		std::uint64_t generation;
		std::uint64_t seekRevision;
		std::uint64_t parameterRevision;
		std::uint64_t transportRevision;
		bool preparedSeek;
		TimelineBuildResult() : timeline(), seekState(), error(), generation(0), seekRevision(0), parameterRevision(0), transportRevision(0), preparedSeek(false) {}
	};
	std::mutex timelineBuildMutex_;
	std::condition_variable timelineBuildCondition_;
	TimelineBuildRequest pendingTimelineBuild_;
	TimelineBuildResult completedTimelineBuild_;
	std::uint64_t requestedTimelineGeneration_;
	bool hasPendingTimelineBuild_;
	bool hasCompletedTimelineBuild_;
	bool stopTimelineWorker_;
	bool lastAudioDeviceAvailable_;
	bool seekPreparing_;
	std::uint64_t requestedSeekRevision_;
	bool timelinePreparing_;
	std::string timelineError_;
	std::thread timelineWorker_;

	void recomputeComparison(const sortinglogic::SortTrace* trace, std::size_t cursor,
		SoundEngine::AudioComparison& comparison, bool& hasComparison, bool& isProbe) const;
	bool makeComparison(const sortinglogic::SortTrace& trace, const sortinglogic::SemanticEvent& event,
		SoundEngine::AudioComparison& comparison) const;
	void requestTimelineBuild(const std::shared_ptr<const sortinglogic::SortTrace>& trace, double durationSeconds,
		SoundEngine::PlaybackTimingMode timingMode, const SoundEngine::Parameters& parameters);
	void requestSeekBuild(const SoundEngine::AudioTransportState& transport,
		const SoundEngine::ReconstructedParameters& parameters, const SoundEngine::AudioTimelineGainSettings& gains);
	void pollTimelineBuild();
	void timelineWorkerLoop();
};
