#include "SdlAudioOutput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>

namespace
{
	void writeU16(std::ofstream& stream, std::uint16_t value)
	{
		const char bytes[2] = {
			static_cast<char>(value & 0xff),
			static_cast<char>((value >> 8) & 0xff)
		};
		stream.write(bytes, sizeof(bytes));
	}

	void writeU32(std::ofstream& stream, std::uint32_t value)
	{
		const char bytes[4] = {
			static_cast<char>(value & 0xff),
			static_cast<char>((value >> 8) & 0xff),
			static_cast<char>((value >> 16) & 0xff),
			static_cast<char>((value >> 24) & 0xff)
		};
		stream.write(bytes, sizeof(bytes));
	}
}

SdlAudioOutput::SdlAudioOutput(LatestAudioStateMailbox& mailbox)
	: mailbox_(mailbox), device_(0), obtained_(), subsystemInitialized_(false),
	  status_("Audio device is not initialized."), synthesizer_(), reconstructedSynthesizers_(), activeReconstructedSynthesizer_(0),
	  seekCrossfading_(false), seekCrossfadeProgress_(0), seekCrossfadeFrames_(0), crossfadeOldScratch_(), crossfadeNewScratch_(),
	  appliedRevision_(0),
	  appliedTimelineGeneration_(0), appliedTimeline_(nullptr), appliedTransportRevision_(0), acknowledgedTimelineGeneration_(0),
	  acknowledgedSeekRevision_(0), appliedSeekRevision_(0), appliedSeekState_(nullptr), outputOverloadCount_(0),
	  feedbackSequence_(0), feedbackFrame_(0), feedbackCursor_(0), feedbackGeneration_(0), feedbackTransportRevision_(0), feedbackDurationFrames_(0), feedbackPlaying_(false),
	  diagnosticSamples_(), callbackRecords_(), captureState_(static_cast<unsigned int>(CaptureState::Idle)),
	  targetCaptureFrames_(0), captureFramesWritten_(0), callbackRecordCount_(0),
	  lastCallbackStartCounter_(0), performanceFrequency_(0), callbackTraceLoaded_(false), callbackHasComparison_(false),
	  callbackTimelinePrepared_(false), callbackTimelineGeneration_(0), callbackSeekRevision_(0)
{
	retry();
}

SdlAudioOutput::~SdlAudioOutput()
{
	if (device_ != 0)
	{
		SDL_PauseAudioDevice(device_, 1);
		SDL_CloseAudioDevice(device_);
		device_ = 0;
	}
	if (subsystemInitialized_)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInitialized_ = false;
	}
}

bool SdlAudioOutput::retry()
{
	if (device_ != 0)
	{
		SDL_PauseAudioDevice(device_, 1);
		SDL_CloseAudioDevice(device_);
		device_ = 0;
	}
	if (subsystemInitialized_)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInitialized_ = false;
	}
	for (unsigned int i = 0; i < 2; ++i)
	{
		reconstructedSynthesizers_[i].setPreparedTimeline(nullptr, 0);
		reconstructedSynthesizers_[i].reset();
	}
	activeReconstructedSynthesizer_ = 0;
	seekCrossfading_ = false;
	seekCrossfadeProgress_ = seekCrossfadeFrames_ = 0;
	appliedRevision_ = 0;
	appliedTimelineGeneration_ = 0;
	appliedTimeline_ = nullptr;
	appliedTransportRevision_ = 0;
	appliedSeekRevision_ = 0;
	appliedSeekState_ = nullptr;
	acknowledgedTimelineGeneration_.store(0, std::memory_order_release);
	acknowledgedSeekRevision_.store(0, std::memory_order_release);
	captureState_.store(static_cast<unsigned int>(CaptureState::Idle), std::memory_order_release);
	diagnosticSamples_.clear();
	callbackRecords_.clear();
	targetCaptureFrames_ = 0;
	captureFramesWritten_ = 0;
	callbackRecordCount_ = 0;
	lastCallbackStartCounter_ = 0;
	performanceFrequency_ = 0;
	callbackTraceLoaded_ = false;
	callbackHasComparison_ = false;
	callbackTimelinePrepared_ = false;
	callbackTimelineGeneration_ = 0;
	callbackSeekRevision_ = 0;
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		status_ = std::string("Audio unavailable: ") + SDL_GetError();
		return false;
	}
	subsystemInitialized_ = true;
	performanceFrequency_ = SDL_GetPerformanceFrequency();
	SDL_AudioSpec desired;
	SDL_zero(desired);
	desired.freq = 44100;
	desired.format = AUDIO_F32SYS;
	desired.channels = 2;
	desired.samples = 512;
	desired.callback = &SdlAudioOutput::audioCallback;
	desired.userdata = this;
	SDL_zero(obtained_);
	device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained_, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (device_ == 0)
	{
		status_ = std::string("Audio unavailable: ") + SDL_GetError();
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInitialized_ = false;
		return false;
	}
	if (obtained_.format != AUDIO_F32SYS || obtained_.channels != 2)
	{
		status_ = "Audio unavailable: the device did not accept stereo float output.";
		SDL_CloseAudioDevice(device_);
		device_ = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInitialized_ = false;
		return false;
	}
	targetCaptureFrames_ = static_cast<std::size_t>(obtained_.freq) * DiagnosticDurationSeconds;
	const std::size_t channels = obtained_.channels;
	const std::size_t recordCapacity = (targetCaptureFrames_ + std::max<std::size_t>(1, obtained_.samples) - 1) /
		std::max<std::size_t>(1, obtained_.samples) + 2;
	try
	{
		diagnosticSamples_.resize(targetCaptureFrames_ * channels);
		callbackRecords_.resize(recordCapacity);
	}
	catch (...)
	{
		status_ = "Audio unavailable: there was not enough memory to reserve diagnostic capture space.";
		SDL_CloseAudioDevice(device_);
		device_ = 0;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		subsystemInitialized_ = false;
		return false;
	}
	SDL_PauseAudioDevice(device_, 0);
	status_ = "Audio ready (" + std::to_string(obtained_.freq) + " Hz stereo).";
	return true;
}

bool SdlAudioOutput::armDiagnosticCapture()
{
	if (device_ == 0 || targetCaptureFrames_ == 0 || diagnosticSamples_.empty())
	{
		status_ = "Audio is unavailable; diagnostic capture could not start.";
		return false;
	}
	const unsigned int current = captureState_.load(std::memory_order_acquire);
	if (current == static_cast<unsigned int>(CaptureState::Armed) ||
		current == static_cast<unsigned int>(CaptureState::Complete) ||
		current == static_cast<unsigned int>(CaptureState::Saving))
	{
		status_ = "A diagnostic capture is already in progress.";
		return false;
	}
	captureFramesWritten_ = 0;
	callbackRecordCount_ = 0;
	lastCallbackStartCounter_ = 0;
	captureState_.store(static_cast<unsigned int>(CaptureState::Armed), std::memory_order_release);
	status_ = "Diagnostic capture armed for 30 seconds. Reproduce the stutter now.";
	return true;
}

void SdlAudioOutput::serviceDiagnosticCapture()
{
	unsigned int expected = static_cast<unsigned int>(CaptureState::Complete);
	if (!captureState_.compare_exchange_strong(expected,
		static_cast<unsigned int>(CaptureState::Saving), std::memory_order_acq_rel))
		return;

	if (saveDiagnosticCapture())
		status_ = "Diagnostic saved: AudioDiagnostic.wav and AudioDiagnostic.csv in the app working folder.";
	else
		status_ = "Diagnostic capture finished, but its files could not be written in the app working folder.";
	captureState_.store(static_cast<unsigned int>(CaptureState::Saved), std::memory_order_release);
}

void SdlAudioOutput::audioCallback(void* userdata, Uint8* stream, int length)
{
	SdlAudioOutput* output = static_cast<SdlAudioOutput*>(userdata);
	if (output) output->render(stream, length);
	else if (stream && length > 0) std::memset(stream, 0, static_cast<std::size_t>(length));
}

void SdlAudioOutput::render(Uint8* stream, int length)
{
	if (!stream || length <= 0) return;
	const std::size_t sampleCount = static_cast<std::size_t>(length) / sizeof(float);
	const std::size_t frameCount = sampleCount / 2;
	const bool capturing = captureState_.load(std::memory_order_acquire) ==
		static_cast<unsigned int>(CaptureState::Armed);
	const Uint64 callbackStart = capturing ? SDL_GetPerformanceCounter() : 0;
	const Uint64 callbackInterval = capturing && lastCallbackStartCounter_ != 0
		? callbackStart - lastCallbackStartCounter_ : 0;
	if (capturing)
		lastCallbackStartCounter_ = callbackStart;
	AudioPresentationState state;
	if (mailbox_.consumeLatest(state) && state.revision != appliedRevision_)
	{
		callbackTraceLoaded_ = state.traceLoaded;
		callbackHasComparison_ = state.hasComparison;
		callbackTimelinePrepared_ = state.timeline != nullptr;
		callbackTimelineGeneration_ = state.timelineGeneration;
		callbackSeekRevision_ = state.seekRevision;
		for (unsigned int i = 0; i < 2; ++i)
		{
			reconstructedSynthesizers_[i].setParameters(state.reconstructedParameters);
			reconstructedSynthesizers_[i].setTimelineGainSettings(state.timelineGains);
		}
		const bool newSeekState = state.seekState && state.seekRevision != appliedSeekRevision_ &&
			state.seekState->timelineGeneration == state.timelineGeneration &&
			state.seekState->transportRevision <= state.transport.revision;
		const bool timelineChanged = state.timelineGeneration != appliedTimelineGeneration_ || state.timeline != appliedTimeline_;
		if (newSeekState)
		{
			const unsigned int incomingIndex = 1u - activeReconstructedSynthesizer_;
			reconstructedSynthesizers_[incomingIndex].installPreparedSeekState(*state.seekState);
			// A newer Play can be coalesced with the paused snapshot publication.
			// Installing history must not discard that newer transport command.
			reconstructedSynthesizers_[incomingIndex].setTransportState(state.transport);
			appliedSeekState_ = state.seekState;
			appliedSeekRevision_ = state.seekRevision;
			seekCrossfading_ = true;
			seekCrossfadeProgress_ = 0;
			seekCrossfadeFrames_ = (std::max<std::size_t>)(1, static_cast<std::size_t>(std::ceil(
				128.0 * static_cast<double>(obtained_.freq) / 44100.0)));
			appliedTimelineGeneration_ = state.timelineGeneration;
			appliedTimeline_ = state.timeline;
			appliedTransportRevision_ = state.transport.revision;
		}
		else
		{
			if (timelineChanged)
			{
				for (unsigned int i = 0; i < 2; ++i)
					reconstructedSynthesizers_[i].setPreparedTimeline(state.timeline, state.timelineGeneration);
				seekCrossfading_ = false;
				activeReconstructedSynthesizer_ = 0;
				appliedTimelineGeneration_ = state.timelineGeneration;
				appliedTimeline_ = state.timeline;
				appliedTransportRevision_ = 0;
			}
			if (state.timeline && state.transport.generation == state.timelineGeneration)
			{
				if (state.transport.revision != appliedTransportRevision_)
				{
					for (unsigned int i = 0; i < 2; ++i)
						reconstructedSynthesizers_[i].setTransportState(state.transport);
					appliedTransportRevision_ = state.transport.revision;
				}
			}
			else if (state.hasComparison)
			{
				for (unsigned int i = 0; i < 2; ++i)
				{
					reconstructedSynthesizers_[i].setComparison(state.burstComparison);
					if (state.releaseSelection) reconstructedSynthesizers_[i].clearComparison();
				}
			}
			else
				for (unsigned int i = 0; i < 2; ++i) reconstructedSynthesizers_[i].stopBirths();
			if (timelineChanged)
				acknowledgedTimelineGeneration_.store(state.timelineGeneration, std::memory_order_release);
		}
		appliedRevision_ = state.revision;
	}
	const std::uint32_t deviceRate = obtained_.freq > 0 ? static_cast<std::uint32_t>(obtained_.freq) : 0u;
	float* outputSamples = reinterpret_cast<float*>(stream);
	if (seekCrossfading_)
	{
		std::size_t offset = 0;
		while (offset < frameCount)
		{
			const std::size_t count = (std::min<std::size_t>)(512, frameCount - offset);
			const unsigned int incomingIndex = 1u - activeReconstructedSynthesizer_;
			float* oldSamples = crossfadeOldScratch_.data();
			float* newSamples = crossfadeNewScratch_.data();
			reconstructedSynthesizers_[activeReconstructedSynthesizer_].renderAtSampleRateLinear(oldSamples, count, deviceRate);
			reconstructedSynthesizers_[incomingIndex].renderAtSampleRateLinear(newSamples, count, deviceRate);
			for (std::size_t frame = 0; frame < count; ++frame)
			{
				const std::size_t progress = seekCrossfadeProgress_;
				const double blend = progress >= seekCrossfadeFrames_ ? 1.0 :
					static_cast<double>(progress + 1) / static_cast<double>(seekCrossfadeFrames_);
				for (std::size_t channel = 0; channel < 2; ++channel)
				{
					const std::size_t sample = frame * 2 + channel;
					outputSamples[(offset + frame) * 2 + channel] = static_cast<float>(
						oldSamples[sample] * (1.0 - blend) + newSamples[sample] * blend);
				}
				if (seekCrossfadeProgress_ < seekCrossfadeFrames_) ++seekCrossfadeProgress_;
			}
			offset += count;
			if (seekCrossfadeProgress_ >= seekCrossfadeFrames_)
			{
				activeReconstructedSynthesizer_ = incomingIndex;
				seekCrossfading_ = false;
				acknowledgedTimelineGeneration_.store(appliedTimelineGeneration_, std::memory_order_release);
				acknowledgedSeekRevision_.store(appliedSeekRevision_, std::memory_order_release);
				if (offset < frameCount)
					reconstructedSynthesizers_[activeReconstructedSynthesizer_].renderAtSampleRateLinear(
						outputSamples + offset * 2, frameCount - offset, deviceRate);
				break;
			}
		}
	}
	else
		reconstructedSynthesizers_[activeReconstructedSynthesizer_].renderAtSampleRateLinear(outputSamples, frameCount, deviceRate);
	for (std::size_t sample = 0; sample < frameCount * 2; ++sample)
	{
		const float value = outputSamples[sample];
		if (!std::isfinite(value) || std::abs(value) > 0.999f)
		{
			outputSamples[sample] = std::isfinite(value) ? (value < 0.0f ? -0.999f : 0.999f) : 0.0f;
			outputOverloadCount_.fetch_add(1, std::memory_order_relaxed);
		}
	}
	const unsigned int feedbackSynthIndex = seekCrossfading_ ? 1u - activeReconstructedSynthesizer_ : activeReconstructedSynthesizer_;
	const SoundEngine::AudioPlayheadFeedback feedback = reconstructedSynthesizers_[feedbackSynthIndex].playheadFeedback();
	feedbackSequence_.fetch_add(1, std::memory_order_acq_rel);
	feedbackFrame_.store(feedback.timelineFrame, std::memory_order_relaxed);
	feedbackCursor_.store(feedback.sourceCursor, std::memory_order_relaxed);
	feedbackGeneration_.store(feedback.generation, std::memory_order_relaxed);
	feedbackTransportRevision_.store(feedback.transportRevision, std::memory_order_relaxed);
	feedbackDurationFrames_.store(feedback.durationFrames, std::memory_order_relaxed);
	feedbackPlaying_.store(feedback.playing, std::memory_order_relaxed);
	feedbackSequence_.fetch_add(1, std::memory_order_release);
	const Uint64 renderEnd = capturing ? SDL_GetPerformanceCounter() : 0;
	const std::size_t renderedBytes = frameCount * 2 * sizeof(float);
	if (renderedBytes < static_cast<std::size_t>(length))
		std::memset(stream + renderedBytes, 0, static_cast<std::size_t>(length) - renderedBytes);

	if (capturing)
	{
		const std::size_t remaining = targetCaptureFrames_ - captureFramesWritten_;
		const std::size_t capturedFrames = std::min(frameCount, remaining);
		if (capturedFrames > 0)
		{
			const std::size_t capturedSamples = capturedFrames * 2;
			float* destination = diagnosticSamples_.data() + captureFramesWritten_ * 2;
			std::memcpy(destination, stream, capturedSamples * sizeof(float));
			double squareSum = 0.0;
			const float* captured = reinterpret_cast<const float*>(stream);
			for (std::size_t i = 0; i < capturedSamples; ++i)
				squareSum += static_cast<double>(captured[i]) * captured[i];
			if (callbackRecordCount_ < callbackRecords_.size())
			{
				CallbackRecord& record = callbackRecords_[callbackRecordCount_++];
				record.frameOffset = captureFramesWritten_;
				record.frameCount = static_cast<std::uint32_t>(capturedFrames);
				record.intervalTicks = callbackInterval;
				record.renderTicks = renderEnd - callbackStart;
				record.deadlineTicks = obtained_.freq > 0 && performanceFrequency_ > 0
					? static_cast<std::uint64_t>(static_cast<double>(frameCount) * performanceFrequency_ / obtained_.freq) : 0;
				record.timelineFrame = feedback.timelineFrame;
				record.timelineGeneration = feedback.generation;
				record.seekRevision = callbackSeekRevision_;
				record.overloadSamples = outputOverloadCount_.load(std::memory_order_relaxed);
				record.transportRevision = feedback.transportRevision;
				record.playing = feedback.playing;
				record.sourceCursor = static_cast<std::uint32_t>((std::min<std::size_t>)(feedback.sourceCursor,
					(std::numeric_limits<std::uint32_t>::max)()));
				record.rms = static_cast<float>(std::sqrt(squareSum / capturedSamples));
				record.traceLoaded = callbackTraceLoaded_;
				record.hasComparison = callbackHasComparison_;
				record.timelinePrepared = callbackTimelinePrepared_;
				record.deadlineMissed = record.deadlineTicks > 0 && record.renderTicks > record.deadlineTicks;
			}
			captureFramesWritten_ += capturedFrames;
			if (captureFramesWritten_ >= targetCaptureFrames_)
				captureState_.store(static_cast<unsigned int>(CaptureState::Complete), std::memory_order_release);
		}
	}
}

bool SdlAudioOutput::readPlayheadFeedback(SoundEngine::AudioPlayheadFeedback& feedback) const
{
	for (int attempt = 0; attempt < 8; ++attempt)
	{
		const std::uint64_t before = feedbackSequence_.load(std::memory_order_acquire);
		if (before == 0 || (before & 1u)) continue;
		SoundEngine::AudioPlayheadFeedback copy;
		copy.timelineFrame = feedbackFrame_.load(std::memory_order_relaxed);
		copy.sourceCursor = static_cast<std::size_t>(feedbackCursor_.load(std::memory_order_relaxed));
		copy.generation = feedbackGeneration_.load(std::memory_order_relaxed);
		copy.transportRevision = feedbackTransportRevision_.load(std::memory_order_relaxed);
		copy.durationFrames = feedbackDurationFrames_.load(std::memory_order_relaxed);
		copy.playing = feedbackPlaying_.load(std::memory_order_relaxed);
		const std::uint64_t after = feedbackSequence_.load(std::memory_order_acquire);
		if (before == after)
		{
			feedback = copy;
			return true;
		}
	}
	return false;
}

bool SdlAudioOutput::saveDiagnosticCapture()
{
	if (captureFramesWritten_ == 0 || obtained_.freq <= 0)
		return false;
	const std::uint32_t sampleRate = static_cast<std::uint32_t>(obtained_.freq);
	const std::uint32_t dataBytes = static_cast<std::uint32_t>(captureFramesWritten_ * 2 * sizeof(float));
	std::ofstream wav("AudioDiagnostic.wav", std::ios::binary | std::ios::trunc);
	if (!wav)
		return false;
	wav.write("RIFF", 4);
	writeU32(wav, 36u + dataBytes);
	wav.write("WAVE", 4);
	wav.write("fmt ", 4);
	writeU32(wav, 16u);
	writeU16(wav, 3u); // IEEE float PCM
	writeU16(wav, 2u);
	writeU32(wav, sampleRate);
	writeU32(wav, sampleRate * 2u * static_cast<std::uint32_t>(sizeof(float)));
	writeU16(wav, static_cast<std::uint16_t>(2u * sizeof(float)));
	writeU16(wav, 32u);
	wav.write("data", 4);
	writeU32(wav, dataBytes);
	wav.write(reinterpret_cast<const char*>(diagnosticSamples_.data()), dataBytes);
	wav.close();
	if (!wav)
		return false;

	std::ofstream csv("AudioDiagnostic.csv", std::ios::trunc);
	if (!csv)
		return false;
	const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());
	csv << "callback,frame_offset,frame_count,time_seconds,interval_ms,render_ms,deadline_ms,deadline_missed,rms,trace_loaded,comparison_active,timeline_prepared,timeline_generation,timeline_frame,source_cursor,seek_revision,overload_samples,transport_revision,playing\n";
	csv << std::fixed << std::setprecision(6);
	for (std::size_t i = 0; i < callbackRecordCount_; ++i)
	{
		const CallbackRecord& record = callbackRecords_[i];
		const double timeSeconds = static_cast<double>(record.frameOffset) / sampleRate;
		const double intervalMs = counterFrequency > 0.0
			? static_cast<double>(record.intervalTicks) * 1000.0 / counterFrequency : 0.0;
		const double renderMs = counterFrequency > 0.0
			? static_cast<double>(record.renderTicks) * 1000.0 / counterFrequency : 0.0;
		const double deadlineMs = counterFrequency > 0.0
			? static_cast<double>(record.deadlineTicks) * 1000.0 / counterFrequency : 0.0;
		csv << i << ',' << record.frameOffset << ',' << record.frameCount << ',' << timeSeconds << ','
			<< intervalMs << ',' << renderMs << ',' << deadlineMs << ',' << (record.deadlineMissed ? 1 : 0) << ','
			<< record.rms << ',' << (record.traceLoaded ? 1 : 0) << ',' << (record.hasComparison ? 1 : 0) << ','
			<< (record.timelinePrepared ? 1 : 0) << ',' << record.timelineGeneration << ',' << record.timelineFrame << ','
			<< record.sourceCursor << ',' << record.seekRevision << ',' << record.overloadSamples << ','
			<< record.transportRevision << ',' << (record.playing ? 1 : 0) << '\n';
	}
	return static_cast<bool>(csv);
}
