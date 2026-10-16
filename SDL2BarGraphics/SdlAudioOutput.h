#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "AudioPresentationBridge.h"

class SdlAudioOutput
{
public:
	explicit SdlAudioOutput(LatestAudioStateMailbox& mailbox);
	~SdlAudioOutput();

	SdlAudioOutput(const SdlAudioOutput&) = delete;
	SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

	bool retry();
	bool available() const { return device_ != 0; }
	const std::string& status() const { return status_; }
	std::uint64_t acknowledgedTimelineGeneration() const { return acknowledgedTimelineGeneration_.load(std::memory_order_acquire); }
	std::uint64_t acknowledgedSeekRevision() const { return acknowledgedSeekRevision_.load(std::memory_order_acquire); }
	std::uint64_t outputOverloadCount() const { return outputOverloadCount_.load(std::memory_order_acquire); }
	bool readPlayheadFeedback(SoundEngine::AudioPlayheadFeedback& feedback) const;
	bool armDiagnosticCapture();
	void serviceDiagnosticCapture();

private:
	enum class CaptureState : unsigned int
	{
		Idle,
		Armed,
		Complete,
		Saving,
		Saved
	};

	struct CallbackRecord
	{
		std::uint64_t frameOffset;
		std::uint32_t frameCount;
		std::uint64_t intervalTicks;
		std::uint64_t renderTicks;
		std::uint64_t deadlineTicks;
		std::uint64_t timelineFrame;
		std::uint64_t timelineGeneration;
		std::uint64_t seekRevision;
		std::uint64_t overloadSamples;
		std::uint32_t sourceCursor;
		float rms;
		bool traceLoaded;
		bool hasComparison;
		bool timelinePrepared;
		bool deadlineMissed;
	};

	static const unsigned int DiagnosticDurationSeconds = 30;

	LatestAudioStateMailbox& mailbox_;
	SDL_AudioDeviceID device_;
	SDL_AudioSpec obtained_;
	bool subsystemInitialized_;
	std::string status_;
	SoundEngine::Synthesizer synthesizer_;
	SoundEngine::ReconstructedSynthesizer reconstructedSynthesizers_[2];
	unsigned int activeReconstructedSynthesizer_;
	bool seekCrossfading_;
	std::size_t seekCrossfadeProgress_;
	std::size_t seekCrossfadeFrames_;
	std::array<float, 1024> crossfadeOldScratch_;
	std::array<float, 1024> crossfadeNewScratch_;
	std::uint64_t appliedRevision_;
	std::uint64_t appliedTimelineGeneration_;
	const SoundEngine::PreparedAudioTimeline* appliedTimeline_;
	std::uint64_t appliedTransportRevision_;
	std::atomic<std::uint64_t> acknowledgedTimelineGeneration_;
	std::atomic<std::uint64_t> acknowledgedSeekRevision_;
	std::uint64_t appliedSeekRevision_;
	const SoundEngine::PreparedAudioSeekState* appliedSeekState_;
	std::atomic<std::uint64_t> outputOverloadCount_;
	mutable std::atomic<std::uint64_t> feedbackSequence_;
	std::atomic<std::uint64_t> feedbackFrame_;
	std::atomic<std::uint64_t> feedbackCursor_;
	std::atomic<std::uint64_t> feedbackGeneration_;
	std::atomic<std::uint64_t> feedbackTransportRevision_;
	std::atomic<std::uint64_t> feedbackDurationFrames_;
	std::atomic<bool> feedbackPlaying_;
	std::vector<float> diagnosticSamples_;
	std::vector<CallbackRecord> callbackRecords_;
	std::atomic<unsigned int> captureState_;
	std::size_t targetCaptureFrames_;
	std::size_t captureFramesWritten_;
	std::size_t callbackRecordCount_;
	std::uint64_t lastCallbackStartCounter_;
	std::uint64_t performanceFrequency_;
	bool callbackTraceLoaded_;
	bool callbackHasComparison_;
	bool callbackTimelinePrepared_;
	std::uint64_t callbackTimelineGeneration_;
	std::uint64_t callbackSeekRevision_;

	static void audioCallback(void* userdata, Uint8* stream, int length);
	void render(Uint8* stream, int length);
	bool saveDiagnosticCapture();
};
