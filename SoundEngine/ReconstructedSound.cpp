#include "ReconstructedSound.h"

#include <algorithm>
#include <cmath>

namespace SoundEngine
{
	namespace
	{
		const double SampleRate = 44100.0;
		const double Pi = 3.1415926535897932384626433832795;
		const std::size_t ResamplerPhaseCount = 1024;
		const std::size_t ResamplerTapCount = 16;
		const std::size_t ResamplerCutoffCount = 16;
		struct ResamplerCoefficientBank
		{
			std::array<float, ResamplerPhaseCount * ResamplerTapCount> coefficients[ResamplerCutoffCount];
			double cutoffs[ResamplerCutoffCount];
			ResamplerCoefficientBank()
			{
				const double values[ResamplerCutoffCount] = {
					1.0, 0.875, 0.75, 0.625, 0.5, 0.4375, 0.375, 0.3125,
					0.25, 0.21875, 0.1875, 0.15625, 0.125, 0.09375, 0.0625, 0.03125
				};
				for (std::size_t bank = 0; bank < ResamplerCutoffCount; ++bank)
				{
					cutoffs[bank] = values[bank];
					for (std::size_t phase = 0; phase < ResamplerPhaseCount; ++phase)
					{
						const double fraction = static_cast<double>(phase) / static_cast<double>(ResamplerPhaseCount);
						double raw[ResamplerTapCount];
						double sum = 0.0;
						for (std::size_t tap = 0; tap < ResamplerTapCount; ++tap)
						{
							const double distance = fraction + 7.5 - static_cast<double>(tap);
							const double window = std::abs(distance) >= 8.0 ? 0.0 :
								0.5 + 0.5 * std::cos(Pi * distance / 8.0);
							const double argument = values[bank] * distance;
							const double sinc = std::abs(argument) < 1.0e-12 ? 1.0 :
								std::sin(Pi * argument) / (Pi * argument);
							raw[tap] = values[bank] * sinc * window;
							sum += raw[tap];
						}
						const std::size_t offset = phase * ResamplerTapCount;
						for (std::size_t tap = 0; tap < ResamplerTapCount; ++tap)
							coefficients[bank][offset + tap] = static_cast<float>(sum == 0.0 ? 0.0 : raw[tap] / sum);
					}
				}
			}
		};
		const ResamplerCoefficientBank& resamplerCoefficientBank()
		{
			static const ResamplerCoefficientBank bank;
			return bank;
		}
		std::size_t resamplerCutoffIndex(double cutoff)
		{
			const ResamplerCoefficientBank& bank = resamplerCoefficientBank();
			for (std::size_t i = 0; i < ResamplerCutoffCount; ++i)
				if (bank.cutoffs[i] <= cutoff) return i;
			return ResamplerCutoffCount - 1;
		}
		double clamp(double value, double low, double high)
		{
			return (std::max)(low, (std::min)(high, value));
		}

		double roleGain(std::uint8_t role)
		{
			// The reconstruction uses equal comparison lanes by default. Semantic
			// roles are carried for future per-role controls but add no hidden boost.
			(void)role;
			return 1.0;
		}
	}

	ReconstructedParameters::ReconstructedParameters()
		: minimumFrequencyHz(104.0322699094767), maximumFrequencyHz(4054.6605862015895),
		  excitationClockHz(300.0), carrierCycles(9.302650315454065), tukeyFraction(0.025666772580559438),
		  secondOperandDelayMicroseconds(225.66891905936825), masterLevel(1.0), specialReleaseMilliseconds(250.0),
		  lowPassCutoffHz(20000.0), audioEnabled(true), lowPassEnabled(false), mapping(ReconstructedMapping::LinearHz),
		  idlePolicy(ReconstructedIdlePolicy::HoldSelection)
	{
	}

	BurstComparison::BurstComparison()
		: firstPosition(0.0), secondPosition(0.0), firstRole(0), secondRole(0), firstGain(1.0), secondGain(1.0), probeOnly(false)
	{
	}

	PreparedAudioSeekState::PreparedAudioSeekState()
		: timeline(nullptr), timelineGeneration(0), parameterRevision(0), transportRevision(0), targetFrame(0), targetCursor(0),
		notes(), firHistory(), firPosition(0), sampleClock(0), nextClockFrame(128), firstLaneFrame(128),
		timelineAnchorSample(128), timelineAnchorFrame(0), clockRemainder(0.0), currentMaster(1.0), targetMaster(1.0),
		idleFadeRemaining(0.0), idleFadeLength(0.0), smoothedCutoff(20000.0), filterB0(0.0), filterB1(0.0), filterB2(0.0),
		filterA1(0.0), filterA2(0.0), filterX1(0.0), filterX2(0.0), filterY1(0.0), filterY2(0.0),
		filterControlCountdown(0), comparison(), pendingComparison(), pendingParameters(), transport(), pendingBurstGain(1.0),
		hasComparison(false), comparisonBirthsEnabled(false), firstLanePending(false), firstLaneScheduled(false),
		releaseSelectionActive(false), valid(false)
	{
	}

	ReconstructedSynthesizer::ReconstructedSynthesizer()
		: parameters_(), comparison_(), pendingComparison_(), pendingParameters_(), noteSources_(), fir_(), timeline_(nullptr),
		  transport_(), timelineGains_(), transportRevision_(0), timelineAnchorSample_(128), timelineAnchorFrame_(0),
		  timelineFrame_(0), timelineCursor_(0),
		  resamplerRing_(), resamplerScratch_(),
		  resamplerBaseFrame_(0), resamplerGeneratedFrames_(0), resamplerSourcePosition_(0.0), resamplerDeviceRate_(44100),
		  sampleClock_(0),
		  nextClockFrame_(128), firstLaneFrame_(128), clockRemainder_(0.0), currentMaster_(1.0), targetMaster_(1.0), idleFadeRemaining_(0.0),
		  idleFadeLength_(0.0), smoothedCutoff_(20000.0), filterB0_(0.0), filterB1_(0.0), filterB2_(0.0), filterA1_(0.0), filterA2_(0.0),
		  filterX1_(0.0), filterX2_(0.0), filterY1_(0.0), filterY2_(0.0), filterControlCountdown_(0), hasComparison_(false),
		  comparisonBirthsEnabled_(false), firstLanePending_(false), firstLaneScheduled_(true), releaseSelectionActive_(false),
		  pendingBurstGain_(1.0), overloadLatched_(false), overloadCount_(0)
	{
		// Pay the small coefficient-table initialization cost on the owner thread,
		// before SDL starts invoking the audio callback.
		(void)resamplerCoefficientBank();
		updateFilterCoefficients();
	}

	void ReconstructedSynthesizer::setParameters(const ReconstructedParameters& input)
	{
		ReconstructedParameters p = input;
		p.minimumFrequencyHz = clamp(p.minimumFrequencyHz, 50.0, 1000.0);
		p.maximumFrequencyHz = clamp(p.maximumFrequencyHz, 1000.0, 8000.0);
		if (p.minimumFrequencyHz >= p.maximumFrequencyHz)
		{
			p.minimumFrequencyHz = parameters_.minimumFrequencyHz;
			p.maximumFrequencyHz = parameters_.maximumFrequencyHz;
		}
		p.excitationClockHz = clamp(p.excitationClockHz, 100.0, 600.0);
		p.carrierCycles = clamp(p.carrierCycles, 1.0, 16.0);
		p.tukeyFraction = clamp(p.tukeyFraction, 0.0, 1.0);
		p.secondOperandDelayMicroseconds = clamp(p.secondOperandDelayMicroseconds, 0.0, 2000.0);
		p.masterLevel = clamp(p.masterLevel, 0.0, 1.0);
		p.specialReleaseMilliseconds = clamp(p.specialReleaseMilliseconds, 0.0, 2000.0);
		p.lowPassCutoffHz = clamp(p.lowPassCutoffHz, 500.0, 20000.0);
		parameters_ = p;
		targetMaster_ = p.masterLevel;
		if (!firstLanePending_ && sampleClock_ < nextClockFrame_)
		{
			const double delayFrames = p.secondOperandDelayMicroseconds * SampleRate / 1000000.0;
			const std::uint64_t earlyLead = static_cast<std::uint64_t>(std::ceil(delayFrames));
			firstLaneFrame_ = nextClockFrame_ >= earlyLead ? nextClockFrame_ - earlyLead : sampleClock_;
			firstLaneScheduled_ = true;
		}
	}

	void ReconstructedSynthesizer::setComparison(const BurstComparison& comparison)
	{
		comparison_ = comparison;
		comparison_.firstPosition = clamp(comparison_.firstPosition, 0.0, 1.0);
		comparison_.secondPosition = clamp(comparison_.secondPosition, 0.0, 1.0);
		comparison_.firstGain = clamp(comparison_.firstGain * roleGain(comparison_.firstRole), 0.0, 1000.0);
		comparison_.secondGain = clamp(comparison_.secondGain * roleGain(comparison_.secondRole), 0.0, 1000.0);
		hasComparison_ = true;
		comparisonBirthsEnabled_ = true;
		idleFadeRemaining_ = 0.0;
		idleFadeLength_ = 0.0;
		releaseSelectionActive_ = false;
	}

	void ReconstructedSynthesizer::setPreparedTimeline(const PreparedAudioTimeline* timeline, std::uint64_t generation)
	{
		if (timeline_ == timeline && transport_.generation == generation) return;
		timeline_ = timeline && timeline->ready() ? timeline : nullptr;
		transport_ = AudioTransportState();
		transport_.generation = generation;
		transportRevision_ = 0;
		timelineAnchorSample_ = nextClockFrame_;
		timelineAnchorFrame_ = 0;
		timelineFrame_ = timelineCursor_ = 0;
		stopBirths();
		firstLaneFrame_ = nextClockFrame_;
		firstLaneScheduled_ = true;
	}

	void ReconstructedSynthesizer::setTimelineGainSettings(const AudioTimelineGainSettings& gains)
	{
		timelineGains_.comparisonGain = clamp(gains.comparisonGain, 0.0, 1000.0);
		timelineGains_.heldKeyGain = clamp(gains.heldKeyGain, 0.0, 1000.0);
		timelineGains_.probeGain = clamp(gains.probeGain, 0.0, 1000.0);
		for (std::size_t i = 0; i < AudioTimelineRoleCount; ++i)
			timelineGains_.roleGain[i] = clamp(gains.roleGain[i], 0.0, 1000.0);
	}

	void ReconstructedSynthesizer::setTransportState(const AudioTransportState& requested)
	{
		if (!timeline_ || requested.generation != transport_.generation || requested.revision <= transportRevision_)
			return;
		transport_ = requested;
		transportRevision_ = requested.revision;
		timelineFrame_ = (std::min)(requested.timelineFrame, timeline_->durationFrames());
		timelineCursor_ = (std::min)(requested.sourceCursor, timeline_->sourceEventCount());
		timelineAnchorSample_ = requested.playing && timelineFrame_ == 0 && sampleClock_ == 0
			? nextClockFrame_ : sampleClock_;
		timelineAnchorFrame_ = timelineFrame_;
		if (!requested.stopAtEnd && !firstLanePending_)
		{
			const double delayFrames = parameters_.secondOperandDelayMicroseconds * SampleRate / 1000000.0;
			const std::uint64_t earlyLead = static_cast<std::uint64_t>(std::ceil(delayFrames));
			firstLaneFrame_ = nextClockFrame_ >= earlyLead ? nextClockFrame_ - earlyLead : sampleClock_;
			firstLaneScheduled_ = true;
		}
		else if (firstLanePending_)
			firstLaneScheduled_ = false;
		if (requested.stopAtEnd)
		{
			transport_.playing = false;
			transport_.holdSelection = false;
			stopBirths();
		}
		else if (requested.releaseSelection)
		{
			AudioTimelineEvent selected;
			if (timeline_->comparisonAtCursor(timelineCursor_, selected))
				selectTimelineEvent(selected);
			clearComparison();
		}
		else if (!requested.playing && !requested.holdSelection)
			stopBirths();
		else if (!requested.playing && requested.holdSelection)
		{
			AudioTimelineEvent selected;
			if (timeline_->comparisonAtCursor(timelineCursor_, selected)) selectTimelineEvent(selected);
			else stopBirths();
		}
	}

	bool ReconstructedSynthesizer::prepareSeekState(const PreparedAudioTimeline& timeline,
		const ReconstructedParameters& parameters, const AudioTimelineGainSettings& gains,
		const AudioTransportState& requestedTransport, std::uint64_t parameterRevision,
		PreparedAudioSeekState& result, std::string* error)
	{
		result = PreparedAudioSeekState();
		if (!timeline.ready() || timeline.sampleRate() != static_cast<std::uint32_t>(SampleRate))
		{
			if (error) *error = "Seek preparation requires a ready 44.1 kHz timeline.";
			return false;
		}
		const std::uint64_t targetFrame = (std::min)(requestedTransport.timelineFrame, timeline.durationFrames());
		const std::size_t targetCursor = (std::min)(requestedTransport.sourceCursor, timeline.sourceEventCount());
		// 20,000 frames cover the longest allowed 16-cycle note at 50 Hz,
		// maximum operand delay, the reference FIR, and >1e-12 two-pole decay.
		const std::uint64_t historyFrames = 20000;
		const std::uint64_t historyStart = targetFrame > historyFrames ? targetFrame - historyFrames : 0;
		ReconstructedSynthesizer scratch;
		scratch.setParameters(parameters);
		scratch.setTimelineGainSettings(gains);
		scratch.timeline_ = &timeline;
		scratch.transport_ = AudioTransportState();
		scratch.transport_.generation = requestedTransport.generation;
		scratch.transport_.playing = true;
		scratch.transportRevision_ = 0;
		scratch.timelineAnchorSample_ = 128;
		scratch.timelineAnchorFrame_ = 0;
		scratch.timelineFrame_ = historyStart;
		scratch.timelineCursor_ = timeline.cursorAtFrame(historyStart);
		if (historyStart != 0)
		{
			const long double period = static_cast<long double>(SampleRate) / parameters.excitationClockHz;
			long double tick = std::ceil(static_cast<long double>(historyStart) / period);
			long double offset = tick * period;
			while (std::floor(offset) < static_cast<long double>(historyStart))
			{
				tick += 1.0L;
				offset = tick * period;
			}
			const long double wholeOffset = std::floor(offset);
			scratch.sampleClock_ = 128 + historyStart;
			scratch.nextClockFrame_ = 128 + static_cast<std::uint64_t>(wholeOffset);
			scratch.clockRemainder_ = static_cast<double>(offset - wholeOffset);
			const std::uint64_t delayLead = static_cast<std::uint64_t>(std::ceil(
				parameters.secondOperandDelayMicroseconds * SampleRate / 1000000.0));
			scratch.firstLaneFrame_ = scratch.nextClockFrame_ >= delayLead
				? scratch.nextClockFrame_ - delayLead : scratch.sampleClock_;
			if (scratch.firstLaneFrame_ < scratch.sampleClock_)
				scratch.firstLaneFrame_ = scratch.sampleClock_;
			scratch.firstLaneScheduled_ = true;
		}
		const std::uint64_t targetSample = 128 + targetFrame;
		std::array<float, 512> block;
		while (scratch.sampleClock_ < targetSample)
		{
			const std::uint64_t remaining = targetSample - scratch.sampleClock_;
			const std::size_t count = static_cast<std::size_t>((std::min<std::uint64_t>)(remaining, block.size() / 2));
			scratch.render(block.data(), count);
		}
		AudioTransportState finalTransport = requestedTransport;
		finalTransport.timelineFrame = targetFrame;
		finalTransport.sourceCursor = targetCursor;
		if (finalTransport.revision <= scratch.transportRevision_)
			finalTransport.revision = scratch.transportRevision_ + 1;
		scratch.setTransportState(finalTransport);

		result.timeline = &timeline;
		result.timelineGeneration = finalTransport.generation;
		result.parameterRevision = parameterRevision;
		result.transportRevision = finalTransport.revision;
		result.targetFrame = targetFrame;
		result.targetCursor = targetCursor;
		result.notes[0] = scratch.noteSources_[0].snapshot();
		result.notes[1] = scratch.noteSources_[1].snapshot();
		result.firHistory = scratch.fir_.history();
		result.firPosition = scratch.fir_.position();
		result.sampleClock = scratch.sampleClock_;
		result.nextClockFrame = scratch.nextClockFrame_;
		result.firstLaneFrame = scratch.firstLaneFrame_;
		result.timelineAnchorSample = scratch.timelineAnchorSample_;
		result.timelineAnchorFrame = scratch.timelineAnchorFrame_;
		result.clockRemainder = scratch.clockRemainder_;
		result.currentMaster = scratch.currentMaster_;
		result.targetMaster = scratch.targetMaster_;
		result.idleFadeRemaining = scratch.idleFadeRemaining_;
		result.idleFadeLength = scratch.idleFadeLength_;
		result.smoothedCutoff = scratch.smoothedCutoff_;
		result.filterB0 = scratch.filterB0_; result.filterB1 = scratch.filterB1_; result.filterB2 = scratch.filterB2_;
		result.filterA1 = scratch.filterA1_; result.filterA2 = scratch.filterA2_;
		result.filterX1 = scratch.filterX1_; result.filterX2 = scratch.filterX2_;
		result.filterY1 = scratch.filterY1_; result.filterY2 = scratch.filterY2_;
		result.filterControlCountdown = scratch.filterControlCountdown_;
		result.comparison = scratch.comparison_;
		result.pendingComparison = scratch.pendingComparison_;
		result.pendingParameters = scratch.pendingParameters_;
		result.transport = scratch.transport_;
		result.pendingBurstGain = scratch.pendingBurstGain_;
		result.hasComparison = scratch.hasComparison_;
		result.comparisonBirthsEnabled = scratch.comparisonBirthsEnabled_;
		result.firstLanePending = scratch.firstLanePending_;
		result.firstLaneScheduled = scratch.firstLaneScheduled_;
		result.releaseSelectionActive = scratch.releaseSelectionActive_;
		result.valid = true;
		if (error) error->clear();
		return true;
	}

	void ReconstructedSynthesizer::installPreparedSeekState(const PreparedAudioSeekState& state)
	{
		if (!state.valid || !state.timeline || !state.timeline->ready()) return;
		timeline_ = state.timeline;
		transport_ = state.transport;
		transportRevision_ = state.transportRevision;
		timelineFrame_ = state.targetFrame;
		timelineCursor_ = state.targetCursor;
		timelineAnchorSample_ = state.timelineAnchorSample;
		timelineAnchorFrame_ = state.timelineAnchorFrame;
		noteSources_[0].restore(state.notes[0]);
		noteSources_[1].restore(state.notes[1]);
		fir_.restore(state.firHistory, state.firPosition);
		sampleClock_ = state.sampleClock;
		nextClockFrame_ = state.nextClockFrame;
		firstLaneFrame_ = state.firstLaneFrame;
		clockRemainder_ = state.clockRemainder;
		currentMaster_ = state.currentMaster;
		targetMaster_ = state.targetMaster;
		idleFadeRemaining_ = state.idleFadeRemaining;
		idleFadeLength_ = state.idleFadeLength;
		smoothedCutoff_ = state.smoothedCutoff;
		filterB0_ = state.filterB0; filterB1_ = state.filterB1; filterB2_ = state.filterB2;
		filterA1_ = state.filterA1; filterA2_ = state.filterA2;
		filterX1_ = state.filterX1; filterX2_ = state.filterX2;
		filterY1_ = state.filterY1; filterY2_ = state.filterY2;
		filterControlCountdown_ = state.filterControlCountdown;
		comparison_ = state.comparison;
		pendingComparison_ = state.pendingComparison;
		pendingParameters_ = state.pendingParameters;
		pendingBurstGain_ = state.pendingBurstGain;
		hasComparison_ = state.hasComparison;
		comparisonBirthsEnabled_ = state.comparisonBirthsEnabled;
		firstLanePending_ = state.firstLanePending;
		firstLaneScheduled_ = state.firstLaneScheduled;
		releaseSelectionActive_ = state.releaseSelectionActive;
		resamplerRing_.fill(0.0f);
		resamplerBaseFrame_ = resamplerGeneratedFrames_ = 0;
		resamplerSourcePosition_ = 0.0;
		resamplerDeviceRate_ = 44100;
		overloadLatched_ = false;
	}

	std::uint64_t ReconstructedSynthesizer::timelineFrameAtSample(std::uint64_t sample) const
	{
		if (!timeline_) return 0;
		const std::uint64_t delta = sample > timelineAnchorSample_ ? sample - timelineAnchorSample_ : 0;
		return (std::min)(timeline_->durationFrames(), timelineAnchorFrame_ + delta);
	}

	void ReconstructedSynthesizer::updateTimelineSelection()
	{
		if (!timeline_ || transport_.stopAtEnd)
		{
			stopBirths();
			return;
		}
		AudioTimelineEvent selected;
		bool found = false;
		if (transport_.playing)
		{
			const std::uint64_t targetFrame = timelineFrameAtSample(nextClockFrame_);
			if (targetFrame >= timeline_->durationFrames())
			{
				timelineFrame_ = timeline_->durationFrames();
				timelineCursor_ = timeline_->sourceEventCount();
				stopBirths();
				return;
			}
			timelineFrame_ = targetFrame;
			timelineCursor_ = timeline_->cursorAtFrame(targetFrame);
			found = timeline_->comparisonAtFrame(targetFrame, selected);
		}
		else if (transport_.holdSelection)
			found = timeline_->comparisonAtCursor(timelineCursor_, selected);
		else if (releaseSelectionActive_ && comparisonBirthsEnabled_ && hasComparison_)
			return;
		else
		{
			stopBirths();
			return;
		}
		if (!found)
		{
			stopBirths();
			return;
		}
		selectTimelineEvent(selected);
	}

	void ReconstructedSynthesizer::selectTimelineEvent(const AudioTimelineEvent& selected)
	{
		BurstComparison comparison;
		comparison.firstPosition = selected.firstPosition;
		comparison.secondPosition = selected.secondPosition;
		comparison.firstRole = selected.firstRole;
		comparison.secondRole = selected.secondRole;
		comparison.firstGain = selected.firstGain;
		comparison.secondGain = selected.secondGain;
		comparison.probeOnly = selected.kind == TimelineEventKind::Probe;
		if (comparison.probeOnly)
		{
			comparison.firstGain *= timelineGains_.probeGain;
			comparison.secondGain = 0.0;
		}
		else
		{
			comparison.firstGain *= timelineGains_.comparisonGain;
			comparison.secondGain *= timelineGains_.comparisonGain;
			if (comparison.firstRole < AudioTimelineRoleCount)
				comparison.firstGain *= timelineGains_.roleGain[comparison.firstRole];
			if (comparison.secondRole < AudioTimelineRoleCount)
				comparison.secondGain *= timelineGains_.roleGain[comparison.secondRole];
			if (comparison.firstRole == 1) comparison.firstGain *= timelineGains_.heldKeyGain;
			if (comparison.secondRole == 1) comparison.secondGain *= timelineGains_.heldKeyGain;
		}
		setComparison(comparison);
	}

	AudioPlayheadFeedback ReconstructedSynthesizer::playheadFeedback() const
	{
		AudioPlayheadFeedback feedback;
		feedback.generation = transport_.generation;
		feedback.transportRevision = transportRevision_;
		feedback.durationFrames = timeline_ ? timeline_->durationFrames() : 0;
		feedback.timelineFrame = timeline_ && transport_.playing ? timelineFrameAtSample(sampleClock_) : timelineFrame_;
		feedback.sourceCursor = timeline_ && transport_.playing
			? timeline_->cursorAtFrame(feedback.timelineFrame) : timelineCursor_;
		feedback.playing = timeline_ && transport_.playing && feedback.timelineFrame < feedback.durationFrames;
		return feedback;
	}

	void ReconstructedSynthesizer::clearComparison()
	{
		if (parameters_.idlePolicy == ReconstructedIdlePolicy::ReleaseSelection && hasComparison_)
		{
			idleFadeLength_ = parameters_.specialReleaseMilliseconds * SampleRate / 1000.0;
			idleFadeRemaining_ = idleFadeLength_;
			comparisonBirthsEnabled_ = idleFadeLength_ > 0.0;
			releaseSelectionActive_ = idleFadeLength_ > 0.0;
			if (!comparisonBirthsEnabled_)
				hasComparison_ = false;
		}
		else
		{
			comparisonBirthsEnabled_ = hasComparison_ && parameters_.idlePolicy == ReconstructedIdlePolicy::HoldSelection;
			releaseSelectionActive_ = false;
		}
	}

	void ReconstructedSynthesizer::stopBirths()
	{
		comparisonBirthsEnabled_ = false;
		hasComparison_ = false;
		firstLanePending_ = false;
		firstLaneScheduled_ = false;
		releaseSelectionActive_ = false;
		idleFadeRemaining_ = idleFadeLength_ = 0.0;
	}

	void ReconstructedSynthesizer::setMasterLevel(double level)
	{
		targetMaster_ = clamp(level, 0.0, 1.0);
	}

	bool ReconstructedSynthesizer::launchNote(std::size_t voice, double startOffsetSamples, double gain,
		const BurstComparison& comparison, const ReconstructedParameters& parameters)
	{
		const double position = voice == 0 ? comparison.firstPosition : comparison.secondPosition;
		double frequency = parameters.minimumFrequencyHz + position *
			(parameters.maximumFrequencyHz - parameters.minimumFrequencyHz);
		if (parameters.mapping == ReconstructedMapping::LogFrequency)
			frequency = parameters.minimumFrequencyHz * std::pow(parameters.maximumFrequencyHz / parameters.minimumFrequencyHz, position);
		const double fraction = clamp(startOffsetSamples - std::floor(startOffsetSamples), 0.0, 0.999999999);
		if (noteSources_[voice].trigger(frequency, parameters.carrierCycles, parameters.tukeyFraction, fraction, gain))
			return true;
		++overloadCount_;
		overloadLatched_ = true;
		return false;
	}

	void ReconstructedSynthesizer::launchTick()
	{
		if (!comparisonBirthsEnabled_ || !hasComparison_) { firstLanePending_ = false; return; }
		double gain = 1.0;
		if (idleFadeRemaining_ > 0.0 && idleFadeLength_ > 0.0)
		{
			const double progress = clamp(1.0 - idleFadeRemaining_ / idleFadeLength_, 0.0, 1.0);
			gain = 0.5 * (1.0 + std::cos(Pi * progress));
		}
		pendingComparison_ = comparison_;
		pendingParameters_ = parameters_;
		pendingBurstGain_ = gain;
		const double delayFrames = pendingParameters_.secondOperandDelayMicroseconds * SampleRate / 1000000.0;
		launchNote(0, -delayFrames, gain * pendingComparison_.firstGain, pendingComparison_, pendingParameters_);
		firstLanePending_ = true;
	}

	double ReconstructedSynthesizer::filterSample(double sample)
	{
		const double filtered = filterB0_ * sample + filterB1_ * filterX1_ + filterB2_ * filterX2_ -
			filterA1_ * filterY1_ - filterA2_ * filterY2_;
		filterX2_ = filterX1_; filterX1_ = sample;
		filterY2_ = filterY1_; filterY1_ = filtered;
		return parameters_.lowPassEnabled ? filtered : sample;
	}

	void ReconstructedSynthesizer::updateFilterCoefficients()
	{
		const double omega = 2.0 * Pi * clamp(smoothedCutoff_, 1.0, SampleRate * 0.499) / SampleRate;
		const double cosine = std::cos(omega);
		const double sine = std::sin(omega);
		const double alpha = sine * 0.7071067811865475244; // Butterworth Q = 1/sqrt(2).
		const double a0 = 1.0 + alpha;
		filterB0_ = (1.0 - cosine) * 0.5 / a0;
		filterB1_ = (1.0 - cosine) / a0;
		filterB2_ = filterB0_;
		filterA1_ = -2.0 * cosine / a0;
		filterA2_ = (1.0 - alpha) / a0;
	}

	void ReconstructedSynthesizer::render(float* output, std::size_t frames)
	{
		if (!output) return;
		for (std::size_t frame = 0; frame < frames; ++frame)
		{
			if (timeline_ && transport_.playing && timelineFrameAtSample(sampleClock_) >= timeline_->durationFrames())
			{
				timelineFrame_ = timeline_->durationFrames();
				timelineCursor_ = timeline_->sourceEventCount();
				transport_.playing = false;
				transport_.holdSelection = false;
				stopBirths();
			}
			if (firstLaneScheduled_ && sampleClock_ >= firstLaneFrame_ &&
				(sampleClock_ < nextClockFrame_ || (sampleClock_ == nextClockFrame_ && firstLaneFrame_ == nextClockFrame_)))
			{
				firstLaneScheduled_ = false;
				if (timeline_) updateTimelineSelection();
				launchTick();
			}
			if (sampleClock_ == nextClockFrame_)
			{
				if (firstLanePending_)
				{
					launchNote(1, 0.0, pendingBurstGain_ * pendingComparison_.secondGain, pendingComparison_, pendingParameters_);
					firstLanePending_ = false;
				}
				clockRemainder_ += SampleRate / parameters_.excitationClockHz;
				const std::uint64_t interval = static_cast<std::uint64_t>(std::max(1.0, std::floor(clockRemainder_)));
				clockRemainder_ -= static_cast<double>(interval);
				nextClockFrame_ += interval;
				const double delayFrames = parameters_.secondOperandDelayMicroseconds * SampleRate / 1000000.0;
				const std::uint64_t earlyLead = static_cast<std::uint64_t>(std::ceil(delayFrames));
				firstLaneFrame_ = nextClockFrame_ >= earlyLead ? nextClockFrame_ - earlyLead : sampleClock_ + interval;
				firstLaneScheduled_ = true;
			}

			double mono = noteSources_[0].sample() + noteSources_[1].sample();
			if (idleFadeRemaining_ > 0.0)
			{
				--idleFadeRemaining_;
				if (idleFadeRemaining_ <= 0.0 && parameters_.idlePolicy == ReconstructedIdlePolicy::ReleaseSelection)
				{
					idleFadeRemaining_ = 0.0;
					releaseSelectionActive_ = false;
					hasComparison_ = false;
					comparisonBirthsEnabled_ = false;
				}
			}

			double filtered = fir_.sample(mono);

			const double alpha = std::exp(-1.0 / (0.010 * SampleRate));
			currentMaster_ = alpha * currentMaster_ + (1.0 - alpha) * targetMaster_;
			const double cutoffAlpha = std::exp(-1.0 / (0.010 * SampleRate));
			smoothedCutoff_ = cutoffAlpha * smoothedCutoff_ + (1.0 - cutoffAlpha) * parameters_.lowPassCutoffHz;
			if (filterControlCountdown_ == 0)
			{
				updateFilterCoefficients();
				filterControlCountdown_ = 31;
			}
			else --filterControlCountdown_;
			filtered = filterSample(filtered) * currentMaster_;
			if (!parameters_.audioEnabled) filtered = 0.0;
			if (!std::isfinite(filtered)) filtered = 0.0;
			output[frame * 2] = static_cast<float>(filtered);
			output[frame * 2 + 1] = static_cast<float>(filtered);
			++sampleClock_;
		}
	}

	void ReconstructedSynthesizer::renderAtSampleRate(float* output, std::size_t frames, std::uint32_t deviceSampleRate)
	{
		renderAtSampleRateImpl(output, frames, deviceSampleRate, true);
	}

	void ReconstructedSynthesizer::renderAtSampleRateLinear(float* output, std::size_t frames, std::uint32_t deviceSampleRate)
	{
		renderAtSampleRateImpl(output, frames, deviceSampleRate, false);
	}

	void ReconstructedSynthesizer::renderAtSampleRateImpl(float* output, std::size_t frames,
		std::uint32_t deviceSampleRate, bool applySafetyClamp)
	{
		if (!output || frames == 0) return;
		if (deviceSampleRate == 44100)
		{
			resamplerDeviceRate_ = 44100;
			resamplerBaseFrame_ = resamplerGeneratedFrames_ = 0;
			resamplerSourcePosition_ = 0.0;
			render(output, frames);
			if (applySafetyClamp) applyOutputSafetyClamp(output, frames);
			return;
		}
		if (deviceSampleRate == 0)
		{
			std::fill(output, output + frames * 2, 0.0f);
			return;
		}
		if (resamplerDeviceRate_ != deviceSampleRate)
		{
			resamplerDeviceRate_ = deviceSampleRate;
			resamplerBaseFrame_ = resamplerGeneratedFrames_ = 0;
			resamplerSourcePosition_ = 0.0;
		}

		const double sourceFramesPerOutputFrame = 44100.0 / static_cast<double>(deviceSampleRate);
		const double antiAliasCutoff = (std::min)(1.0, static_cast<double>(deviceSampleRate) / 44100.0);
		const ResamplerCoefficientBank& coefficientBank = resamplerCoefficientBank();
		const std::size_t cutoffIndex = resamplerCutoffIndex(antiAliasCutoff);
		const std::array<float, ResamplerPhaseCount * ResamplerTapCount>& coefficients = coefficientBank.coefficients[cutoffIndex];
		std::size_t outputFrame = 0;
		while (outputFrame < frames)
		{
			const std::size_t capacityBound = static_cast<std::size_t>((ResamplerCapacity - 32) /
				(std::max)(1.0, sourceFramesPerOutputFrame));
			const std::size_t chunkFrames = (std::min)((std::min)(static_cast<std::size_t>(64),
				(std::max)(static_cast<std::size_t>(1), capacityBound)), frames - outputFrame);
			const std::uint64_t firstTargetFrame = static_cast<std::uint64_t>(std::floor(resamplerSourcePosition_));
			const double finalSourcePosition = resamplerSourcePosition_ +
				sourceFramesPerOutputFrame * static_cast<double>(chunkFrames - 1);
			const std::uint64_t requiredEnd = static_cast<std::uint64_t>(std::floor(finalSourcePosition)) + 1;
			// Keep the FIR causal: it uses the current source frame and the fifteen
			// preceding frames. A centered non-causal kernel needs look-ahead and
			// advances the synthesis/playhead clock beyond the requested device time.
			resamplerBaseFrame_ = firstTargetFrame > 15 ? firstTargetFrame - 15 : 0;
			while (resamplerGeneratedFrames_ < requiredEnd)
			{
				if (resamplerGeneratedFrames_ > resamplerBaseFrame_ &&
					resamplerGeneratedFrames_ - resamplerBaseFrame_ >= ResamplerCapacity)
				{
					// The ratio is bounded by practical device rates; this is a defensive
					// guard that keeps the callback allocation-free if a driver lies.
					resamplerBaseFrame_ = resamplerGeneratedFrames_ - ResamplerCapacity + 1;
				}
				const std::uint64_t missing = requiredEnd - resamplerGeneratedFrames_;
				const std::size_t count = static_cast<std::size_t>((std::min<std::uint64_t>)(missing, resamplerScratch_.size() / 2));
				render(resamplerScratch_.data(), count);
				for (std::size_t source = 0; source < count; ++source)
				{
					const std::size_t slot = static_cast<std::size_t>((resamplerGeneratedFrames_ + source) % ResamplerCapacity) * 2;
					resamplerRing_[slot] = resamplerScratch_[source * 2];
					resamplerRing_[slot + 1] = resamplerScratch_[source * 2 + 1];
				}
				resamplerGeneratedFrames_ += count;
			}
			for (std::size_t chunkFrame = 0; chunkFrame < chunkFrames; ++chunkFrame)
			{
				const std::uint64_t targetFrame = static_cast<std::uint64_t>(std::floor(resamplerSourcePosition_));
				resamplerBaseFrame_ = targetFrame > 15 ? targetFrame - 15 : 0;
				const double fraction = resamplerSourcePosition_ - static_cast<double>(targetFrame);
				const std::size_t phase = (std::min)(ResamplerPhaseCount - 1,
					static_cast<std::size_t>(fraction * static_cast<double>(ResamplerPhaseCount)));
				const std::size_t coefficientOffset = phase * ResamplerTapCount;
				for (std::size_t channel = 0; channel < 2; ++channel)
				{
					double sample = 0.0;
					for (int tap = 0; tap < 16; ++tap)
					{
						const std::int64_t sourceFrame = static_cast<std::int64_t>(targetFrame) + tap - 15;
						if (sourceFrame < 0 || static_cast<std::uint64_t>(sourceFrame) < resamplerBaseFrame_ ||
							static_cast<std::uint64_t>(sourceFrame) >= resamplerGeneratedFrames_) continue;
						const std::size_t slot = static_cast<std::size_t>(static_cast<std::uint64_t>(sourceFrame) % ResamplerCapacity) * 2 + channel;
						sample += coefficients[coefficientOffset + static_cast<std::size_t>(tap)] * resamplerRing_[slot];
					}
					output[(outputFrame + chunkFrame) * 2 + channel] = static_cast<float>(sample);
				}
				resamplerSourcePosition_ += sourceFramesPerOutputFrame;
			}
			outputFrame += chunkFrames;
		}
		if (applySafetyClamp) applyOutputSafetyClamp(output, frames);
	}

	void ReconstructedSynthesizer::applyOutputSafetyClamp(float* output, std::size_t frames)
	{
		for (std::size_t sample = 0; sample < frames * 2; ++sample)
		{
			const float value = output[sample];
			if (!std::isfinite(value) || std::abs(value) > 0.999f)
			{
				if (!overloadLatched_) ++overloadCount_;
				overloadLatched_ = true;
				output[sample] = static_cast<float>(clamp(std::isfinite(value) ? value : 0.0, -0.999, 0.999));
			}
			else overloadLatched_ = false;
		}
	}

	void ReconstructedSynthesizer::reset()
	{
		for (std::size_t voice = 0; voice < noteSources_.size(); ++voice) noteSources_[voice].reset();
		fir_.reset();
		resamplerRing_.fill(0.0f);
		resamplerBaseFrame_ = resamplerGeneratedFrames_ = 0;
		resamplerSourcePosition_ = 0.0;
		resamplerDeviceRate_ = 44100;
		sampleClock_ = 0;
		nextClockFrame_ = 128;
		firstLaneFrame_ = 128;
		firstLaneScheduled_ = true;
		clockRemainder_ = 0.0;
		filterX1_ = filterX2_ = filterY1_ = filterY2_ = 0.0;
		filterControlCountdown_ = 0;
		idleFadeRemaining_ = idleFadeLength_ = 0.0;
		hasComparison_ = comparisonBirthsEnabled_ = firstLanePending_ = false;
		releaseSelectionActive_ = false;
		pendingBurstGain_ = 1.0;
		currentMaster_ = targetMaster_ = parameters_.masterLevel;
		timelineAnchorSample_ = 128;
		timelineAnchorFrame_ = 0;
		timelineFrame_ = timelineCursor_ = 0;
		transport_ = AudioTransportState();
		transportRevision_ = 0;
		smoothedCutoff_ = parameters_.lowPassCutoffHz;
		updateFilterCoefficients();
	}

	std::size_t ReconstructedSynthesizer::activeNoteCount(std::size_t voice) const
	{
		if (voice >= noteSources_.size()) return 0;
		return noteSources_[voice].activeCount();
	}
}
