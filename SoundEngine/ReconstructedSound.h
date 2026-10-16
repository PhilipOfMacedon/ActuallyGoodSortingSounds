#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "ReconstructedDsp.h"
#include "AudioTimeline.h"

namespace SoundEngine
{
	enum class ReconstructedMapping : std::uint8_t { LinearHz, LogFrequency };
	enum class ReconstructedIdlePolicy : std::uint8_t { HoldSelection, ReleaseSelection };

	// Application-facing controls for the reconstructed delayed sine-burst model.
	// The defaults reproduce andrut_delayed_sine_bursts_v1.json.
	struct ReconstructedParameters
	{
		double minimumFrequencyHz;
		double maximumFrequencyHz;
		double excitationClockHz;
		double carrierCycles;
		double tukeyFraction;
		double secondOperandDelayMicroseconds;
		double masterLevel;
		double specialReleaseMilliseconds;
		double lowPassCutoffHz;
		bool audioEnabled;
		bool lowPassEnabled;
		ReconstructedMapping mapping;
		ReconstructedIdlePolicy idlePolicy;
		ReconstructedParameters();
	};

	// Roles use the stable SoundEngine comparison role numeric values. They are
	// intentionally untyped here so this interface stays independent of traces.
	struct BurstComparison
	{
		double firstPosition;
		double secondPosition;
		std::uint8_t firstRole;
		std::uint8_t secondRole;
		double firstGain;
		double secondGain;
		bool probeOnly;
		BurstComparison();
	};

	// Fully-owned fixed-capacity DSP snapshot for a seek. It contains no trace
	// events or sorting types; the timeline pointer remains owned by the app.
	struct PreparedAudioSeekState
	{
		const PreparedAudioTimeline* timeline;
		std::uint64_t timelineGeneration;
		std::uint64_t parameterRevision;
		std::uint64_t transportRevision;
		std::uint64_t targetFrame;
		std::size_t targetCursor;
		std::array<BurstNote, ReconstructedNoteCapacityPerVoice> notes[2];
		std::array<double, ReconstructedFirTapCount> firHistory;
		std::size_t firPosition;
		std::uint64_t sampleClock;
		std::uint64_t nextClockFrame;
		std::uint64_t firstLaneFrame;
		std::uint64_t timelineAnchorSample;
		std::uint64_t timelineAnchorFrame;
		double clockRemainder;
		double currentMaster;
		double targetMaster;
		double idleFadeRemaining;
		double idleFadeLength;
		double smoothedCutoff;
		double filterB0, filterB1, filterB2, filterA1, filterA2;
		double filterX1, filterX2, filterY1, filterY2;
		std::size_t filterControlCountdown;
		BurstComparison comparison;
		BurstComparison pendingComparison;
		ReconstructedParameters pendingParameters;
		AudioTransportState transport;
		double pendingBurstGain;
		bool hasComparison;
		bool comparisonBirthsEnabled;
		bool firstLanePending;
		bool firstLaneScheduled;
		bool releaseSelectionActive;
		bool valid;
		PreparedAudioSeekState();
	};

	// A fixed-capacity, allocation-free renderer. The callback owner applies
	// controls and comparisons, then calls render on the same thread.
	class ReconstructedSynthesizer
	{
	public:
		ReconstructedSynthesizer();
		void setParameters(const ReconstructedParameters& parameters);
		const ReconstructedParameters& parameters() const { return parameters_; }
		void setComparison(const BurstComparison& comparison);
		void setPreparedTimeline(const PreparedAudioTimeline* timeline, std::uint64_t generation);
		void setTimelineGainSettings(const AudioTimelineGainSettings& gains);
		void setTransportState(const AudioTransportState& transport);
		static bool prepareSeekState(const PreparedAudioTimeline& timeline,
			const ReconstructedParameters& parameters, const AudioTimelineGainSettings& gains,
			const AudioTransportState& transport, std::uint64_t parameterRevision,
			PreparedAudioSeekState& result, std::string* error = nullptr);
		void installPreparedSeekState(const PreparedAudioSeekState& state);
		AudioPlayheadFeedback playheadFeedback() const;
		void clearComparison();
		void stopBirths();
		void setMasterLevel(double level);
		void render(float* interleavedStereo, std::size_t frames);
		// Renders at the device rate while keeping burst timing and FIR processing
		// on the fixed 44.1 kHz reference clock. The 44.1 kHz path is bit-identical
		// to render(); other rates use preallocated streaming interpolation.
		void renderAtSampleRate(float* interleavedStereo, std::size_t frames, std::uint32_t deviceSampleRate);
	// Same prepared streaming path without the device safety clamp. Useful when
	// the host mixes several pre-rendered states and clamps only the final mix.
	void renderAtSampleRateLinear(float* interleavedStereo, std::size_t frames, std::uint32_t deviceSampleRate);
		void reset();
		std::uint64_t sampleClock() const { return sampleClock_; }
		std::size_t activeNoteCount(std::size_t voice) const;
		bool overloaded() const { return overloadCount_ != 0; }
		std::uint64_t overloadCount() const { return overloadCount_; }
	private:
		ReconstructedParameters parameters_;
		BurstComparison comparison_;
		BurstComparison pendingComparison_;
		ReconstructedParameters pendingParameters_;
		std::array<DelayedSineBurstSource, 2> noteSources_;
		ReferenceFir fir_;
		const PreparedAudioTimeline* timeline_;
		AudioTransportState transport_;
		AudioTimelineGainSettings timelineGains_;
		std::uint64_t transportRevision_;
		// Immutable per transport run. timelineFrame_ is only the last published
		// playhead; it must never feed back into elapsed-time calculation.
		std::uint64_t timelineAnchorSample_;
		std::uint64_t timelineAnchorFrame_;
		std::uint64_t timelineFrame_;
		std::size_t timelineCursor_;
		static const std::size_t ResamplerCapacity = 2048;
		std::array<float, ResamplerCapacity * 2> resamplerRing_;
		std::array<float, 512> resamplerScratch_;
		std::uint64_t resamplerBaseFrame_;
		std::uint64_t resamplerGeneratedFrames_;
		double resamplerSourcePosition_;
		std::uint32_t resamplerDeviceRate_;
		std::uint64_t sampleClock_;
		std::uint64_t nextClockFrame_;
		std::uint64_t firstLaneFrame_;
		double clockRemainder_;
		double currentMaster_;
		double targetMaster_;
		double idleFadeRemaining_;
		double idleFadeLength_;
		double smoothedCutoff_;
		double filterB0_, filterB1_, filterB2_, filterA1_, filterA2_;
		double filterX1_, filterX2_, filterY1_, filterY2_;
		std::size_t filterControlCountdown_;
		bool hasComparison_;
		bool comparisonBirthsEnabled_;
		bool firstLanePending_;
		bool firstLaneScheduled_;
		bool releaseSelectionActive_;
		double pendingBurstGain_;
		bool overloadLatched_;
		std::uint64_t overloadCount_;

		void launchTick();
		void updateTimelineSelection();
		void selectTimelineEvent(const AudioTimelineEvent& event);
		std::uint64_t timelineFrameAtSample(std::uint64_t sample) const;
		bool launchNote(std::size_t voice, double startOffsetSamples, double gain,
			const BurstComparison& comparison, const ReconstructedParameters& parameters);
		double filterSample(double sample);
		void updateFilterCoefficients();
	void renderAtSampleRateImpl(float* interleavedStereo, std::size_t frames,
		std::uint32_t deviceSampleRate, bool applySafetyClamp);
	void applyOutputSafetyClamp(float* interleavedStereo, std::size_t frames);
	};
}
