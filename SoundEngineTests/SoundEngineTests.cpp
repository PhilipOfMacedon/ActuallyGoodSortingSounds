#include "SoundEngine.h"
#include "ComposableEngine.h"
#include "AudioTimeline.h"
#include "ReconstructedSound.h"
#include "LegacySoundEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	void require(bool condition, const std::string& message);
	double maxAbs(const std::vector<float>& samples);
	extern std::atomic<bool> countAllocations;
	extern std::atomic<std::size_t> allocationCount;

	void testPreparedAudioTimelineMappings()
	{
		using namespace SoundEngine;
		std::vector<AudioTimelineInputEvent> events(5);
		events[0].sourceCursor = 1;
		events[0].kind = TimelineEventKind::Other;
		events[1].sourceCursor = 2;
		events[1].kind = TimelineEventKind::Comparison;
		events[1].firstPosition = .2;
		events[1].secondPosition = .8;
		events[2].sourceCursor = 3;
		events[2].kind = TimelineEventKind::Other;
		events[3].sourceCursor = 4;
		events[3].kind = TimelineEventKind::Probe;
		events[3].firstPosition = events[3].secondPosition = .6;
		events[4].sourceCursor = 5;
		events[4].kind = TimelineEventKind::Other;

		PreparedAudioTimeline fixed;
		std::string error;
		require(fixed.prepare(events, 5, PlaybackTimingMode::FixedDuration, 4.0, 44100, 300, &error),
			"fixed-duration audio timeline preparation failed: " + error);
		require(fixed.durationFrames() == 176400 && fixed.frameForCursor(1) == 35280 &&
			fixed.frameForCursor(5) == fixed.durationFrames(),
			"fixed-duration timeline does not map the full event history across the selected duration");
		require(fixed.cursorAtFrame(35279) == 0 && fixed.cursorAtFrame(35280) == 1 &&
			fixed.cursorAtFrame(176400) == 5,
			"fixed-duration playhead lost its exact source event cursor");
		AudioTimelineEvent selected;
		require(!fixed.comparisonAtFrame(70560 - 1, selected) && fixed.comparisonAtFrame(70560, selected) &&
			selected.sourceCursor == 2 && selected.firstPosition == .2 && selected.secondPosition == .8,
			"fixed-duration comparison lookup did not select the latest due event");

		PreparedAudioTimeline cadence;
		require(cadence.prepare(events, 5, PlaybackTimingMode::ReferenceCadence, 10.0, 44100, 300, &error),
			"reference-cadence audio timeline preparation failed: " + error);
		require(cadence.durationFrames() == 294 && cadence.durationSeconds() == 294.0 / 44100.0 &&
			cadence.eligibleEventCount() == 2,
			"reference cadence duration is not based on its eligible comparison count");
		require(cadence.cursorAtFrame(0) == 2 && cadence.cursorAtFrame(146) == 2 &&
			cadence.cursorAtFrame(147) == 4 && cadence.cursorAtFrame(294) == 5,
			"reference cadence did not apply intervening and trailing source events in order");
		require(cadence.comparisonAtFrame(0, selected) && selected.sourceCursor == 2 &&
			cadence.comparisonAtFrame(147, selected) && selected.sourceCursor == 4 &&
			selected.kind == TimelineEventKind::Probe,
			"reference cadence did not schedule eligible comparisons into consecutive slots");

		PreparedAudioTimeline comparisonScaled;
		require(comparisonScaled.prepare(events, 5, PlaybackTimingMode::FixedDuration, 4.0, 44100, 300, &error,
			FixedDurationSchedule::EligibleEvents),
			"comparison-scaled fixed-duration timeline preparation failed: " + error);
		require(comparisonScaled.durationFrames() == 176400 && comparisonScaled.eligibleEventCount() == 2 &&
			comparisonScaled.frameForCursor(1) == 0 && comparisonScaled.frameForCursor(2) == 0 &&
			comparisonScaled.frameForCursor(3) == 88200 && comparisonScaled.frameForCursor(4) == 88200 &&
			comparisonScaled.frameForCursor(5) == comparisonScaled.durationFrames(),
			"fixed duration scales the eligible event cadence and attaches bookkeeping to adjacent boundaries");
		require(comparisonScaled.cursorAtFrame(0) == 2 && comparisonScaled.cursorAtFrame(88199) == 2 &&
			comparisonScaled.cursorAtFrame(88200) == 4 && comparisonScaled.cursorAtFrame(176400) == 5,
			"comparison-scaled fixed duration retains exact source cursors at each boundary");

		std::vector<AudioTimelineInputEvent> empty;
		PreparedAudioTimeline noComparisons;
		require(noComparisons.prepare(empty, 0, PlaybackTimingMode::ReferenceCadence, 0.0, 44100, 300, &error) &&
			noComparisons.durationFrames() == 0 && noComparisons.cursorAtFrame(0) == 0 &&
			!noComparisons.comparisonAtFrame(0, selected),
			"empty eligible schedule should complete without sound");
		events[2].sourceCursor = 2;
		require(!fixed.prepare(events, 5, PlaybackTimingMode::FixedDuration, 4.0, 44100, 300, &error),
			"out-of-order source cursors should be rejected during preparation");
	}

	void testParameterNormalization()
	{
		using namespace SoundEngine;
		Parameters input;
		input.masterLevel = 1.5f;
		input.bedLevel = -0.5f;
		input.bedHarmonicExponent = std::numeric_limits<float>::quiet_NaN();
		input.bedComb.phaseOffsetDegrees = std::numeric_limits<float>::infinity();
		input.mappingMode = static_cast<MappingMode>(99);
		input.comparisonSpreadMode = static_cast<ComparisonSpreadMode>(99);
		input.chorusWeighting = static_cast<ChorusWeighting>(99);
		input.roleBoostDb.fill(30.0f);
		input.bandTrimDb.fill(-120.0f);
		input.accentGain.fill(0.8f);

		const Parameters normalized = normalizeParameters(input);
		Synthesizer runtime;
		runtime.setParameters(input);
		require(runtime.parameters() == normalized,
			"pure audio normalization and Synthesizer::setParameters must produce identical control values");
		require(normalized.masterLevel == 1.0f && normalized.bedLevel == 0.0f && normalized.bedHarmonicExponent == 1.5f &&
			normalized.bedComb.phaseOffsetDegrees == 12.0f && normalized.mappingMode == MappingMode::LinearHz &&
			normalized.comparisonSpreadMode == ComparisonSpreadMode::FixedBins &&
			normalized.chorusWeighting == ChorusWeighting::Uniform && normalized.roleBoostDb[0] == 12.0f &&
			normalized.bandTrimDb[0] == -96.0f && normalized.accentGain[0] == 0.5f,
			"audio normalization preserves existing finite bounds, non-finite defaults, and invalid-enum fallbacks");
		require(normalizeParameters(normalized) == normalized,
			"audio parameter normalization is idempotent");

		ReconstructedParameters previous;
		previous.minimumFrequencyHz = 250.0;
		previous.maximumFrequencyHz = 5000.0;
		ReconstructedParameters conflicting = previous;
		conflicting.minimumFrequencyHz = 1000.0;
		conflicting.maximumFrequencyHz = 1000.0;
		conflicting.excitationClockHz = 900.0;
		conflicting.masterLevel = -1.0;
		const ReconstructedParameters normalizedReconstructed = normalizeReconstructedParameters(conflicting, previous);
		ReconstructedSynthesizer reconstructedRuntime;
		reconstructedRuntime.setParameters(previous);
		reconstructedRuntime.setParameters(conflicting);
		require(reconstructedRuntime.parameters() == normalizedReconstructed,
			"pure reconstructed-parameter normalization matches the renderer's stateful fallback behavior");
		require(normalizedReconstructed.minimumFrequencyHz == previous.minimumFrequencyHz &&
			normalizedReconstructed.maximumFrequencyHz == previous.maximumFrequencyHz &&
			normalizedReconstructed.excitationClockHz == 600.0 && normalizedReconstructed.masterLevel == 0.0,
			"reconstructed normalization preserves previous frequency bounds and existing scalar clamps");
	}

	void testTimelineDrivenBurstPlayback()
	{
		using namespace SoundEngine;
		std::vector<AudioTimelineInputEvent> events(3);
		events[0].sourceCursor = 1;
		events[0].kind = TimelineEventKind::Comparison;
		events[0].firstPosition = .1;
		events[0].secondPosition = .9;
		events[0].firstRole = 1;
		events[0].secondRole = 2;
		events[1].sourceCursor = 2;
		events[1].kind = TimelineEventKind::Comparison;
		events[1].firstPosition = .3;
		events[1].secondPosition = .7;
		events[2].sourceCursor = 3;
		events[2].kind = TimelineEventKind::Other;
		PreparedAudioTimeline timeline;
		std::string error;
		require(timeline.prepare(events, 3, PlaybackTimingMode::FixedDuration, 1.0, 44100, 300, &error),
			"timeline playback fixture failed preparation: " + error);
		ReconstructedSynthesizer whole, partitioned;
		ReconstructedParameters parameters;
		whole.setParameters(parameters);
		partitioned.setParameters(parameters);
		whole.setPreparedTimeline(&timeline, 7);
		partitioned.setPreparedTimeline(&timeline, 7);
		AudioTransportState transport;
		transport.generation = 7;
		transport.revision = 1;
		transport.playing = true;
		whole.setTransportState(transport);
		partitioned.setTransportState(transport);
		// Timeline elapsed time starts at the first scheduled excitation tick,
		// which is held 128 source frames after the renderer's initial clock.
		const std::size_t playbackFrames = 44100 + 128;
		std::vector<float> output(playbackFrames * 2), pieces(output.size());
		whole.render(output.data(), playbackFrames);
		const std::size_t blocks[] = { 1, 113, 509, 7, 2048, 4093, 31, 8191 };
		std::size_t offset = 0, block = 0;
		while (offset < playbackFrames)
		{
			const std::size_t count = (std::min)(blocks[block++ % (sizeof(blocks) / sizeof(blocks[0]))], playbackFrames - offset);
			partitioned.render(pieces.data() + offset * 2, count);
			offset += count;
		}
		require(output == pieces,
			"audio-timed comparisons and captured bursts are invariant under irregular callback block partitions");
		require(maxAbs(output) > 1e-6 && whole.sampleClock() == playbackFrames,
			"the prepared timeline starts the burst source from its sample schedule");
		AudioPlayheadFeedback feedback = whole.playheadFeedback();
		require(feedback.generation == 7 && feedback.transportRevision == 1 && feedback.timelineFrame == 44100 &&
			feedback.sourceCursor == 3 && !feedback.playing,
			"audio playhead reports exact source history and completion at the scheduled duration");

		transport.revision = 2;
		transport.playing = false;
		transport.timelineFrame = timeline.frameForCursor(1);
		transport.sourceCursor = 1;
		transport.holdSelection = true;
		whole.setTransportState(transport);
		require(whole.playheadFeedback().sourceCursor == 1 && whole.playheadFeedback().transportRevision == 2,
			"paused audio feedback preserves the exact inspected cursor instead of collapsing same-time steps");
		float pauseOutput[2048 * 2] = {};
		allocationCount.store(0, std::memory_order_relaxed);
		countAllocations.store(true, std::memory_order_relaxed);
		whole.render(pauseOutput, 512);
		countAllocations.store(false, std::memory_order_relaxed);
		require(allocationCount.load(std::memory_order_relaxed) == 0,
			"callback timeline lookup and burst rendering allocate no memory");
		require(whole.activeNoteCount(0) > 0 && whole.activeNoteCount(1) > 0,
			"paused Hold selection continues exciting the exact inspected comparison (" +
			std::to_string(whole.activeNoteCount(0)) + ", " + std::to_string(whole.activeNoteCount(1)) + ")");
		transport.revision = 3;
		transport.holdSelection = false;
		transport.releaseSelection = true;
		parameters.idlePolicy = ReconstructedIdlePolicy::ReleaseSelection;
		parameters.specialReleaseMilliseconds = 0;
		whole.setParameters(parameters);
		whole.setTransportState(transport);
		whole.render(pauseOutput, 2048);
		require(whole.activeNoteCount(0) == 0 && whole.activeNoteCount(1) == 0,
			"Release selection stops future births and drains old notes to exact zero");
	}

	void testReconstructedBurstRenderer()
	{
		using namespace SoundEngine;
		ReconstructedSynthesizer renderer;
		ReconstructedParameters parameters;
		renderer.setParameters(parameters);
		BurstComparison comparison;
		comparison.firstPosition = 0.0f;
		comparison.secondPosition = 1.0f;
		renderer.setComparison(comparison);
		std::vector<float> contiguous(44100 * 2);
		renderer.render(contiguous.data(), contiguous.size() / 2);
		require(renderer.sampleClock() == 44100, "reconstructed renderer keeps a persistent sample clock with a 128-frame prepared lead");
		require(renderer.activeNoteCount(0) > 0 && renderer.activeNoteCount(1) > 0,
			"reconstructed comparison launches two overlapping finite note lanes");
		require(renderer.activeNoteCount(0) <= ReconstructedNoteCapacityPerVoice &&
			renderer.activeNoteCount(1) <= ReconstructedNoteCapacityPerVoice,
			"reconstructed note occupancy stays within its preallocated capacity");
		for (std::size_t i = 0; i < contiguous.size(); ++i)
			require(std::isfinite(contiguous[i]) && std::fabs(contiguous[i]) <= 0.999f,
				"reconstructed output remains finite and device-safe");
		require(renderer.overloadCount() == 0,
			"the calibrated reconstructed reference renders its baseline without invoking the output safety clamp");
		BurstComparison loudComparison = comparison;
		loudComparison.firstGain = loudComparison.secondGain = 1000.0;
		ReconstructedSynthesizer rawLoud;
		rawLoud.setComparison(loudComparison);
		std::array<float, 2048> rawSamples;
		rawLoud.render(rawSamples.data(), rawSamples.size() / 2);
		float rawPeak = 0.0f;
		for (std::size_t i = 0; i < rawSamples.size(); ++i) rawPeak = (std::max)(rawPeak, std::fabs(rawSamples[i]));
		require(rawPeak > 0.999f && rawLoud.overloadCount() == 0,
			"analysis rendering remains linear and does not apply the device safety clamp");
		ReconstructedSynthesizer clampedDevice;
		clampedDevice.setComparison(loudComparison);
		std::array<float, 2048> deviceSamples;
		clampedDevice.renderAtSampleRate(deviceSamples.data(), deviceSamples.size() / 2, 44100);
		bool deviceSafe = true;
		for (std::size_t i = 0; i < deviceSamples.size(); ++i)
			if (std::fabs(deviceSamples[i]) > 0.999f) { deviceSafe = false; break; }
		require(deviceSafe && clampedDevice.overloadCount() > 0,
			"device rendering clamps after synthesis and reports output overloads");

		ReconstructedSynthesizer partitioned;
		partitioned.setParameters(parameters);
		partitioned.setComparison(comparison);
		std::vector<float> split(contiguous.size());
		const std::size_t blocks[] = { 1, 113, 509, 7, 2048, 4093, 31, 8191 };
		std::size_t renderedFrames = 0;
		std::size_t block = 0;
		while (renderedFrames < split.size() / 2)
		{
			const std::size_t frames = (std::min)(blocks[block++ % (sizeof(blocks) / sizeof(blocks[0]))],
				split.size() / 2 - renderedFrames);
			partitioned.render(split.data() + renderedFrames * 2, frames);
			renderedFrames += frames;
		}
		double partitionError = 0.0;
		for (std::size_t i = 0; i < split.size(); ++i)
			partitionError = (std::max)(partitionError, std::fabs(static_cast<double>(split[i] - contiguous[i])));
		require(partitionError == 0.0, "reconstructed output is identical across irregular render block partitions");

		ReconstructedSynthesizer directDeviceRate;
		directDeviceRate.setParameters(parameters);
		directDeviceRate.setComparison(comparison);
		std::vector<float> directRateSamples(4096 * 2);
		ReconstructedSynthesizer referenceRate;
		referenceRate.setParameters(parameters);
		referenceRate.setComparison(comparison);
		std::vector<float> referenceRateSamples(directRateSamples.size());
		directDeviceRate.renderAtSampleRate(directRateSamples.data(), 4096, 44100);
		referenceRate.render(referenceRateSamples.data(), 4096);
		require(directRateSamples == referenceRateSamples,
			"44.1 kHz device rendering remains bit-identical to the frozen native-rate path");

		ReconstructedSynthesizer resampled;
		resampled.setParameters(parameters);
		resampled.setComparison(comparison);
		std::vector<float> at48k(48000 * 2);
		resampled.renderAtSampleRate(at48k.data(), 48000, 48000);
		require(resampled.sampleClock() >= 44100 && resampled.sampleClock() <= 44102,
			"48 kHz streaming output advances the synthesis clock at the fixed 44.1 kHz rate");
		bool resamplerFinite = true;
		for (std::size_t i = 0; i < at48k.size(); ++i)
			if (!std::isfinite(at48k[i]) || std::fabs(at48k[i]) > 0.999f) { resamplerFinite = false; break; }
		require(resamplerFinite, "streaming resampler output remains finite and device-safe");
		const std::uint32_t deviceRates[] = { 22050, 32000, 44100, 48000, 96000 };
		for (std::size_t rateIndex = 0; rateIndex < sizeof(deviceRates) / sizeof(deviceRates[0]); ++rateIndex)
		{
			const std::uint32_t rate = deviceRates[rateIndex];
			const std::size_t frames = rate / 4;
			ReconstructedSynthesizer rateRenderer;
			rateRenderer.setParameters(parameters);
			rateRenderer.setComparison(comparison);
			std::vector<float> rateOutput(frames * 2);
			rateRenderer.renderAtSampleRate(rateOutput.data(), frames, rate);
			const std::uint64_t expectedSourceFrames = 44100 / 4;
			const std::uint64_t actualSourceFrames = rateRenderer.sampleClock();
			bool validRateOutput = actualSourceFrames + 2 >= expectedSourceFrames &&
				actualSourceFrames <= expectedSourceFrames + 2;
			for (std::size_t sample = 0; validRateOutput && sample < rateOutput.size(); ++sample)
				validRateOutput = std::isfinite(rateOutput[sample]) && std::fabs(rateOutput[sample]) <= 0.999f;
			require(validRateOutput,
				"streaming output remains finite and advances the 44.1 kHz source clock correctly at " + std::to_string(rate));
		}

		ReconstructedSynthesizer resampledPartitioned;
		resampledPartitioned.setParameters(parameters);
		resampledPartitioned.setComparison(comparison);
		std::vector<float> at48kSplit(at48k.size());
		const std::size_t rateBlocks[] = { 1, 17, 509, 3, 1024, 7, 211 };
		std::size_t rateRendered = 0, rateBlock = 0;
		while (rateRendered < 48000)
		{
			const std::size_t count = (std::min)(rateBlocks[rateBlock++ % (sizeof(rateBlocks) / sizeof(rateBlocks[0]))], 48000 - rateRendered);
			resampledPartitioned.renderAtSampleRate(at48kSplit.data() + rateRendered * 2, count, 48000);
			rateRendered += count;
		}
		require(at48kSplit == at48k,
			"streaming resampler output is identical across irregular device callback partitions");

		ReconstructedSynthesizer drained;
		drained.setParameters(parameters);
		drained.setComparison(comparison);
		std::vector<float> initial(128 * 2);
		drained.render(initial.data(), initial.size() / 2);
		parameters.idlePolicy = ReconstructedIdlePolicy::ReleaseSelection;
		parameters.specialReleaseMilliseconds = 0.0;
		drained.setParameters(parameters);
		drained.clearComparison();
		std::vector<float> tail(44100 * 2 / 5);
		drained.render(tail.data(), tail.size() / 2);
		require(drained.activeNoteCount(0) == 0 && drained.activeNoteCount(1) == 0,
			"reconstructed note tails reach exact zero and release their slots");
		drained.reset();
		require(drained.sampleClock() == 0 && drained.activeNoteCount(0) == 0,
			"reconstructed reset clears note and filter history without allocation");
	}

	void advanceRenderer(SoundEngine::ReconstructedSynthesizer& renderer, std::uint64_t frames)
	{
		std::array<float, 1024> scratch;
		while (frames > 0)
		{
			const std::size_t count = static_cast<std::size_t>((std::min<std::uint64_t>)(frames, scratch.size() / 2));
			renderer.render(scratch.data(), count);
			frames -= count;
		}
	}

	void testReconstructedTimelineClockAndSeekSnapshots()
	{
		using namespace SoundEngine;
		std::string error;
		std::vector<AudioTimelineInputEvent> events(8);
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			events[i].sourceCursor = i + 1;
			events[i].kind = TimelineEventKind::Comparison;
			events[i].firstPosition = .1 + .08 * static_cast<double>(i);
			events[i].secondPosition = 1.0 - events[i].firstPosition;
			events[i].firstRole = static_cast<std::uint8_t>(i == 2 ? 1 : 0);
		}
		PreparedAudioTimeline timeline;
		require(timeline.prepare(events, events.size(), PlaybackTimingMode::FixedDuration, 3.0, 44100, 300, &error),
			"seek snapshot timeline failed to prepare: " + error);

		ReconstructedSynthesizer clocked;
		ReconstructedParameters parameters;
		parameters.lowPassEnabled = true;
		parameters.lowPassCutoffHz = 500.0;
		clocked.setParameters(parameters);
		clocked.setPreparedTimeline(&timeline, 11);
		AudioTransportState play;
		play.generation = 11;
		play.revision = 1;
		play.playing = true;
		clocked.setTransportState(play);
		advanceRenderer(clocked, 44100 + 128);
		AudioPlayheadFeedback feedback = clocked.playheadFeedback();
		require(feedback.timelineFrame == 44100 && feedback.sourceCursor == 2 && feedback.playing,
			"playback elapsed time must be measured from an immutable anchor at the one-second playhead");
		advanceRenderer(clocked, 44100);
		feedback = clocked.playheadFeedback();
		require(feedback.timelineFrame == 88200 && feedback.sourceCursor == 5 && feedback.playing,
			"successive excitation ticks must not compound elapsed timeline time");
		advanceRenderer(clocked, 44099);
		feedback = clocked.playheadFeedback();
		require(feedback.timelineFrame == 132299 && feedback.playing,
			"a three-second run must remain active on the final source frame before completion");
		advanceRenderer(clocked, 1);
		feedback = clocked.playheadFeedback();
		require(feedback.timelineFrame == 132300 && feedback.sourceCursor == events.size() && !feedback.playing,
			"a fixed-duration run must complete exactly at its duration after the separate 128-frame lead");

		AudioTimelineGainSettings gains;
		const std::uint64_t targetFrames[] = { 10000, 32000, 61000, 85000 };
		for (std::size_t targetIndex = 0; targetIndex < sizeof(targetFrames) / sizeof(targetFrames[0]); ++targetIndex)
		{
			const std::uint64_t target = targetFrames[targetIndex];
			ReconstructedSynthesizer uninterrupted;
			uninterrupted.setParameters(parameters);
			uninterrupted.setTimelineGainSettings(gains);
			uninterrupted.setPreparedTimeline(&timeline, 23);
			AudioTransportState transport;
			transport.generation = 23;
			transport.revision = 2;
			transport.timelineFrame = target;
			transport.sourceCursor = timeline.cursorAtFrame(target);
			transport.playing = true;
			AudioTransportState initial;
			initial.generation = 23;
			initial.revision = 1;
			initial.playing = true;
			uninterrupted.setTransportState(initial);
			advanceRenderer(uninterrupted, 128 + target);

			PreparedAudioSeekState prepared;
			require(ReconstructedSynthesizer::prepareSeekState(timeline, parameters, gains, transport, 7, prepared, &error),
				"bounded seek-state preparation failed: " + error);
			require(prepared.valid && prepared.targetFrame == target && prepared.targetCursor == transport.sourceCursor &&
				prepared.timelineGeneration == 23 && prepared.parameterRevision == 7,
				"prepared seek state omitted its target identity or revision tags");
			ReconstructedSynthesizer restored;
			restored.setParameters(parameters);
			restored.setTimelineGainSettings(gains);
			restored.installPreparedSeekState(prepared);
			std::array<float, 4096> expected, actual;
			uninterrupted.render(expected.data(), expected.size() / 2);
			allocationCount.store(0, std::memory_order_relaxed);
			countAllocations.store(true, std::memory_order_relaxed);
			restored.render(actual.data(), actual.size() / 2);
			countAllocations.store(false, std::memory_order_relaxed);
			double maximumError = 0.0;
			for (std::size_t sample = 0; sample < expected.size(); ++sample)
				maximumError = (std::max)(maximumError, std::fabs(static_cast<double>(expected[sample] - actual[sample])));
			require(maximumError < 2.0e-6,
				"prepared seek output diverged from uninterrupted output at frame " + std::to_string(target) +
				" (max error " + std::to_string(maximumError) + ")");
			require(allocationCount.load(std::memory_order_relaxed) == 0,
				"installing and rendering a fixed-size seek snapshot must allocate nothing");
		}
	}

	void testReleaseSelectionFadeDurations()
	{
		using namespace SoundEngine;
		std::vector<AudioTimelineInputEvent> events(20);
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			events[i].sourceCursor = i + 1;
			events[i].kind = TimelineEventKind::Comparison;
			events[i].firstPosition = .2;
			events[i].secondPosition = .8;
		}
		PreparedAudioTimeline timeline;
		std::string error;
		require(timeline.prepare(events, events.size(), PlaybackTimingMode::FixedDuration, 2.0, 44100, 300, &error),
			"release fade fixture failed to prepare: " + error);
		double measuredRms[3] = {};
		const double releaseMilliseconds[] = { 0.0, 250.0, 2000.0 };
		for (std::size_t test = 0; test < 3; ++test)
		{
			ReconstructedSynthesizer renderer;
			ReconstructedParameters parameters;
			renderer.setParameters(parameters);
			renderer.setPreparedTimeline(&timeline, 31);
			AudioTransportState playing;
			playing.generation = 31;
			playing.revision = 1;
			playing.playing = true;
			renderer.setTransportState(playing);
			advanceRenderer(renderer, 128 + 20000);
			const AudioPlayheadFeedback beforePause = renderer.playheadFeedback();
			parameters.idlePolicy = ReconstructedIdlePolicy::ReleaseSelection;
			parameters.specialReleaseMilliseconds = releaseMilliseconds[test];
			renderer.setParameters(parameters);
			AudioTransportState paused;
			paused.generation = 31;
			paused.revision = 2;
			paused.timelineFrame = beforePause.timelineFrame;
			paused.sourceCursor = beforePause.sourceCursor;
			paused.releaseSelection = true;
			renderer.setTransportState(paused);
			std::vector<float> window(8820 * 2);
			renderer.render(window.data(), 8820);
			double squareSum = 0.0;
			for (std::size_t i = 0; i < window.size(); ++i) squareSum += static_cast<double>(window[i]) * window[i];
			measuredRms[test] = std::sqrt(squareSum / window.size());
			const std::uint64_t remaining = static_cast<std::uint64_t>(releaseMilliseconds[test] * 44.1) + 20000;
			advanceRenderer(renderer, remaining);
			require(renderer.activeNoteCount(0) == 0 && renderer.activeNoteCount(1) == 0,
				"Release selection stops new births and drains each configured fade to exact zero");
		}
		require(measuredRms[1] > measuredRms[0] && measuredRms[2] > measuredRms[1],
			"0, 250, and 2,000 ms release settings produce progressively longer audible fades");
	}

	std::atomic<bool> countAllocations(false);
	std::atomic<std::size_t> allocationCount(0);
}

void* operator new(std::size_t size)
{
	if (countAllocations.load(std::memory_order_relaxed))
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
	throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
	if (countAllocations.load(std::memory_order_relaxed))
		allocationCount.fetch_add(1, std::memory_order_relaxed);
	if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
	throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

namespace
{
	const std::uint32_t SampleRate = 44100;
	const double Pi = 3.1415926535897932384626433832795;
	const double TwoPi = Pi * 2.0;
	int assertions = 0;

	void require(bool condition, const std::string& message)
	{
		++assertions;
		if (!condition)
			throw std::runtime_error(message);
	}

	bool near(double actual, double expected, double tolerance)
	{
		return std::fabs(actual - expected) <= tolerance;
	}

	double sineAmplitude(const std::vector<float>& stereo, std::size_t startFrame, std::size_t frameCount, double frequency)
	{
		double cosine = 0.0;
		double sine = 0.0;
		for (std::size_t i = 0; i < frameCount; ++i)
		{
			const double phase = TwoPi * frequency * static_cast<double>(startFrame + i) / SampleRate;
			const double sample = stereo[(startFrame + i) * 2];
			cosine += sample * std::cos(phase);
			sine += sample * std::sin(phase);
		}
		return 2.0 * std::sqrt(cosine * cosine + sine * sine) / frameCount;
	}

	double normalizedBandAmplitude(const std::vector<float>& samples, std::size_t startFrame, std::size_t frameCount, std::size_t bandIndex)
	{
		const SoundEngine::FrequencyBand& band = SoundEngine::frequencyBank()[bandIndex];
		return sineAmplitude(samples, startFrame, frameCount, band.frequencyHz) / band.baseAmplitude;
	}

	double maxAbs(const std::vector<float>& samples)
	{
		double peak = 0.0;
		for (std::size_t i = 0; i < samples.size(); ++i)
			peak = std::max(peak, std::fabs(static_cast<double>(samples[i])));
		return peak;
	}

	LegacySoundEngine::Parameters makeLegacyParameters(const SoundEngine::Parameters& source)
	{
		LegacySoundEngine::Parameters target;
		target.masterLevel = source.masterLevel;
		target.bedLevel = source.bedLevel;
		target.bedHarmonicExponent = source.bedHarmonicExponent;
		target.includeSub300HzBed = source.includeSub300HzBed;
		target.bedComb.enabled = source.bedComb.enabled;
		target.bedComb.applyPhase = source.bedComb.applyPhase;
		target.bedComb.delayMicroseconds = source.bedComb.delayMicroseconds;
		target.bedComb.phaseOffsetDegrees = source.bedComb.phaseOffsetDegrees;
		target.bedComb.wetAtLowFrequency = source.bedComb.wetAtLowFrequency;
		target.bedComb.wetAtHighFrequency = source.bedComb.wetAtHighFrequency;
		target.bedComb.fadeStartHz = source.bedComb.fadeStartHz;
		target.bedComb.fadeEndHz = source.bedComb.fadeEndHz;
		target.bedComb.fadeExponent = source.bedComb.fadeExponent;
		target.comparisonBoostDb = source.comparisonBoostDb;
		target.heldKeyBoostDb = source.heldKeyBoostDb;
		target.roleBoostDb = source.roleBoostDb;
		target.tauBelowBins = source.tauBelowBins;
		target.tauAboveBins = source.tauAboveBins;
		target.chorusMix = source.chorusMix;
		target.chorusRateHz = source.chorusRateHz;
		target.chorusDepthMs = source.chorusDepthMs;
		target.chorusWeighting = static_cast<LegacySoundEngine::ChorusWeighting>(source.chorusWeighting);
		target.attackMs = source.attackMs;
		target.releaseMs = source.releaseMs;
		target.mappingMode = static_cast<LegacySoundEngine::MappingMode>(source.mappingMode);
		target.comparisonSpreadMode = static_cast<LegacySoundEngine::ComparisonSpreadMode>(source.comparisonSpreadMode);
		target.bandTrimDb = source.bandTrimDb;
		target.accentRouting = source.accentRouting;
		target.accentGain = source.accentGain;
		target.enabled = source.enabled;
		return target;
	}

	void requirePcmEqual(const std::vector<float>& actual, const std::vector<float>& reference, const std::string& context)
	{
		require(actual.size() == reference.size(), context + ": output sizes differ");
		for (std::size_t i = 0; i < actual.size(); ++i)
		{
			if (actual[i] != reference[i])
				throw std::runtime_error(context + ": PCM mismatch at sample " + std::to_string(i));
			++assertions;
		}
	}

	void testLegacyRendererBitExactParity()
	{
		SoundEngine::Synthesizer current;
		LegacySoundEngine::Synthesizer frozen;
		auto renderAndCompare = [&](std::size_t frames, std::uint32_t rate, const std::string& label)
		{
			std::vector<float> actual(frames * 2), reference(frames * 2);
			current.render(actual.data(), frames, rate);
			frozen.render(reference.data(), frames, rate);
			requirePcmEqual(actual, reference, label);
		};
		current.setTraceLoaded(true);
		frozen.setTraceLoaded(true);
		const SoundEngine::MappingMode mappings[] = {
			SoundEngine::MappingMode::LinearHz,
			SoundEngine::MappingMode::LogFrequency,
			SoundEngine::MappingMode::EqualBandSlots };
		const SoundEngine::ChorusWeighting chorusModes[] = {
			SoundEngine::ChorusWeighting::Uniform,
			SoundEngine::ChorusWeighting::ComparisonEnvelope };
		for (std::size_t i = 0; i < 12; ++i)
		{
			SoundEngine::Parameters parameters = current.parameters();
			parameters.mappingMode = mappings[i % 3];
			parameters.comparisonSpreadMode = (i & 1u)
				? SoundEngine::ComparisonSpreadMode::PositionScaled
				: SoundEngine::ComparisonSpreadMode::FixedBins;
			parameters.chorusWeighting = chorusModes[(i / 2) % 2];
			parameters.chorusMix = (i % 3) * 0.07f;
			parameters.chorusRateHz = 0.4f + 0.17f * static_cast<float>(i);
			parameters.chorusDepthMs = 0.25f + 0.31f * static_cast<float>(i % 7);
			parameters.bedComb.enabled = (i & 1u) != 0;
			parameters.bedComb.applyPhase = (i % 3) != 0;
			parameters.includeSub300HzBed = (i % 4) == 0;
			parameters.bedHarmonicExponent = 0.75f + 0.17f * static_cast<float>(i % 10);
			parameters.tauBelowBins = 0.1f + 0.07f * static_cast<float>(i % 6);
			parameters.tauAboveBins = 0.5f + 0.11f * static_cast<float>(i % 8);
			parameters.bandTrimDb[i % SoundEngine::BandCount] = -12.0f + static_cast<float>(i);
			parameters.roleBoostDb[i % SoundEngine::RoleCount] = static_cast<float>(i) - 4.0f;
			current.setParameters(parameters);
			frozen.setParameters(makeLegacyParameters(parameters));
			const SoundEngine::ComparisonRole firstRole = (i % 4 == 0)
				? SoundEngine::ComparisonRole::HeldKey : static_cast<SoundEngine::ComparisonRole>((i % 7) + 1);
			const SoundEngine::ComparisonRole secondRole = (i % 3 == 0)
				? SoundEngine::ComparisonRole::ProbedItem : SoundEngine::ComparisonRole::None;
			current.setComparison(SoundEngine::AudioComparison(
				static_cast<float>(i) / 11.0f, 1.0f - static_cast<float>(i) / 13.0f, firstRole, secondRole));
			frozen.setComparison(LegacySoundEngine::AudioComparison(
				static_cast<float>(i) / 11.0f, 1.0f - static_cast<float>(i) / 13.0f,
				static_cast<LegacySoundEngine::ComparisonRole>(firstRole),
				static_cast<LegacySoundEngine::ComparisonRole>(secondRole)));
			SoundEngine::EventAccentSummary accents;
			for (std::size_t category = 0; category < SoundEngine::AccentCategoryCount; ++category)
			{
				accents.counts[category] = static_cast<std::uint16_t>((i + category) % 9);
				accents.representativePositions[category] = static_cast<float>((i + category) % 12) / 11.0f;
			}
			current.setEventAccentSummary(accents);
			LegacySoundEngine::EventAccentSummary legacyAccents;
			legacyAccents.counts = accents.counts;
			legacyAccents.representativePositions = accents.representativePositions;
			frozen.setEventAccentSummary(legacyAccents);
			renderAndCompare(1 + (i * 97) % 513, (i % 3 == 0) ? 44100 : ((i % 3 == 1) ? 48000 : 96000),
				"frozen legacy configuration " + std::to_string(i));
			if ((i % 4) == 0)
			{
				current.clearComparison();
				frozen.clearComparison();
				renderAndCompare(257, 44100, "legacy comparison release " + std::to_string(i));
			}
			if ((i % 5) == 0)
			{
				parameters.enabled = false;
				current.setParameters(parameters);
				frozen.setParameters(makeLegacyParameters(parameters));
				renderAndCompare(31, 48000, "legacy disabled output " + std::to_string(i));
				parameters.enabled = true;
				current.setParameters(parameters);
				frozen.setParameters(makeLegacyParameters(parameters));
			}
		}
		current.reset();
		frozen.reset();
		renderAndCompare(1024, 44100, "legacy reset parity");
	}

	SoundEngine::RenderSpec experimentalSpec()
	{
		SoundEngine::RenderSpec spec;
		spec.sampleRate = SampleRate;
		spec.masterGain = 1.0f;
		spec.sourcePeakGain = 1.0f;
		spec.attackMs = 0.0f;
		spec.releaseMs = 0.0f;
		spec.pitchSlewMs = 0.0f;
		spec.minimumFrequencyHz = 50.0f;
		spec.maximumFrequencyHz = 4200.0f;
		return spec;
	}

	void testComposableRegistryAndRealtimeAllocation()
	{
		const std::vector<SoundEngine::GeneratorDescriptor>& sources = SoundEngine::ComponentRegistry::generators();
		const std::vector<SoundEngine::FilterDescriptor>& filters = SoundEngine::ComponentRegistry::filters();
		const std::vector<SoundEngine::OutputProcessorDescriptor>& effects = SoundEngine::ComponentRegistry::outputProcessors();
		require(sources.size() >= 4 && filters.size() >= 3 && effects.size() >= 3,
			"the built-in component catalogs are incomplete");
		require(SoundEngine::ComponentRegistry::findGenerator("continuous_sine") != nullptr &&
			SoundEngine::ComponentRegistry::findGenerator("clocked_sine_packets") != nullptr &&
			SoundEngine::ComponentRegistry::findGenerator("bandlimited_saw_300") != nullptr &&
			SoundEngine::ComponentRegistry::findGenerator(SoundEngine::reconstructedBurstGeneratorId()) != nullptr &&
			SoundEngine::ComponentRegistry::findOutputProcessor(SoundEngine::referenceFirProcessorId()) != nullptr,
			"the experimental source catalog or reconstructed burst/FIR components are not registered");
		SoundEngine::PipelineDescription description;
		SoundEngine::ComposableRenderer renderer;
		std::string error;
		require(renderer.prepare(experimentalSpec(), description, &error), "basic composable pipeline failed preparation: " + error);
		renderer.setVoiceControl(0, SoundEngine::VoiceControl(1150.0f, 0.8f, true));
		allocationCount.store(0, std::memory_order_relaxed);
		countAllocations.store(true, std::memory_order_relaxed);
		float samples[4096 * 2];
		renderer.render(samples, 4096);
		countAllocations.store(false, std::memory_order_relaxed);
		require(allocationCount.load(std::memory_order_relaxed) == 0,
			"prepared composable render allocated memory on the render path");
		require(renderer.sampleClock() == 4096, "the persistent sample clock did not advance by the rendered frame count");

		SoundEngine::PipelineDescription reconstructed;
		for (std::size_t voice = 0; voice < 2; ++voice)
		{
			reconstructed.voices[voice].source.id = SoundEngine::reconstructedBurstGeneratorId();
			reconstructed.voices[voice].source.packetClockHz = 300.0f;
			reconstructed.voices[voice].source.carrierCycles = 9.3026503f;
			reconstructed.voices[voice].source.tukeyFraction = 0.02566677f;
		}
		reconstructed.outputProcessorCount = 1;
		reconstructed.outputProcessors[0].id = SoundEngine::referenceFirProcessorId();
		reconstructed.outputProcessors[0].mix = 1.0f;
		SoundEngine::ComposableRenderer burstPipeline;
		require(burstPipeline.prepare(experimentalSpec(), reconstructed, &error),
			"registered reconstructed burst/FIR pipeline failed preparation: " + error);
		burstPipeline.setVoiceControl(0, SoundEngine::VoiceControl(1150.0f, 1.0f, true));
		burstPipeline.setVoiceControl(1, SoundEngine::VoiceControl(2700.0f, 1.0f, true));
		float reconstructedSamples[2048 * 2] = {};
		allocationCount.store(0, std::memory_order_relaxed);
		countAllocations.store(true, std::memory_order_relaxed);
		burstPipeline.render(reconstructedSamples, 2048);
		countAllocations.store(false, std::memory_order_relaxed);
		require(allocationCount.load(std::memory_order_relaxed) == 0 && burstPipeline.sampleClock() == 2048,
			"registered delayed-burst and reference-FIR components process prepared audio without allocation");
		double reconstructedPeak = 0.0;
		for (std::size_t i = 0; i < 2048 * 2; ++i)
			reconstructedPeak = (std::max)(reconstructedPeak, std::fabs(static_cast<double>(reconstructedSamples[i])));
		require(reconstructedPeak > 1.0e-5,
			"registered delayed-burst/FIR components produce the captured-pitch comparison sound");
	}

	void testContinuousSineAndBlockPartitionContinuity()
	{
		SoundEngine::PipelineDescription description;
		SoundEngine::ComposableRenderer contiguous, partitioned;
		const SoundEngine::RenderSpec spec = experimentalSpec();
		std::string error;
		require(contiguous.prepare(spec, description, &error), "continuous sine pipeline preparation failed: " + error);
		require(partitioned.prepare(spec, description, &error), "partition continuity pipeline preparation failed: " + error);
		const SoundEngine::VoiceControl control(1150.0f, 1.0f, true);
		contiguous.setVoiceControl(0, control);
		partitioned.setVoiceControl(0, control);
		std::vector<float> whole(44100 * 2), pieces(whole.size());
		contiguous.render(whole.data(), 44100);
		partitioned.render(pieces.data(), 123);
		partitioned.render(pieces.data() + 123 * 2, 877);
		partitioned.render(pieces.data() + 1000 * 2, 431);
		partitioned.render(pieces.data() + 1431 * 2, 42669);
		requirePcmEqual(whole, pieces, "continuous sine irregular block partition");
		const double measured = sineAmplitude(whole, 5000, 30000, 1150.0);
		require(near(measured, 0.70710678, 0.002), "the continuous oscillator missed its off-grid 1,150 Hz pitch");
		contiguous.reset();
		partitioned.reset();
		contiguous.setVoiceControl(0, SoundEngine::VoiceControl(850.0f, 0.9f, true));
		partitioned.setVoiceControl(0, SoundEngine::VoiceControl(850.0f, 0.9f, true));
		whole.assign(4000 * 2, 0.0f); pieces.assign(whole.size(), 0.0f);
		contiguous.render(whole.data(), 1000);
		partitioned.render(pieces.data(), 211);
		partitioned.render(pieces.data() + 422, 789);
		contiguous.setMasterGain(0.27f); partitioned.setMasterGain(0.27f);
		contiguous.setSourcePeakGain(0.61f); partitioned.setSourcePeakGain(0.61f);
		contiguous.render(whole.data() + 2000, 3000);
		partitioned.render(pieces.data() + 2000, 37);
		partitioned.render(pieces.data() + 2074, 1024);
		partitioned.render(pieces.data() + 4122, 1939);
		requirePcmEqual(whole, pieces, "smoothed parameter changes across irregular blocks");
		const double lowFrequency = 73.0;
		contiguous.reset();
		contiguous.setMasterGain(1.0f);
		contiguous.setSourcePeakGain(1.0f);
		contiguous.setVoiceControl(0, SoundEngine::VoiceControl(static_cast<float>(lowFrequency), 1.0f, true));
		std::vector<float> low(22050 * 2);
		contiguous.render(low.data(), 22050);
		require(sineAmplitude(low, 5000, 16000, lowFrequency) > 0.70,
			"the continuous oscillator did not support an off-grid sub-300 Hz voice");
	}

	void testClockedPacketsAndSawSource()
	{
		SoundEngine::PipelineDescription packetDescription;
		packetDescription.voices[0].source.id = SoundEngine::clockedSinePacketGeneratorId();
		packetDescription.voices[0].source.durationMode = SoundEngine::PacketDuration::FixedSeconds;
		packetDescription.voices[0].source.packetDurationSeconds = 2.0f / 300.0f;
		packetDescription.voices[0].source.window = SoundEngine::PacketWindow::Gaussian;
		packetDescription.voices[0].source.phase = SoundEngine::PacketPhase::ResetAtBirth;
		SoundEngine::ComposableRenderer packetA, packetB;
		std::string error;
		require(packetA.prepare(experimentalSpec(), packetDescription, &error), "clocked packet pipeline preparation failed: " + error);
		require(packetB.prepare(experimentalSpec(), packetDescription, &error), "clocked packet partition preparation failed: " + error);
		packetA.setVoiceControl(0, SoundEngine::VoiceControl(1150.0f, 1.0f, true));
		packetB.setVoiceControl(0, SoundEngine::VoiceControl(1150.0f, 1.0f, true));
		std::vector<float> packets(22050 * 2), split(packets.size());
		packetA.render(packets.data(), 22050);
		packetB.render(split.data(), 17);
		packetB.render(split.data() + 34, 997);
		packetB.render(split.data() + 2028, 5000);
		packetB.render(split.data() + 12028, 16036);
		requirePcmEqual(packets, split, "clocked sine packet partition");
		require(maxAbs(packets) > 0.01, "the packet generator produced no sound");
		packetA.setVoiceControl(0, SoundEngine::VoiceControl(1150.0f, 1.0f, false));
		std::vector<float> silence(1000 * 2);
		packetA.render(silence.data(), 1000);
		for (std::size_t i = 0; i < silence.size(); ++i)
			require(silence[i] == 0.0f, "cleared packet focus continued producing gated output");

		SoundEngine::PipelineDescription sawDescription;
		sawDescription.voices[0].source.id = SoundEngine::bandlimitedSaw300GeneratorId();
		SoundEngine::ComposableRenderer saw;
		require(saw.prepare(experimentalSpec(), sawDescription, &error), "band-limited saw pipeline preparation failed: " + error);
		saw.setVoiceControl(0, SoundEngine::VoiceControl(1200.0f, 1.0f, true));
		std::vector<float> sawSamples(44100 * 2);
		saw.render(sawSamples.data(), 44100);
		const double fundamental = sineAmplitude(sawSamples, 5000, 30000, 300.0);
		const double second = sineAmplitude(sawSamples, 5000, 30000, 600.0);
		require(fundamental > 0.44 && near(second / fundamental, 0.5, 0.025),
			"the 300 Hz saw Fourier partials did not follow the 1/k amplitude series: fundamental=" +
			std::to_string(fundamental) + ", second=" + std::to_string(second));
	}

	void testMovingFilterAndBedLane()
	{
		SoundEngine::PipelineDescription description;
		description.voices[0].source.id = SoundEngine::bandlimitedSaw300GeneratorId();
		description.voices[0].filterCount = 2;
		description.voices[0].filters[0].id = SoundEngine::tptBandPassFilterId();
		description.voices[0].filters[0].q = 4.0f;
		description.voices[0].filters[0].constantQ = true;
		description.voices[0].filters[1].id = SoundEngine::butterworthLowPassFilterId();
		description.voices[0].filters[1].cutoffHz = 10000.0f;
		description.bedEnabled = true;
		description.bed.source.id = SoundEngine::continuousSineGeneratorId();
		description.bedGain = 0.1f;
		SoundEngine::ComposableRenderer renderer;
		std::string error;
		require(renderer.prepare(experimentalSpec(), description, &error), "filtered lane pipeline preparation failed: " + error);
		renderer.setVoiceControl(0, SoundEngine::VoiceControl(1800.0f, 1.0f, true));
		renderer.setBedControl(SoundEngine::VoiceControl(300.0f, 1.0f, true));
		std::vector<float> output(44100 * 2);
		renderer.render(output.data(), 44100);
		require(maxAbs(output) > 0.001, "filter chain and independent bed produced silence");
		for (std::size_t i = 0; i < output.size(); ++i)
			require(std::isfinite(output[i]), "moving filter became numerically unstable");

		SoundEngine::PipelineDescription bandPass;
		bandPass.voices[0].source.id = SoundEngine::bandlimitedSaw300GeneratorId();
		bandPass.voices[0].filterCount = 1;
		bandPass.voices[0].filters[0].id = SoundEngine::tptBandPassFilterId();
		bandPass.voices[0].filters[0].cutoffHz = 1000.0f;
		bandPass.voices[0].filters[0].q = 4.0f;
		SoundEngine::ComposableRenderer selected;
		require(selected.prepare(experimentalSpec(), bandPass, &error), "band-pass response pipeline preparation failed: " + error);
		selected.setVoiceControl(0, SoundEngine::VoiceControl(1800.0f, 1.0f, true));
		std::vector<float> filtered(44100 * 2), repeated(filtered.size());
		selected.render(filtered.data(), 44100);
		const double atCenter = sineAmplitude(filtered, 5000, 30000, 1800.0);
		const double lowPartial = sineAmplitude(filtered, 5000, 30000, 300.0);
		require(atCenter > lowPartial * 2.0, "TPT band-pass did not emphasize its moving center frequency");
		selected.reset();
		selected.setVoiceControl(0, SoundEngine::VoiceControl(1800.0f, 1.0f, true));
		selected.render(repeated.data(), 44100);
		requirePcmEqual(filtered, repeated, "composable reset did not restore source and filter state");

		SoundEngine::PipelineDescription lowPass;
		lowPass.voices[0].source.id = SoundEngine::bandlimitedSaw300GeneratorId();
		lowPass.voices[0].filterCount = 1;
		lowPass.voices[0].filters[0].id = SoundEngine::butterworthLowPassFilterId();
		lowPass.voices[0].filters[0].cutoffHz = 2000.0f;
		SoundEngine::ComposableRenderer lowPassed;
		require(lowPassed.prepare(experimentalSpec(), lowPass, &error), "low-pass response pipeline preparation failed: " + error);
		lowPassed.setVoiceControl(0, SoundEngine::VoiceControl(1000.0f, 1.0f, true));
		std::vector<float> lowPassSamples(44100 * 2);
		lowPassed.render(lowPassSamples.data(), 44100);
		const double lowPassFundamental = sineAmplitude(lowPassSamples, 5000, 30000, 300.0);
		const double highPartial = sineAmplitude(lowPassSamples, 5000, 30000, 6000.0);
		require(highPartial < lowPassFundamental * 0.02, "two-pole Butterworth low-pass did not attenuate the upper saw partials");
	}

	void testCombFilterBedShaping()
	{
		SoundEngine::BedCombFilterParameters defaults;
		require(!defaults.enabled, "experimental bed comb should be opt-in");
		require(near(SoundEngine::bedCombMagnitude(2100.0, defaults), 1.0, 1e-7),
			"a disabled bed comb should have unity response");

		SoundEngine::BedCombFilterParameters comb = defaults;
		comb.enabled = true;
		require(near(SoundEngine::bedCombMagnitude(150.0, comb), 1.0, 1e-7),
			"comb shaping must leave sub-300 Hz comparison components untouched");
		require(SoundEngine::bedCombMagnitude(2100.0, comb) < 0.06f,
			"the fitted 2.1 kHz comb null was not deep enough");
		require(SoundEngine::bedCombMagnitude(6600.0, comb) < 0.06f,
			"the fitted 6.6 kHz comb null was not deep enough");
		require(near(SoundEngine::bedCombMagnitude(11100.0, comb), 0.05625, 0.002),
			"the high-frequency fade did not produce the expected 11.1 kHz comb depth");
		require(near(SoundEngine::bedCombMagnitude(15600.0, comb), 0.10, 0.002),
			"the high-frequency comb tail did not fade to the expected 15.6 kHz depth");
		require(SoundEngine::bedCombMagnitude(2400.0, comb) > SoundEngine::bedCombMagnitude(2100.0, comb),
			"the response did not rise away from the 2.1 kHz null");

		SoundEngine::Synthesizer flat;
		SoundEngine::Synthesizer filtered;
		SoundEngine::Parameters flatParameters = flat.parameters();
		flatParameters.masterLevel = 0.1f;
		flatParameters.bedLevel = 0.15f;
		flatParameters.bedComb.enabled = false;
		flatParameters.bedComb.applyPhase = false;
		flatParameters.comparisonBoostDb = 0.0f;
		flatParameters.heldKeyBoostDb = 0.0f;
		flatParameters.attackMs = 1.0f;
		flatParameters.releaseMs = 1.0f;
		SoundEngine::Parameters filteredParameters = flatParameters;
		filteredParameters.bedComb.enabled = true;
		flat.setParameters(flatParameters);
		filtered.setParameters(filteredParameters);
		flat.setTraceLoaded(true);
		filtered.setTraceLoaded(true);
		std::vector<float> flatSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> filteredSamples(flatSamples.size());
		flat.render(flatSamples.data(), flatSamples.size() / 2, SampleRate);
		filtered.render(filteredSamples.data(), filteredSamples.size() / 2, SampleRate);
		const std::size_t start = SampleRate;
		const std::size_t frames = SampleRate;
		const double flat2100 = sineAmplitude(flatSamples, start, frames, 2100.0);
		const double filtered2100 = sineAmplitude(filteredSamples, start, frames, 2100.0);
		const double flat2400 = sineAmplitude(flatSamples, start, frames, 2400.0);
		const double filtered2400 = sineAmplitude(filteredSamples, start, frames, 2400.0);
		require(flat2100 > 0.0 && near(filtered2100 / flat2100, SoundEngine::bedCombMagnitude(2100.0, comb), 0.003),
			"rendered bed did not match the precomputed 2.1 kHz comb response");
		require(flat2400 > 0.0 && near(filtered2400 / flat2400, SoundEngine::bedCombMagnitude(2400.0, comb), 0.003),
			"rendered bed did not match the precomputed off-null comb response");

		SoundEngine::Synthesizer unfilteredComparison;
		SoundEngine::Synthesizer combComparison;
		flatParameters.bedLevel = 0.0f;
		filteredParameters = flatParameters;
		filteredParameters.bedComb.enabled = true;
		unfilteredComparison.setParameters(flatParameters);
		combComparison.setParameters(filteredParameters);
		unfilteredComparison.setTraceLoaded(true);
		combComparison.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.23f, 0.67f, SoundEngine::HeldKeyParticipant::Second);
		unfilteredComparison.setComparison(comparison);
		combComparison.setComparison(comparison);
		std::vector<float> bedFade(static_cast<std::size_t>(SampleRate) * 2 * 2);
		unfilteredComparison.render(bedFade.data(), bedFade.size() / 2, SampleRate);
		combComparison.render(bedFade.data(), bedFade.size() / 2, SampleRate);
		std::vector<float> comparisonA(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> comparisonB(comparisonA.size());
		unfilteredComparison.render(comparisonA.data(), comparisonA.size() / 2, SampleRate);
		combComparison.render(comparisonB.data(), comparisonB.size() / 2, SampleRate);
		double comparisonDifference = 0.0;
		for (std::size_t i = 0; i < comparisonA.size(); ++i)
			comparisonDifference = std::max(comparisonDifference,
				std::fabs(static_cast<double>(comparisonA[i] - comparisonB[i])));
		require(comparisonDifference < 1e-7,
			"bed comb shaping changed comparison tones when the bed was silent (max delta=" +
			std::to_string(comparisonDifference) + ")");
	}

	void testPhaseAwareBedAndHarmonicRolloff()
	{
		SoundEngine::BedCombFilterParameters comb;
		comb.enabled = true;
		require(near(SoundEngine::bedCombPhase(150.0, comb), 0.0, 1e-8),
			"comb phase must leave sub-300 Hz comparison bands untouched");
		require(near(SoundEngine::bedCombPhase(2100.0, comb), 0.0, 1e-8),
			"magnitude-only comb mode unexpectedly changed partial phase");
		comb.applyPhase = true;
		require(std::fabs(SoundEngine::bedCombPhase(1500.0, comb)) > 0.1f,
			"phase-aware comb mode did not produce a phase rotation");

		SoundEngine::Synthesizer magnitudeOnly;
		SoundEngine::Synthesizer complexResponse;
		SoundEngine::Parameters magnitudeParameters = magnitudeOnly.parameters();
		magnitudeParameters.masterLevel = 0.1f;
		magnitudeParameters.bedLevel = 0.15f;
		magnitudeParameters.bedComb.enabled = true;
		magnitudeParameters.bedComb.applyPhase = false;
		magnitudeParameters.attackMs = 1.0f;
		magnitudeParameters.releaseMs = 1.0f;
		SoundEngine::Parameters phaseParameters = magnitudeParameters;
		phaseParameters.bedComb.applyPhase = true;
		magnitudeOnly.setParameters(magnitudeParameters);
		complexResponse.setParameters(phaseParameters);
		magnitudeOnly.setTraceLoaded(true);
		complexResponse.setTraceLoaded(true);
		std::vector<float> magnitudeSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> phaseSamples(magnitudeSamples.size());
		magnitudeOnly.render(magnitudeSamples.data(), magnitudeSamples.size() / 2, SampleRate);
		complexResponse.render(phaseSamples.data(), phaseSamples.size() / 2, SampleRate);
		const std::size_t start = SampleRate;
		const std::size_t frames = SampleRate;
		const double magnitude2100 = sineAmplitude(magnitudeSamples, start, frames, 2100.0);
		const double phase2100 = sineAmplitude(phaseSamples, start, frames, 2100.0);
		require(near(phase2100 / magnitude2100, 1.0, 0.003),
			"phase-aware comb changed the fitted magnitude response");
		double maxPhaseDifference = 0.0;
		for (std::size_t i = 0; i < magnitudeSamples.size(); ++i)
			maxPhaseDifference = std::max(maxPhaseDifference,
				std::fabs(static_cast<double>(magnitudeSamples[i] - phaseSamples[i])));
		require(maxPhaseDifference > 0.005,
			"phase-aware comb did not alter the bed's time-domain waveform");

		SoundEngine::Synthesizer fundamentalBed;
		SoundEngine::Synthesizer rolledBed;
		SoundEngine::Parameters base = fundamentalBed.parameters();
		base.masterLevel = 0.1f;
		base.bedLevel = 0.15f;
		base.bedHarmonicExponent = 1.0f;
		base.attackMs = 1.0f;
		base.releaseMs = 1.0f;
		SoundEngine::Parameters rolled = base;
		rolled.bedHarmonicExponent = 1.5f;
		fundamentalBed.setParameters(base);
		rolledBed.setParameters(rolled);
		fundamentalBed.setTraceLoaded(true);
		rolledBed.setTraceLoaded(true);
		std::vector<float> baseSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> rolledSamples(baseSamples.size());
		fundamentalBed.render(baseSamples.data(), baseSamples.size() / 2, SampleRate);
		rolledBed.render(rolledSamples.data(), rolledSamples.size() / 2, SampleRate);
		const double base300 = sineAmplitude(baseSamples, start, frames, 300.0);
		const double base1200 = sineAmplitude(baseSamples, start, frames, 1200.0);
		const double rolled300 = sineAmplitude(rolledSamples, start, frames, 300.0);
		const double rolled1200 = sineAmplitude(rolledSamples, start, frames, 1200.0);
		require(near(rolled300 / base300, 1.0, 0.003), "harmonic rolloff changed the 300 Hz fundamental");
		require(near((rolled1200 / base1200), 0.5, 0.004),
			"p=1.5 bed rolloff did not attenuate the fourth harmonic by 1/sqrt(4)");
	}

	void testPositionScaledComparisonSpread()
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters parameters = synthesizer.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		parameters.comparisonSpreadMode = SoundEngine::ComparisonSpreadMode::PositionScaled;
		parameters.attackMs = 1.0f;
		synthesizer.setParameters(parameters);
		synthesizer.setTraceLoaded(true);

		const SoundEngine::AudioComparison low(0.0f, 0.0f, SoundEngine::HeldKeyParticipant::None);
		synthesizer.setComparison(low);
		std::vector<float> lowSamples(static_cast<std::size_t>(SampleRate) * 2);
		synthesizer.render(lowSamples.data(), SampleRate, SampleRate);
		const double lowCenter = normalizedBandAmplitude(lowSamples, SampleRate / 2, SampleRate / 2, 0);
		const double lowNeighbor = normalizedBandAmplitude(lowSamples, SampleRate / 2, SampleRate / 2, 1);
		require(near(lowNeighbor / lowCenter, std::exp(-10.0), 2e-5),
			"low-value position-scaled spread did not use tau=0.1 bins");

		const SoundEngine::AudioComparison high(1.0f, 1.0f, SoundEngine::HeldKeyParticipant::None);
		synthesizer.setComparison(high);
		std::vector<float> highSamples(static_cast<std::size_t>(SampleRate) * 2);
		synthesizer.render(highSamples.data(), SampleRate, SampleRate);
		const double highCenter = normalizedBandAmplitude(highSamples, SampleRate / 2, SampleRate / 2, 16);
		const double highNeighbor = normalizedBandAmplitude(highSamples, SampleRate / 2, SampleRate / 2, 17);
		require(near(highNeighbor / highCenter, std::exp(-2.0), 0.01),
			"high-value position-scaled spread did not widen to tau=0.5 bins");
	}

	void testMasterGainRemainsLinearUnderHeadroomProtection()
	{
		SoundEngine::Synthesizer full;
		SoundEngine::Synthesizer reduced;
		SoundEngine::Parameters fullParameters = full.parameters();
		fullParameters.masterLevel = 0.5f;
		fullParameters.bedLevel = 1.0f;
		fullParameters.comparisonBoostDb = 18.0f;
		fullParameters.heldKeyBoostDb = 18.0f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			fullParameters.bandTrimDb[i] = 12.0f;
		SoundEngine::Parameters reducedParameters = fullParameters;
		reducedParameters.masterLevel = 0.375f;
		full.setParameters(fullParameters);
		reduced.setParameters(reducedParameters);
		full.setTraceLoaded(true);
		reduced.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.37f, 0.84f, SoundEngine::HeldKeyParticipant::Second);
		full.setComparison(comparison);
		reduced.setComparison(comparison);
		std::vector<float> warm(static_cast<std::size_t>(SampleRate) * 2 * 2);
		full.render(warm.data(), warm.size() / 2, SampleRate);
		reduced.render(warm.data(), warm.size() / 2, SampleRate);
		std::vector<float> fullOutput(static_cast<std::size_t>(SampleRate) * 2 / 2);
		std::vector<float> reducedOutput(fullOutput.size());
		full.render(fullOutput.data(), fullOutput.size() / 2, SampleRate);
		reduced.render(reducedOutput.data(), reducedOutput.size() / 2, SampleRate);
		double fullPower = 0.0;
		double reducedPower = 0.0;
		for (std::size_t i = 0; i < fullOutput.size(); ++i)
		{
			fullPower += static_cast<double>(fullOutput[i]) * fullOutput[i];
			reducedPower += static_cast<double>(reducedOutput[i]) * reducedOutput[i];
		}
		const double measuredGainRatio = std::sqrt(reducedPower / fullPower);
		require(near(measuredGainRatio, 0.75, 1e-3),
			"master gain was compensated away by the headroom limiter");
	}

	void testFrequencyBankAndMapping()
	{
		const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount>& bank = SoundEngine::frequencyBank();
		require(SoundEngine::BandCount == 60 && SoundEngine::FocusBandCount == 17, "band or focus-band count changed unexpectedly");
		require(bank[0].frequencyHz == 50.0 && bank[1].frequencyHz == 100.0 && bank[2].frequencyHz == 150.0,
			"low-frequency bank centers are incorrect");
		require(bank[0].frequencyHz != 75.0, "the old 75 Hz bin remains in the bank");
		for (std::size_t harmonic = 1; harmonic <= 57; ++harmonic)
		{
			const std::size_t index = harmonic + 2;
			require(bank[index].frequencyHz == 300.0 * harmonic, "harmonic bank has an unexpected center");
			require(near(bank[index].baseAmplitude, 1.0 / harmonic, 1e-6), "harmonic base amplitude is not 1/k");
		}
		require(bank[SoundEngine::BandCount - 1].frequencyHz == 17100.0, "upper super-harmonic endpoint is incorrect");
		require(near(bank[0].baseAmplitude, std::pow(10.0, -12.0 / 20.0), 1e-6), "low-band profile is not -12 dB");
		require(near(bank[3].baseAmplitude, 1.0, 1e-6), "300 Hz fundamental amplitude should be unity");
		require(near(bank[4].baseAmplitude, 0.5, 1e-6), "second harmonic amplitude should be one half");

		const SoundEngine::MappingMode modes[] = {
			SoundEngine::MappingMode::LinearHz,
			SoundEngine::MappingMode::LogFrequency,
			SoundEngine::MappingMode::EqualBandSlots
		};
		for (std::size_t i = 0; i < 3; ++i)
		{
			const SoundEngine::BandBlend first = SoundEngine::mapNormalizedPosition(0.0f, modes[i]);
			const SoundEngine::BandBlend last = SoundEngine::mapNormalizedPosition(1.0f, modes[i]);
			require(first.lowerIndex == 0 && first.upperIndex == 0 && first.frequencyHz == 50.0, "mapping start endpoint is incorrect");
			require(last.lowerIndex == SoundEngine::FocusBandCount - 1 && last.upperIndex == last.lowerIndex && last.frequencyHz == 4200.0,
				"mapping end endpoint is incorrect");
			for (int step = 0; step <= 100; ++step)
			{
				const SoundEngine::BandBlend mapped = SoundEngine::mapNormalizedPosition(static_cast<float>(step) / 100.0f, modes[i]);
				require(mapped.frequencyHz >= 50.0 && mapped.frequencyHz <= 4200.0, "mapping focus escaped the 50 Hz–4.2 kHz range");
				require(mapped.lowerIndex < SoundEngine::FocusBandCount && mapped.upperIndex < SoundEngine::FocusBandCount,
					"mapping selected a super-harmonic as a focus center");
			}
		}

		const SoundEngine::BandBlend linearMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::LinearHz);
		require(near(linearMid.frequencyHz, 2125.0, 1e-8), "linear-Hz midpoint is incorrect");
		require(linearMid.lowerIndex == 9 && linearMid.upperIndex == 10 && near(linearMid.upperWeight, 1.0 / 12.0, 1e-6), "linear-Hz midpoint blend is incorrect");
		const SoundEngine::BandBlend logMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::LogFrequency);
		require(near(logMid.frequencyHz, std::sqrt(50.0 * 4200.0), 1e-8) && logMid.lowerIndex == 3 && logMid.upperIndex == 4, "log-frequency midpoint is incorrect");
		const SoundEngine::BandBlend slotMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::EqualBandSlots);
		require(near(slotMid.frequencyHz, 1800.0, 1e-8) && slotMid.lowerIndex == 8 && slotMid.upperIndex == 8 && near(slotMid.upperWeight, 0.0, 1e-8), "equal-slot midpoint is incorrect");

		require(near(SoundEngine::normalizeValue(5.0f, 5.0f, 5.0f), 0.5, 1e-7), "zero-width ranges should map to the midpoint");
		require(near(SoundEngine::normalizeValue(0.0f, 0.0f, 10.0f), 0.0, 1e-7), "minimum value did not normalize to zero");
		require(near(SoundEngine::normalizeValue(10.0f, 0.0f, 10.0f), 1.0, 1e-7), "maximum value did not normalize to one");
		require(near(SoundEngine::normalizeValue(7.5f, 10.0f, 0.0f), 0.25, 1e-7), "reversed value ranges were not normalized consistently");
	}

	std::vector<float> renderComparison(SoundEngine::HeldKeyParticipant held)
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters parameters = synthesizer.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.attackMs = 10.0f;
		parameters.tauBelowBins = 0.25f;
		parameters.tauAboveBins = 0.25f;
		synthesizer.setParameters(parameters);
		synthesizer.setTraceLoaded(true);
		synthesizer.setComparison(SoundEngine::AudioComparison(
			SoundEngine::normalizeValue(300.0f, 50.0f, 4200.0f),
			SoundEngine::normalizeValue(600.0f, 50.0f, 4200.0f), held));
		std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		synthesizer.render(samples.data(), samples.size() / 2, SampleRate);
		return samples;
	}

	void testHeldKeyWeighting()
	{
		const std::vector<float> equal = renderComparison(SoundEngine::HeldKeyParticipant::None);
		const std::vector<float> held = renderComparison(SoundEngine::HeldKeyParticipant::Second);
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 2;
		const std::size_t frames = SampleRate;
		const double equal300 = sineAmplitude(equal, start, frames, 300.0);
		const double equal600 = sineAmplitude(equal, start, frames, 600.0);
		const double held300 = sineAmplitude(held, start, frames, 300.0);
		const double held600 = sineAmplitude(held, start, frames, 600.0);
		require(equal300 > 0.01 && equal600 > 0.001, "comparison did not produce the expected test tones");
		require(near((held600 / held300) / (equal600 / equal300), SoundEngine::decibelsToGain(6.0), 0.06), "held participant was not boosted by approximately +6 dB");
	}

	void testGeneratedCarrierCenters()
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters bedParameters = synthesizer.parameters();
		bedParameters.bedComb.enabled = false;
		bedParameters.bedComb.applyPhase = false;
		synthesizer.setParameters(bedParameters);
		synthesizer.setTraceLoaded(true);
		std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2 * 6);
		synthesizer.render(samples.data(), samples.size() / 2, SampleRate);
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 4;
		const std::size_t frames = static_cast<std::size_t>(SampleRate) * 2;
		const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount>& bank = SoundEngine::frequencyBank();
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
		{
			const double amplitude = sineAmplitude(samples, start, frames, bank[i].frequencyHz);
			if (bank[i].frequencyHz < 300.0)
				require(amplitude < 1e-5, "a sub-300 Hz component leaked into the default bed");
			else
				require(amplitude > 1e-4, "a 300 Hz-or-higher bed carrier was missing");
		}
		SoundEngine::Synthesizer lowComparison;
		lowComparison.setTraceLoaded(true);
		lowComparison.setComparison(SoundEngine::AudioComparison(0.0f, 0.0f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> lowSamples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		lowComparison.render(lowSamples.data(), lowSamples.size() / 2, SampleRate);
		require(sineAmplitude(lowSamples, SampleRate * 2, SampleRate, 50.0) > 1e-4,
			"the muted low-frequency bed also muted a low-valued comparison");
		SoundEngine::Synthesizer lowBed;
		SoundEngine::Parameters lowBedParameters = lowBed.parameters();
		lowBedParameters.includeSub300HzBed = true;
		lowBed.setParameters(lowBedParameters);
		lowBed.setTraceLoaded(true);
		std::vector<float> lowBedSamples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		lowBed.render(lowBedSamples.data(), lowBedSamples.size() / 2, SampleRate);
		require(sineAmplitude(lowBedSamples, SampleRate * 2, SampleRate, 50.0) > 1e-4,
			"the optional sub-300 Hz bed setting did not restore the low bands");
		require(sineAmplitude(samples, start, frames, 4500.0) > 1e-4, "4,500 Hz super-harmonic was missing from the bed");
		require(sineAmplitude(samples, start, frames, 17100.0) > 1e-4, "17,100 Hz super-harmonic was missing from the bed");
		require(sineAmplitude(samples, start, frames, 17400.0) < 1e-5, "unexpected 17,400 Hz carrier was generated");
		require(sineAmplitude(samples, start, frames, 18000.0) < 1e-5, "unexpected 18,000 Hz carrier was generated");
	}

	void testExponentialComparisonProfiles()
	{
		struct Profile
		{
			float below;
			float above;
		};
		const Profile profiles[] = {
			{ 0.25f, 0.25f }, { 0.5f, 0.5f }, { 0.75f, 0.75f },
			{ 0.25f, 0.75f }
		};
		const std::size_t centerIndex = 12; // 3,000 Hz, an exact equal-slot focus center.
		const std::size_t lowerIndex = centerIndex - 1;
		const std::size_t upperIndex = centerIndex + 1;

		for (std::size_t profile = 0; profile < sizeof(profiles) / sizeof(profiles[0]); ++profile)
		{
			SoundEngine::Synthesizer synthesizer;
			SoundEngine::Parameters parameters = synthesizer.parameters();
			parameters.masterLevel = 0.1f;
			parameters.bedLevel = 0.0f;
			parameters.comparisonBoostDb = 0.0f;
			parameters.heldKeyBoostDb = 0.0f;
			parameters.attackMs = 1.0f;
			parameters.releaseMs = 1.0f;
			parameters.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
			parameters.tauBelowBins = profiles[profile].below;
			parameters.tauAboveBins = profiles[profile].above;
			synthesizer.setParameters(parameters);
			synthesizer.setTraceLoaded(true);
			synthesizer.setComparison(SoundEngine::AudioComparison(0.75f, 0.75f, SoundEngine::HeldKeyParticipant::None));

			std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2);
			synthesizer.render(samples.data(), SampleRate, SampleRate);
			const std::size_t start = SampleRate / 2;
			const std::size_t frames = SampleRate / 2;
			const double center = normalizedBandAmplitude(samples, start, frames, centerIndex);
			const double lower = normalizedBandAmplitude(samples, start, frames, lowerIndex) / center;
			const double upper = normalizedBandAmplitude(samples, start, frames, upperIndex) / center;
			require(center > 0.01, "comparison envelope did not peak at its mapped band");
			require(near(lower, std::exp(-1.0 / profiles[profile].below), 0.025), "lower-side exponential envelope does not match its tau");
			require(near(upper, std::exp(-1.0 / profiles[profile].above), 0.025), "upper-side exponential envelope does not match its tau");
		}

		SoundEngine::Synthesizer upperTail;
		SoundEngine::Parameters parameters = upperTail.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.attackMs = 1.0f;
		parameters.mappingMode = SoundEngine::MappingMode::LinearHz;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		upperTail.setParameters(parameters);
		upperTail.setTraceLoaded(true);
		upperTail.setComparison(SoundEngine::AudioComparison(1.0f, 1.0f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> tailSamples(static_cast<std::size_t>(SampleRate) * 2);
		upperTail.render(tailSamples.data(), SampleRate, SampleRate);
		const std::size_t tailStart = SampleRate / 2;
		const std::size_t tailFrames = SampleRate / 2;
		const double peak = normalizedBandAmplitude(tailSamples, tailStart, tailFrames, 16);
		const double superHarmonic = normalizedBandAmplitude(tailSamples, tailStart, tailFrames, 17);
		require(peak > 0.01 && near(superHarmonic / peak, std::exp(-1.0 / 0.5), 0.025),
			"focus at 4,200 Hz did not produce the expected 4,500 Hz tail");

		SoundEngine::Synthesizer betweenBands;
		parameters = betweenBands.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		betweenBands.setParameters(parameters);
		betweenBands.setTraceLoaded(true);
		const float halfwayPosition = 12.5f / static_cast<float>(SoundEngine::FocusBandCount - 1);
		betweenBands.setComparison(SoundEngine::AudioComparison(halfwayPosition, halfwayPosition, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> halfwaySamples(static_cast<std::size_t>(SampleRate) * 2);
		betweenBands.render(halfwaySamples.data(), SampleRate, SampleRate);
		const double lowerPeak = normalizedBandAmplitude(halfwaySamples, tailStart, tailFrames, 12);
		const double upperPeak = normalizedBandAmplitude(halfwaySamples, tailStart, tailFrames, 13);
		require(lowerPeak > 0.01 && near(lowerPeak, upperPeak, 0.025),
			"an inter-band focus did not share its peak smoothly between the nearest bins");
	}

	void testPhaseContinuityAndSmoothing()
	{
		SoundEngine::Synthesizer whole;
		SoundEngine::Synthesizer chunked;
		whole.setTraceLoaded(true);
		chunked.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.22f, 0.83f, SoundEngine::HeldKeyParticipant::Second);
		whole.setComparison(comparison);
		chunked.setComparison(comparison);
		const std::size_t totalFrames = 12000;
		std::vector<float> wholeOutput(totalFrames * 2);
		std::vector<float> chunkedOutput(totalFrames * 2);
		whole.render(wholeOutput.data(), totalFrames, SampleRate);
		chunked.render(chunkedOutput.data(), 3500, SampleRate);
		chunked.render(chunkedOutput.data() + 3500 * 2, totalFrames - 3500, SampleRate);
		require(wholeOutput == chunkedOutput, "render block boundary changed phase or envelope continuity");

		SoundEngine::Synthesizer attack;
		SoundEngine::Synthesizer withoutComparison;
		SoundEngine::Parameters attackParameters = attack.parameters();
		attackParameters.masterLevel = 0.1f;
		attackParameters.tauBelowBins = 0.25f;
		attackParameters.tauAboveBins = 0.25f;
		attack.setParameters(attackParameters);
		withoutComparison.setParameters(attackParameters);
		attack.setTraceLoaded(true);
		withoutComparison.setTraceLoaded(true);
		std::vector<float> warm(static_cast<std::size_t>(SampleRate) * 2);
		attack.render(warm.data(), SampleRate, SampleRate);
		withoutComparison.render(warm.data(), SampleRate, SampleRate);
		attack.setComparison(SoundEngine::AudioComparison(0.18f, 0.69f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> onset(16000 * 2);
		std::vector<float> baseline(16000 * 2);
		attack.render(onset.data(), 16000, SampleRate);
		withoutComparison.render(baseline.data(), 16000, SampleRate);
		double earlyEnergy = 0.0;
		double settledEnergy = 0.0;
		for (std::size_t i = 0; i < 300; ++i)
		{
			const double delta = onset[i * 2] - baseline[i * 2];
			earlyEnergy += delta * delta;
		}
		for (std::size_t i = 15000; i < 16000; ++i)
		{
			const double delta = onset[i * 2] - baseline[i * 2];
			settledEnergy += delta * delta;
		}
		const double earlyRms = std::sqrt(earlyEnergy / 300.0);
		const double settledRms = std::sqrt(settledEnergy / 1000.0);
		require(earlyRms < settledRms * 0.65, "comparison attack was not smoothed: early=" + std::to_string(earlyRms) + ", settled=" + std::to_string(settledRms));

		SoundEngine::Synthesizer changingSettings;
		SoundEngine::Synthesizer fixedSettings;
		changingSettings.setTraceLoaded(true);
		fixedSettings.setTraceLoaded(true);
		changingSettings.render(warm.data(), SampleRate, SampleRate);
		fixedSettings.render(warm.data(), SampleRate, SampleRate);
		SoundEngine::Parameters louder = changingSettings.parameters();
		louder.masterLevel = 1.0f;
		changingSettings.setParameters(louder);
		std::vector<float> gainRamp(16000 * 2);
		std::vector<float> gainReference(16000 * 2);
		changingSettings.render(gainRamp.data(), 16000, SampleRate);
		fixedSettings.render(gainReference.data(), 16000, SampleRate);
		earlyEnergy = 0.0;
		settledEnergy = 0.0;
		for (std::size_t i = 0; i < 300; ++i)
		{
			const double delta = gainRamp[i * 2] - gainReference[i * 2];
			earlyEnergy += delta * delta;
		}
		for (std::size_t i = 15000; i < 16000; ++i)
		{
			const double delta = gainRamp[i * 2] - gainReference[i * 2];
			settledEnergy += delta * delta;
		}
		const double earlyGainDelta = std::sqrt(earlyEnergy / 300.0);
		const double settledGainDelta = std::sqrt(settledEnergy / 1000.0);
		require(earlyGainDelta < settledGainDelta * 0.65, "master-level changes were not smoothed");
	}

	void testChorusSidebands()
	{
		std::vector<float> outputs[2];
		for (int variant = 0; variant < 2; ++variant)
		{
			SoundEngine::Synthesizer synthesizer;
			SoundEngine::Parameters parameters = synthesizer.parameters();
			parameters.chorusMix = variant == 0 ? 0.0f : 0.35f;
			parameters.chorusRateHz = 1.0f;
			parameters.chorusDepthMs = 2.0f;
			synthesizer.setParameters(parameters);
			synthesizer.setTraceLoaded(true);
			outputs[variant].resize(static_cast<std::size_t>(SampleRate) * 2 * 6);
			synthesizer.render(outputs[variant].data(), outputs[variant].size() / 2, SampleRate);
		}
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 2;
		const std::size_t frames = static_cast<std::size_t>(SampleRate) * 4;
		const double drySideband = sineAmplitude(outputs[0], start, frames, 299.0) + sineAmplitude(outputs[0], start, frames, 301.0);
		const double chorusSideband = sineAmplitude(outputs[1], start, frames, 299.0) + sineAmplitude(outputs[1], start, frames, 301.0);
		require(drySideband < 1e-5, "chorus-off output contains unexpected 1 Hz sidebands");
		require(chorusSideband > 1e-4, "chorus did not create measurable modulation sidebands");
	}

	void testChorusWeightingModes()
	{
		SoundEngine::Synthesizer uniform;
		SoundEngine::Synthesizer envelopeWeighted;
		SoundEngine::Parameters uniformParameters = uniform.parameters();
		SoundEngine::Parameters weightedParameters = envelopeWeighted.parameters();
		weightedParameters.chorusWeighting = SoundEngine::ChorusWeighting::ComparisonEnvelope;
		uniformParameters.chorusMix = 0.0f;
		weightedParameters.chorusMix = 0.0f;
		uniform.setParameters(uniformParameters);
		envelopeWeighted.setParameters(weightedParameters);
		uniform.setTraceLoaded(true);
		envelopeWeighted.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.35f, 0.38f, SoundEngine::HeldKeyParticipant::Second);
		uniform.setComparison(comparison);
		envelopeWeighted.setComparison(comparison);
		std::vector<float> warmUniform(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> warmWeighted(static_cast<std::size_t>(SampleRate) * 2);
		uniform.render(warmUniform.data(), SampleRate, SampleRate);
		envelopeWeighted.render(warmWeighted.data(), SampleRate, SampleRate);
		std::vector<float> dryUniform(4096 * 2);
		std::vector<float> dryWeighted(4096 * 2);
		uniform.render(dryUniform.data(), 4096, SampleRate);
		envelopeWeighted.render(dryWeighted.data(), 4096, SampleRate);
		require(dryUniform == dryWeighted, "chorus weighting changed the signal while chorus was disabled");

		uniformParameters.chorusMix = 0.1f;
		weightedParameters.chorusMix = 0.1f;
		uniform.setParameters(uniformParameters);
		envelopeWeighted.setParameters(weightedParameters);
		std::vector<float> uniformOutput(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> weightedOutput(static_cast<std::size_t>(SampleRate) * 2);
		uniform.render(uniformOutput.data(), SampleRate, SampleRate);
		envelopeWeighted.render(weightedOutput.data(), SampleRate, SampleRate);
		double maximumDifference = 0.0;
		for (std::size_t i = 0; i < uniformOutput.size(); ++i)
			maximumDifference = std::max(maximumDifference,
				static_cast<double>(std::fabs(uniformOutput[i] - weightedOutput[i])));
		require(maximumDifference > 1e-4, "comparison-envelope chorus weighting did not alter wet output");
	}

	void testHeadroomAndSilence()
	{
		SoundEngine::Synthesizer silent;
		std::vector<float> silence(4096 * 2, 1.0f);
		silent.render(silence.data(), 4096, SampleRate);
		require(maxAbs(silence) == 0.0, "new engine should be silent without a loaded trace");

		SoundEngine::Parameters parameters = silent.parameters();
		parameters.masterLevel = 1.0f;
		parameters.bedLevel = 1.0f;
		parameters.comparisonBoostDb = 18.0f;
		parameters.heldKeyBoostDb = 18.0f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			parameters.bandTrimDb[i] = 12.0f;
		silent.setParameters(parameters);
		silent.setTraceLoaded(true);
		silent.setComparison(SoundEngine::AudioComparison(0.5f, 0.5f, SoundEngine::HeldKeyParticipant::Second));
		std::vector<float> loud(static_cast<std::size_t>(SampleRate) * 2);
		silent.render(loud.data(), loud.size() / 2, SampleRate);
		require(maxAbs(loud) <= 0.950001, "maximum gain exceeded the soft headroom ceiling");

		parameters.enabled = false;
		silent.setParameters(parameters);
		std::fill(silence.begin(), silence.end(), 1.0f);
		silent.render(silence.data(), 4096, SampleRate);
		require(maxAbs(silence) == 0.0, "disabled engine did not return exact silence");

		SoundEngine::Synthesizer fading;
		fading.setTraceLoaded(true);
		std::vector<float> audible(static_cast<std::size_t>(SampleRate) * 2 / 4);
		fading.render(audible.data(), audible.size() / 2, SampleRate);
		require(maxAbs(audible) > 0.001, "loaded trace should make the harmonic bed audible");
		fading.setTraceLoaded(false);
		std::vector<float> tail(static_cast<std::size_t>(SampleRate) * 2 * 2);
		fading.render(tail.data(), tail.size() / 2, SampleRate);
		std::vector<float> finalSecond(tail.begin() + static_cast<std::size_t>(SampleRate) * 2, tail.end());
		require(maxAbs(std::vector<float>(tail.begin(), tail.begin() + static_cast<std::size_t>(SampleRate) / 5 * 2)) > 0.001,
			"clearing a trace should fade the bed instead of cutting it immediately");
		require(maxAbs(finalSecond) < 1e-5, "cleared trace bed did not fade to silence");
	}

	void testComparisonEnvelopeClearsToZeroWithBedRunning()
	{
		SoundEngine::Synthesizer played;
		SoundEngine::Synthesizer bedOnly;
		played.setTraceLoaded(true);
		bedOnly.setTraceLoaded(true);

		std::vector<float> warmPlayed(4096 * 2);
		std::vector<float> warmBed(4096 * 2);
		played.render(warmPlayed.data(), 4096, SampleRate);
		bedOnly.render(warmBed.data(), 4096, SampleRate);

		const SoundEngine::AudioComparison comparisons[] =
		{
			SoundEngine::AudioComparison(0.12f, 0.16f, SoundEngine::HeldKeyParticipant::Second),
			SoundEngine::AudioComparison(0.54f, 0.58f, SoundEngine::HeldKeyParticipant::Second),
			SoundEngine::AudioComparison(0.88f, 0.93f, SoundEngine::HeldKeyParticipant::Second)
		};
		double comparisonDifference = 0.0;
		for (std::size_t i = 0; i < sizeof(comparisons) / sizeof(comparisons[0]); ++i)
		{
			played.setComparison(comparisons[i]);
			std::vector<float> comparisonOutput(4096 * 2);
			std::vector<float> bedOutput(4096 * 2);
			played.render(comparisonOutput.data(), 4096, SampleRate);
			bedOnly.render(bedOutput.data(), 4096, SampleRate);
			for (std::size_t sample = 0; sample < comparisonOutput.size(); ++sample)
				comparisonDifference = std::max(comparisonDifference,
					static_cast<double>(std::fabs(comparisonOutput[sample] - bedOutput[sample])));
		}
		require(comparisonDifference > 1.0e-5, "played comparisons did not produce a signal above the harmonic bed");

		played.clearComparison();
		std::vector<float> releasedOutput(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> uninterruptedBed(releasedOutput.size());
		played.render(releasedOutput.data(), releasedOutput.size() / 2, SampleRate);
		bedOnly.render(uninterruptedBed.data(), uninterruptedBed.size() / 2, SampleRate);

		require(played.traceLoaded(), "clearing comparison focus unexpectedly stopped the loaded trace bed");
		require(maxAbs(releasedOutput) > 1.0e-4, "harmonic bed stopped while comparison focus was cleared");
		double maximumResidual = 0.0;
		const std::size_t settledStart = static_cast<std::size_t>(SampleRate) * 2 / 2;
		for (std::size_t sample = settledStart; sample < releasedOutput.size(); ++sample)
			maximumResidual = std::max(maximumResidual,
				static_cast<double>(std::fabs(releasedOutput[sample] - uninterruptedBed[sample])));
		require(maximumResidual == 0.0,
			"comparison envelopes left residual output after their release settled; max delta=" +
			std::to_string(maximumResidual));
	}

	void testDenseRetargeting()
	{
		SoundEngine::Synthesizer synthesizer;
		synthesizer.setTraceLoaded(true);
		const std::size_t itemCount = 800;
		std::size_t expectedUpdates = 0;
		for (std::size_t outer = 1; outer < itemCount; ++outer)
		{
			for (std::size_t inner = outer; inner > 0; --inner)
			{
				const float first = static_cast<float>(inner) / static_cast<float>(itemCount - 1);
				const float second = static_cast<float>(inner - 1) / static_cast<float>(itemCount - 1);
				const SoundEngine::HeldKeyParticipant held = (inner & 1) ? SoundEngine::HeldKeyParticipant::Second : SoundEngine::HeldKeyParticipant::None;
				synthesizer.setComparison(SoundEngine::AudioComparison(first, second, held));
				++expectedUpdates;
			}
		}
		require(expectedUpdates == 319600, "dense trace test did not cover the 800-item comparison count");
		require(synthesizer.comparisonUpdateCount() == expectedUpdates, "dense events were not applied immediately in order");
		std::vector<float> finalState(static_cast<std::size_t>(SampleRate) * 2 / 2);
		synthesizer.render(finalState.data(), finalState.size() / 2, SampleRate);
		require(maxAbs(finalState) > 0.001, "latest dense comparison did not reach the output");
	}

	void testDefaultsAndCommandDispatch()
	{
		SoundEngine::Parameters defaults;
		require(defaults.masterLevel == 0.5f && defaults.bedLevel == 0.15f, "default master/bed levels changed");
		require(defaults.bedHarmonicExponent == 1.5f, "default bed spectrum should match the selected preview rolloff");
		require(!defaults.includeSub300HzBed, "sub-300 Hz bed components should be quiet by default");
		require(defaults.bedComb.enabled && defaults.bedComb.applyPhase, "default comb profile should match the selected preview");
		require(near(defaults.bedComb.delayMicroseconds, 222.222, 0.001) && defaults.bedComb.phaseOffsetDegrees == 12.0f &&
			defaults.bedComb.wetAtLowFrequency == 0.95f && defaults.bedComb.wetAtHighFrequency == 0.90f &&
			defaults.bedComb.fadeStartHz == 6600.0f && defaults.bedComb.fadeEndHz == 15600.0f && defaults.bedComb.fadeExponent == 3.0f,
			"default comb values should match the selected preview");
		require(defaults.comparisonSpreadMode == SoundEngine::ComparisonSpreadMode::FixedBins,
			"default comparison spread should preserve fixed-bin behavior");
		require(defaults.comparisonBoostDb == 6.0f && defaults.heldKeyBoostDb == 6.0f, "default comparison gains changed");
		require(defaults.tauBelowBins == 0.5f && defaults.tauAboveBins == 0.5f, "default comparison spread profile changed");
		require(defaults.chorusMix == 0.0f && defaults.chorusRateHz == 1.0f && defaults.chorusDepthMs == 2.0f, "default chorus parameters changed");
		require(defaults.chorusWeighting == SoundEngine::ChorusWeighting::Uniform, "default chorus weighting changed");
		require(defaults.attackMs == 8.0f && defaults.releaseMs == 8.0f && defaults.mappingMode == SoundEngine::MappingMode::LinearHz, "default mapping or envelopes changed");
		require(defaults.enabled && !defaults.includeSub300HzBed, "default synthesis enable or low-bed state changed");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			require(defaults.bandTrimDb[i] == 0.0f, "default per-band trim should be zero");

		SoundEngine::Synthesizer synthesizer;
		SoundEngine::AudioCommand command;
		command.kind = SoundEngine::AudioCommandKind::TraceLoaded;
		command.traceLoaded = true;
		synthesizer.applyCommand(command);
		require(synthesizer.traceLoaded(), "trace-loaded command was ignored");
		command.kind = SoundEngine::AudioCommandKind::Comparison;
		command.comparison = SoundEngine::AudioComparison(0.1f, 0.9f, SoundEngine::HeldKeyParticipant::First);
		synthesizer.applyCommand(command);
		require(synthesizer.comparisonUpdateCount() == 1, "comparison command was not applied");
		command.kind = SoundEngine::AudioCommandKind::ClearComparison;
		synthesizer.applyCommand(command);
		require(synthesizer.traceLoaded(), "clearing a comparison should preserve the trace bed");
	}

	void testSemanticEventAccents()
	{
		SoundEngine::Synthesizer active;
		SoundEngine::Synthesizer muted;
		SoundEngine::Parameters parameters = active.parameters();
		parameters.masterLevel = 0.7f;
		parameters.bedLevel = 0.0f;
		parameters.enabled = true;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.accentRouting.fill(false);
		parameters.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = true;
		parameters.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 0.2f;
		active.setParameters(parameters);
		SoundEngine::Parameters mutedParameters = parameters;
		mutedParameters.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = false;
		muted.setParameters(mutedParameters);
		active.setTraceLoaded(true);
		muted.setTraceLoaded(true);
		std::vector<float> warmup(static_cast<std::size_t>(SampleRate) * 2 / 10);
		active.render(warmup.data(), warmup.size() / 2, SampleRate);
		muted.render(warmup.data(), warmup.size() / 2, SampleRate);
		SoundEngine::EventAccentSummary summary;
		summary.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 3;
		summary.representativePositions[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 0.7f;
		active.setEventAccentSummary(summary);
		muted.setEventAccentSummary(summary);
		std::vector<float> activeSamples(static_cast<std::size_t>(SampleRate) * 2 / 5);
		std::vector<float> mutedSamples(activeSamples.size());
		active.render(activeSamples.data(), activeSamples.size() / 2, SampleRate);
		muted.render(mutedSamples.data(), mutedSamples.size() / 2, SampleRate);
		auto maxDifference = [](const std::vector<float>& left, const std::vector<float>& right)
		{
			double peak = 0.0;
			for (std::size_t i = 0; i < left.size(); ++i)
				peak = std::max(peak, std::fabs(static_cast<double>(left[i] - right[i])));
			return peak;
		};
		const double accentDifference = maxDifference(activeSamples, mutedSamples);
		require(accentDifference > 0.01, "routed probe accents should produce an audible transient");
		active.setEventAccentSummary(SoundEngine::EventAccentSummary());
		std::vector<float> released(static_cast<std::size_t>(SampleRate) * 2 / 5);
		std::vector<float> mutedReleased(released.size());
		active.render(released.data(), released.size() / 2, SampleRate);
		muted.render(mutedReleased.data(), mutedReleased.size() / 2, SampleRate);
		require(maxDifference(released, mutedReleased) < accentDifference,
			"clearing a frame summary should release the accent without a stale event queue");
	}
}

int main()
{
	try
	{
		testLegacyRendererBitExactParity();
		testParameterNormalization();
		testPreparedAudioTimelineMappings();
		testTimelineDrivenBurstPlayback();
		testReconstructedBurstRenderer();
		testReconstructedTimelineClockAndSeekSnapshots();
		testReleaseSelectionFadeDurations();
		testComposableRegistryAndRealtimeAllocation();
		testContinuousSineAndBlockPartitionContinuity();
		testClockedPacketsAndSawSource();
		testMovingFilterAndBedLane();
		testFrequencyBankAndMapping();
		testCombFilterBedShaping();
		testPhaseAwareBedAndHarmonicRolloff();
		testPositionScaledComparisonSpread();
		testMasterGainRemainsLinearUnderHeadroomProtection();
		testHeldKeyWeighting();
		testGeneratedCarrierCenters();
		testExponentialComparisonProfiles();
		testPhaseContinuityAndSmoothing();
		testChorusSidebands();
		testChorusWeightingModes();
		testHeadroomAndSilence();
		testComparisonEnvelopeClearsToZeroWithBedRunning();
		testDenseRetargeting();
		testDefaultsAndCommandDispatch();
		testSemanticEventAccents();
		std::cout << "SoundEngine tests passed (" << assertions << " assertions).\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "SoundEngine test failure: " << error.what() << "\n";
		return 1;
	}
}
