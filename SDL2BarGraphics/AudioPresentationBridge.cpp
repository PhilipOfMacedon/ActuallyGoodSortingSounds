#include "AudioPresentationBridge.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace
{
	bool sameParameters(const SoundEngine::Parameters& a, const SoundEngine::Parameters& b)
	{
		if (a.masterLevel != b.masterLevel || a.bedLevel != b.bedLevel || a.bedHarmonicExponent != b.bedHarmonicExponent ||
			a.includeSub300HzBed != b.includeSub300HzBed || a.bedComb.enabled != b.bedComb.enabled ||
			a.bedComb.applyPhase != b.bedComb.applyPhase || a.bedComb.delayMicroseconds != b.bedComb.delayMicroseconds ||
			a.bedComb.phaseOffsetDegrees != b.bedComb.phaseOffsetDegrees || a.bedComb.wetAtLowFrequency != b.bedComb.wetAtLowFrequency ||
			a.bedComb.wetAtHighFrequency != b.bedComb.wetAtHighFrequency || a.bedComb.fadeStartHz != b.bedComb.fadeStartHz ||
			a.bedComb.fadeEndHz != b.bedComb.fadeEndHz || a.bedComb.fadeExponent != b.bedComb.fadeExponent ||
			a.comparisonBoostDb != b.comparisonBoostDb || a.heldKeyBoostDb != b.heldKeyBoostDb ||
			a.tauBelowBins != b.tauBelowBins || a.tauAboveBins != b.tauAboveBins || a.chorusMix != b.chorusMix ||
			a.chorusRateHz != b.chorusRateHz || a.chorusDepthMs != b.chorusDepthMs || a.chorusWeighting != b.chorusWeighting ||
			a.attackMs != b.attackMs || a.releaseMs != b.releaseMs || a.mappingMode != b.mappingMode ||
			a.comparisonSpreadMode != b.comparisonSpreadMode || a.enabled != b.enabled)
			return false;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			if (a.bandTrimDb[i] != b.bandTrimDb[i]) return false;
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
			if (a.roleBoostDb[i] != b.roleBoostDb[i]) return false;
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
			if (a.accentRouting[i] != b.accentRouting[i] || a.accentGain[i] != b.accentGain[i]) return false;
		return true;
	}

	bool sameComparison(const SoundEngine::AudioComparison& a, const SoundEngine::AudioComparison& b)
	{
		return a.firstPosition == b.firstPosition && a.secondPosition == b.secondPosition && a.heldKey == b.heldKey &&
			a.firstRole == b.firstRole && a.secondRole == b.secondRole;
	}

	bool sameEventAccents(const SoundEngine::EventAccentSummary& a, const SoundEngine::EventAccentSummary& b)
	{
		return a.counts == b.counts && a.representativePositions == b.representativePositions;
	}

	bool sameReconstructedParameters(const SoundEngine::ReconstructedParameters& a, const SoundEngine::ReconstructedParameters& b)
	{
		return a.minimumFrequencyHz == b.minimumFrequencyHz && a.maximumFrequencyHz == b.maximumFrequencyHz &&
			a.excitationClockHz == b.excitationClockHz && a.carrierCycles == b.carrierCycles &&
			a.tukeyFraction == b.tukeyFraction && a.secondOperandDelayMicroseconds == b.secondOperandDelayMicroseconds &&
			a.masterLevel == b.masterLevel && a.specialReleaseMilliseconds == b.specialReleaseMilliseconds &&
			a.lowPassCutoffHz == b.lowPassCutoffHz && a.audioEnabled == b.audioEnabled &&
			a.lowPassEnabled == b.lowPassEnabled && a.mapping == b.mapping && a.idlePolicy == b.idlePolicy;
	}

	bool sameTimelineGains(const SoundEngine::AudioTimelineGainSettings& a, const SoundEngine::AudioTimelineGainSettings& b)
	{
		if (a.comparisonGain != b.comparisonGain || a.heldKeyGain != b.heldKeyGain || a.probeGain != b.probeGain) return false;
		for (std::size_t i = 0; i < SoundEngine::AudioTimelineRoleCount; ++i)
			if (a.roleGain[i] != b.roleGain[i]) return false;
		return true;
	}

	void addAccent(SoundEngine::EventAccentSummary& summary, SoundEngine::AccentCategory category, float position)
	{
		const std::size_t index = static_cast<std::size_t>(category);
		if (index >= SoundEngine::AccentCategoryCount) return;
		if (summary.counts[index] < 255) ++summary.counts[index];
		summary.representativePositions[index] = std::max(0.0f, std::min(1.0f, position));
	}

	float gainFromDecibels(float decibels)
	{
		return decibels <= -60.0f ? 0.0f : static_cast<float>(std::pow(10.0, static_cast<double>(decibels) / 20.0));
	}

	SoundEngine::ComparisonRole audioRole(sortinglogic::SemanticRole role)
	{
		switch (role)
		{
		case sortinglogic::SemanticRole::HeldKey: return SoundEngine::ComparisonRole::HeldKey;
		case sortinglogic::SemanticRole::Pivot: return SoundEngine::ComparisonRole::Pivot;
		case sortinglogic::SemanticRole::CurrentMinimum: return SoundEngine::ComparisonRole::CurrentMinimum;
		case sortinglogic::SemanticRole::CurrentMaximum: return SoundEngine::ComparisonRole::CurrentMaximum;
		case sortinglogic::SemanticRole::WriteTarget: return SoundEngine::ComparisonRole::WriteTarget;
		case sortinglogic::SemanticRole::ProbedItem: return SoundEngine::ComparisonRole::ProbedItem;
		case sortinglogic::SemanticRole::ProcessedItem: return SoundEngine::ComparisonRole::ProcessedItem;
		default: return SoundEngine::ComparisonRole::None;
		}
	}

	std::unique_ptr<SoundEngine::PreparedAudioTimeline> buildPreparedTimeline(
		const sortinglogic::SortTrace& trace, double durationSeconds,
		SoundEngine::PlaybackTimingMode timingMode, const SoundEngine::Parameters& parameters,
		std::string& error)
	{
		(void)parameters;
		const bool hasComparisons = !trace.comparisonEventIndices.empty();
		std::map<std::uint64_t, double> normalizedById;
		double minimum = trace.input.empty() ? 0.0 : trace.input.front().value;
		double maximum = minimum;
		for (std::size_t i = 0; i < trace.input.size(); ++i)
		{
			minimum = (std::min)(minimum, static_cast<double>(trace.input[i].value));
			maximum = (std::max)(maximum, static_cast<double>(trace.input[i].value));
		}
		for (std::size_t i = 0; i < trace.input.size(); ++i)
		{
			const double value = trace.input[i].value;
			normalizedById[trace.input[i].stableId] = maximum == minimum ? 0.5 : (value - minimum) / (maximum - minimum);
		}
		const sortinglogic::AlgorithmDescriptor* descriptor =
			sortinglogic::algorithmDescriptorById(trace.familyId, trace.variantId);
		std::vector<SoundEngine::AudioTimelineInputEvent> events(trace.events.size());
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const sortinglogic::SemanticEvent& source = trace.events[i];
			SoundEngine::AudioTimelineInputEvent& event = events[i];
			event.sourceCursor = i + 1;
			if (source.type == sortinglogic::SemanticEventType::Compare && hasComparisons)
			{
				const auto first = normalizedById.find(source.firstItemId);
				const auto second = normalizedById.find(source.secondItemId);
				if (first == normalizedById.end() || second == normalizedById.end()) continue;
				event.kind = SoundEngine::TimelineEventKind::Comparison;
				event.firstPosition = first->second;
				event.secondPosition = second->second;
				event.firstRole = static_cast<std::uint8_t>(audioRole(source.firstRole));
				event.secondRole = static_cast<std::uint8_t>(audioRole(source.secondRole));
				if (descriptor && descriptor->reverseAudioOperandOrder)
				{
					std::swap(event.firstPosition, event.secondPosition);
					std::swap(event.firstRole, event.secondRole);
				}
				event.firstGain = event.secondGain = 1.0;
			}
			else if (!hasComparisons && source.type == sortinglogic::SemanticEventType::Probe)
			{
				const auto item = normalizedById.find(source.firstItemId);
				if (item == normalizedById.end()) continue;
				event.kind = SoundEngine::TimelineEventKind::Probe;
				event.firstPosition = event.secondPosition = item->second;
				event.firstRole = static_cast<std::uint8_t>(SoundEngine::ComparisonRole::ProbedItem);
				event.firstGain = 1.0;
				event.secondGain = 0.0;
			}
		}
		std::unique_ptr<SoundEngine::PreparedAudioTimeline> timeline(new SoundEngine::PreparedAudioTimeline());
		if (!timeline->prepare(events, trace.events.size(), timingMode, durationSeconds, 44100, 300, &error))
			return std::unique_ptr<SoundEngine::PreparedAudioTimeline>();
		return timeline;
	}
}

LatestAudioStateMailbox::LatestAudioStateMailbox()
	: slots_(), middle_(1), producerIndex_(0), consumerIndex_(2)
{
}

void LatestAudioStateMailbox::publish(const AudioPresentationState& state)
{
	slots_[producerIndex_] = state;
	const unsigned int previous = middle_.exchange(producerIndex_ | DirtyBit, std::memory_order_acq_rel);
	producerIndex_ = previous & IndexMask;
}

bool LatestAudioStateMailbox::consumeLatest(AudioPresentationState& state)
{
	const unsigned int received = middle_.exchange(consumerIndex_, std::memory_order_acq_rel);
	if ((received & DirtyBit) == 0)
		return false;
	consumerIndex_ = received & IndexMask;
	state = slots_[consumerIndex_];
	return true;
}

AudioPresentationBridge::AudioPresentationBridge()
	: lastTrace_(nullptr), lastCursor_(0), lastComparison_(), lastEventAccents_(), hasComparison_(false), lastComparisonIsProbe_(false), valuesById_(),
	  workspaceSizes_(), minimumValue_(0.0f), maximumValue_(0.0f), lastPublished_(), hasPublished_(false), revision_(0),
	  timelines_(), currentTimeline_(nullptr), timelineGeneration_(0), seekRevision_(0), audioParameterRevision_(1),
	  transportRevision_(0), lastTransportCursor_(0),
	  lastTimelineDurationSeconds_(-1.0), lastTimingMode_(SoundEngine::PlaybackTimingMode::FixedDuration),
	  currentSeekState_(nullptr), seekStates_(),
	  timelineBuildMutex_(), timelineBuildCondition_(), pendingTimelineBuild_(), completedTimelineBuild_(),
	  requestedTimelineGeneration_(0), hasPendingTimelineBuild_(false), hasCompletedTimelineBuild_(false),
	  stopTimelineWorker_(false), lastAudioClockAvailable_(false), seekPreparing_(false), requestedSeekRevision_(0),
	  timelinePreparing_(false), timelineError_(), timelineWorker_()
{
	timelineWorker_ = std::thread(&AudioPresentationBridge::timelineWorkerLoop, this);
}

AudioPresentationBridge::~AudioPresentationBridge()
{
	{
		std::lock_guard<std::mutex> lock(timelineBuildMutex_);
		stopTimelineWorker_ = true;
		hasPendingTimelineBuild_ = false;
	}
	timelineBuildCondition_.notify_one();
	if (timelineWorker_.joinable()) timelineWorker_.join();
}

void AudioPresentationBridge::update(const sortinglogic::Simulation& simulation,
	const sortinglogic::RenderSnapshot& snapshot, const SoundEngine::Parameters& parameters,
	LatestAudioStateMailbox& mailbox, const SoundEngine::ReconstructedParameters& reconstructedParameters,
	SoundEngine::PlaybackTimingMode timingMode, bool audioClockAvailable)
{
	pollTimelineBuild();
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation.currentTrace();
	const sortinglogic::SortTrace* rawTrace = trace.get();
	const bool traceChanged = rawTrace != lastTrace_;
	if (traceChanged)
	{
		valuesById_.clear();
		workspaceSizes_.clear();
		minimumValue_ = 0.0f;
		maximumValue_ = 0.0f;
		if (rawTrace && !rawTrace->input.empty())
		{
			minimumValue_ = rawTrace->input.front().value;
			maximumValue_ = minimumValue_;
			for (std::size_t i = 0; i < rawTrace->input.size(); ++i)
			{
				valuesById_[rawTrace->input[i].stableId] = rawTrace->input[i].value;
				minimumValue_ = std::min(minimumValue_, rawTrace->input[i].value);
				maximumValue_ = std::max(maximumValue_, rawTrace->input[i].value);
			}
			for (std::size_t i = 0; i < rawTrace->events.size(); ++i)
			{
				const sortinglogic::SemanticEvent& event = rawTrace->events[i];
				if ((event.type == sortinglogic::SemanticEventType::ResizeCounterWorkspace ||
					event.type == sortinglogic::SemanticEventType::ResizeAuxiliary) && !event.workspaceName.empty())
					workspaceSizes_[event.workspaceName] = event.afterSize;
			}
		}
		lastTrace_ = rawTrace;
		lastCursor_ = 0;
		lastComparison_ = SoundEngine::AudioComparison();
		lastEventAccents_ = SoundEngine::EventAccentSummary();
		hasComparison_ = false;
		lastComparisonIsProbe_ = false;
	}
	const bool rebuildTimeline = traceChanged || lastTimelineDurationSeconds_ != snapshot.durationSeconds ||
		lastTimingMode_ != timingMode;
	if (rebuildTimeline)
	{
		++timelineGeneration_;
		requestTimelineBuild(trace, snapshot.durationSeconds, timingMode, parameters);
		lastTimelineDurationSeconds_ = snapshot.durationSeconds;
		lastTimingMode_ = timingMode;
	}

	SoundEngine::EventAccentSummary crossedAccents;
	if (!rawTrace || !snapshot.hasTrace)
	{
		hasComparison_ = false;
		lastComparison_ = SoundEngine::AudioComparison();
		lastEventAccents_ = SoundEngine::EventAccentSummary();
		lastCursor_ = 0;
		lastComparisonIsProbe_ = false;
	}
	else if (snapshot.cursor < lastCursor_)
	{
		recomputeComparison(rawTrace, snapshot.cursor, lastComparison_, hasComparison_, lastComparisonIsProbe_);
		lastCursor_ = snapshot.cursor;
		lastEventAccents_ = SoundEngine::EventAccentSummary();
	}
	else
	{
		const std::size_t end = std::min(snapshot.cursor, rawTrace->events.size());
		for (std::size_t i = lastCursor_; i < end; ++i)
		{
			const sortinglogic::SemanticEvent& event = rawTrace->events[i];
			if (event.type == sortinglogic::SemanticEventType::Compare)
			{
				SoundEngine::AudioComparison comparison;
				if (makeComparison(*rawTrace, event, comparison))
				{
					lastComparison_ = comparison;
					hasComparison_ = true;
					lastComparisonIsProbe_ = false;
				}
			}
			else if (event.type == sortinglogic::SemanticEventType::Probe && rawTrace->comparisonEventIndices.empty())
			{
				SoundEngine::AudioComparison comparison;
				if (makeComparison(*rawTrace, event, comparison))
				{
					lastComparison_ = comparison;
					hasComparison_ = true;
					lastComparisonIsProbe_ = true;
				}
			}
			else
			{
				SoundEngine::AccentCategory category;
				bool routed = true;
				switch (event.type)
				{
				case sortinglogic::SemanticEventType::Probe: category = SoundEngine::AccentCategory::Probe; break;
				case sortinglogic::SemanticEventType::HoldKey:
				case sortinglogic::SemanticEventType::ShiftItem:
				case sortinglogic::SemanticEventType::PlaceKey:
				case sortinglogic::SemanticEventType::Swap:
				case sortinglogic::SemanticEventType::WriteItem:
				case sortinglogic::SemanticEventType::WriteRange: category = SoundEngine::AccentCategory::Movement; break;
				case sortinglogic::SemanticEventType::AuxiliaryItemWrite:
				case sortinglogic::SemanticEventType::CounterWrite:
				case sortinglogic::SemanticEventType::ResizeAuxiliary:
				case sortinglogic::SemanticEventType::ResizeCounterWorkspace: category = SoundEngine::AccentCategory::Distribution; break;
				case sortinglogic::SemanticEventType::DiscardItem: category = SoundEngine::AccentCategory::Discard; break;
				default: routed = false; category = SoundEngine::AccentCategory::Probe; break;
				}
				if (routed)
				{
					float position = 0.5f;
					const bool counterEvent = event.type == sortinglogic::SemanticEventType::CounterWrite ||
						event.type == sortinglogic::SemanticEventType::ResizeCounterWorkspace;
					if (counterEvent && event.firstIndex != sortinglogic::NoGapIndex)
					{
						std::map<std::string, std::size_t>::const_iterator workspace = workspaceSizes_.find(event.workspaceName);
						const std::size_t size = workspace == workspaceSizes_.end() ? event.afterSize : workspace->second;
						if (size > 1) position = static_cast<float>(event.firstIndex) / static_cast<float>(size - 1);
					}
					else
					{
						const std::map<std::uint64_t, float>::const_iterator item = valuesById_.find(event.firstItemId);
						if (item != valuesById_.end()) position = SoundEngine::normalizeValue(item->second, minimumValue_, maximumValue_);
						else if (event.firstIndex != sortinglogic::NoGapIndex && !rawTrace->input.empty())
							position = static_cast<float>(event.firstIndex) / static_cast<float>(std::max<std::size_t>(1, rawTrace->input.size() - 1));
					}
					addAccent(crossedAccents, category, position);
				}
			}
		}
		lastCursor_ = end;
		lastEventAccents_ = crossedAccents;
	}

	if (snapshot.cursor == 0 || snapshot.complete)
	{
		hasComparison_ = false;
		lastComparisonIsProbe_ = false;
	}

	AudioPresentationState current;
	current.parameters = parameters;
	current.reconstructedParameters = reconstructedParameters;
	current.timelineGains.comparisonGain = gainFromDecibels(parameters.comparisonBoostDb);
	current.timelineGains.heldKeyGain = gainFromDecibels(parameters.heldKeyBoostDb);
	const std::size_t probeCategory = static_cast<std::size_t>(SoundEngine::AccentCategory::Probe);
	current.timelineGains.probeGain = parameters.accentRouting[probeCategory] ? parameters.accentGain[probeCategory] : 0.0;
	for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
		current.timelineGains.roleGain[i] = gainFromDecibels(parameters.roleBoostDb[i]);
	current.eventAccents = lastEventAccents_;
	current.traceLoaded = rawTrace != nullptr && snapshot.hasTrace;
	current.hasComparison = current.traceLoaded && hasComparison_;
	current.transportPlaying = snapshot.playing;
	current.releaseSelection = current.hasComparison && !snapshot.playing &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
	current.comparison = lastComparison_;
	SoundEngine::AudioComparison burstOrder = lastComparison_;
	const sortinglogic::AlgorithmDescriptor* audioDescriptor = rawTrace
		? sortinglogic::algorithmDescriptorById(rawTrace->familyId, rawTrace->variantId) : nullptr;
	if (!lastComparisonIsProbe_ && audioDescriptor && audioDescriptor->reverseAudioOperandOrder)
	{
		std::swap(burstOrder.firstPosition, burstOrder.secondPosition);
		std::swap(burstOrder.firstRole, burstOrder.secondRole);
	}
	current.burstComparison.firstPosition = burstOrder.firstPosition;
	current.burstComparison.secondPosition = burstOrder.secondPosition;
	current.burstComparison.firstRole = static_cast<std::uint8_t>(burstOrder.firstRole);
	current.burstComparison.secondRole = static_cast<std::uint8_t>(burstOrder.secondRole);
	current.burstComparison.probeOnly = lastComparisonIsProbe_;
	if (lastComparisonIsProbe_)
	{
		const std::size_t probe = static_cast<std::size_t>(SoundEngine::AccentCategory::Probe);
		current.burstComparison.firstGain = parameters.accentRouting[probe] ? parameters.accentGain[probe] : 0.0f;
		current.burstComparison.secondGain = 0.0f;
	}
	else
	{
		const std::size_t firstRole = static_cast<std::size_t>(burstOrder.firstRole);
		const std::size_t secondRole = static_cast<std::size_t>(burstOrder.secondRole);
		const float firstRoleDb = firstRole < SoundEngine::RoleCount ? parameters.roleBoostDb[firstRole] : 0.0f;
		const float secondRoleDb = secondRole < SoundEngine::RoleCount ? parameters.roleBoostDb[secondRole] : 0.0f;
		const float firstHeldDb = burstOrder.firstRole == SoundEngine::ComparisonRole::HeldKey ? parameters.heldKeyBoostDb : 0.0f;
		const float secondHeldDb = burstOrder.secondRole == SoundEngine::ComparisonRole::HeldKey ? parameters.heldKeyBoostDb : 0.0f;
		current.burstComparison.firstGain = gainFromDecibels(parameters.comparisonBoostDb + firstRoleDb + firstHeldDb);
		current.burstComparison.secondGain = gainFromDecibels(parameters.comparisonBoostDb + secondRoleDb + secondHeldDb);
	}
	current.timeline = currentTimeline_;
	current.timelineGeneration = timelineGeneration_;
	current.transport.generation = timelineGeneration_;
	current.transport.sourceCursor = snapshot.cursor;
	current.transport.playing = snapshot.playing;
	current.transport.holdSelection = !snapshot.playing && current.hasComparison && !snapshot.complete &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::HoldSelection;
	current.transport.stopAtEnd = snapshot.complete || !current.traceLoaded;
	const bool playbackChanged = !hasPublished_ || snapshot.playing != lastPublished_.transportPlaying;
	const bool cursorChangedWhilePaused = !snapshot.playing && snapshot.cursor != lastTransportCursor_;
	const bool timelineInstallationChanged = currentTimeline_ != lastPublished_.timeline;
	const bool clockRecovery = audioClockAvailable && !lastAudioClockAvailable_;
	const bool audioSettingsChanged = hasPublished_ &&
		(!sameReconstructedParameters(current.reconstructedParameters, lastPublished_.reconstructedParameters) ||
		 !sameTimelineGains(current.timelineGains, lastPublished_.timelineGains));
	if (audioSettingsChanged) ++audioParameterRevision_;
	const bool transportChanged = rebuildTimeline || timelineInstallationChanged || playbackChanged || cursorChangedWhilePaused || clockRecovery;
	current.transport.releaseSelection = !snapshot.playing && (playbackChanged || cursorChangedWhilePaused) && current.hasComparison &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
	if (transportChanged)
	{
		++transportRevision_;
		current.transport.timelineFrame = currentTimeline_ ? currentTimeline_->frameForCursor(snapshot.cursor) : 0;
		current.transport.revision = transportRevision_;
	}
	else
	{
		current.transport.timelineFrame = lastPublished_.transport.timelineFrame;
		current.transport.revision = lastPublished_.transport.revision;
	}
	lastTransportCursor_ = snapshot.cursor;
	lastAudioClockAvailable_ = audioClockAvailable;
	if (seekPreparing_ && (!currentTimeline_ || snapshot.cursor == 0 || snapshot.complete))
	{
		std::lock_guard<std::mutex> lock(timelineBuildMutex_);
		++requestedSeekRevision_;
		hasPendingTimelineBuild_ = false;
		hasCompletedTimelineBuild_ = false;
		seekPreparing_ = false;
		timelinePreparing_ = false;
		currentSeekState_ = nullptr;
	}
	const bool seekRequired = currentTimeline_ && snapshot.cursor > 0 && !snapshot.complete &&
		(timelineInstallationChanged || cursorChangedWhilePaused || clockRecovery || (seekPreparing_ && audioSettingsChanged));
	if (seekRequired)
		requestSeekBuild(current.transport, reconstructedParameters, current.timelineGains);
	current.seekState = seekPreparing_ ? nullptr : currentSeekState_;
	current.seekRevision = current.seekState ? seekRevision_ : 0;
	if (seekPreparing_)
		current.transport.stopAtEnd = true;
	if (!hasPublished_ || current.traceLoaded != lastPublished_.traceLoaded ||
		current.hasComparison != lastPublished_.hasComparison ||
		current.transportPlaying != lastPublished_.transportPlaying ||
		current.releaseSelection != lastPublished_.releaseSelection ||
		!sameComparison(current.comparison, lastPublished_.comparison) ||
		current.burstComparison.firstPosition != lastPublished_.burstComparison.firstPosition ||
		current.burstComparison.secondPosition != lastPublished_.burstComparison.secondPosition ||
		current.burstComparison.firstGain != lastPublished_.burstComparison.firstGain ||
		current.burstComparison.secondGain != lastPublished_.burstComparison.secondGain ||
		current.burstComparison.probeOnly != lastPublished_.burstComparison.probeOnly ||
		current.timeline != lastPublished_.timeline || current.timelineGeneration != lastPublished_.timelineGeneration ||
		current.seekState != lastPublished_.seekState || current.seekRevision != lastPublished_.seekRevision ||
		current.transport.revision != lastPublished_.transport.revision ||
		!sameEventAccents(current.eventAccents, lastPublished_.eventAccents) ||
		!sameParameters(current.parameters, lastPublished_.parameters) ||
		!sameReconstructedParameters(current.reconstructedParameters, lastPublished_.reconstructedParameters))
	{
		current.revision = ++revision_;
		lastPublished_ = current;
		hasPublished_ = true;
		mailbox.publish(current);
	}
}

void AudioPresentationBridge::republish(LatestAudioStateMailbox& mailbox)
{
	if (!hasPublished_) return;
	lastPublished_.revision = ++revision_;
	mailbox.publish(lastPublished_);
}

void AudioPresentationBridge::collectRetiredTimelines(std::uint64_t acknowledgedGeneration, bool callbackAvailable,
	std::uint64_t acknowledgedSeekRevision)
{
	if (timelinePreparing_ || seekPreparing_) return;
	for (std::vector<std::unique_ptr<SoundEngine::PreparedAudioTimeline> >::iterator i = timelines_.begin(); i != timelines_.end();)
	{
		const bool current = i->get() == currentTimeline_;
		if (!current && (!callbackAvailable || acknowledgedGeneration >= timelineGeneration_))
			i = timelines_.erase(i);
		else ++i;
	}
	for (std::vector<std::unique_ptr<SoundEngine::PreparedAudioSeekState> >::iterator i = seekStates_.begin(); i != seekStates_.end();)
	{
		const bool current = i->get() == currentSeekState_;
		if (!current && (!callbackAvailable || acknowledgedSeekRevision >= seekRevision_))
			i = seekStates_.erase(i);
		else ++i;
	}
}

void AudioPresentationBridge::requestTimelineBuild(const std::shared_ptr<const sortinglogic::SortTrace>& trace,
	double durationSeconds, SoundEngine::PlaybackTimingMode timingMode, const SoundEngine::Parameters& parameters)
{
	currentTimeline_ = nullptr;
	currentSeekState_ = nullptr;
	timelineError_.clear();
	timelinePreparing_ = static_cast<bool>(trace);
	seekPreparing_ = false;
	std::lock_guard<std::mutex> lock(timelineBuildMutex_);
	++requestedSeekRevision_;
	pendingTimelineBuild_ = TimelineBuildRequest();
	requestedTimelineGeneration_ = timelineGeneration_;
	hasCompletedTimelineBuild_ = false;
	if (!trace)
	{
		hasPendingTimelineBuild_ = false;
		return;
	}
	pendingTimelineBuild_.trace = trace;
	pendingTimelineBuild_.prepareSeek = false;
	pendingTimelineBuild_.durationSeconds = durationSeconds;
	pendingTimelineBuild_.timingMode = timingMode;
	pendingTimelineBuild_.parameters = parameters;
	pendingTimelineBuild_.generation = timelineGeneration_;
	hasPendingTimelineBuild_ = true;
	timelineBuildCondition_.notify_one();
}

void AudioPresentationBridge::requestSeekBuild(const SoundEngine::AudioTransportState& transport,
	const SoundEngine::ReconstructedParameters& parameters, const SoundEngine::AudioTimelineGainSettings& gains)
{
	if (!currentTimeline_ || !currentTimeline_->ready()) return;
	seekPreparing_ = true;
	timelinePreparing_ = true;
	timelineError_.clear();
	currentSeekState_ = nullptr;
	std::lock_guard<std::mutex> lock(timelineBuildMutex_);
	const std::uint64_t requestRevision = ++requestedSeekRevision_;
	pendingTimelineBuild_ = TimelineBuildRequest();
	pendingTimelineBuild_.prepareSeek = true;
	pendingTimelineBuild_.timeline = currentTimeline_;
	pendingTimelineBuild_.reconstructedParameters = parameters;
	pendingTimelineBuild_.timelineGains = gains;
	pendingTimelineBuild_.transport = transport;
	pendingTimelineBuild_.parameterRevision = audioParameterRevision_;
	pendingTimelineBuild_.seekRevision = requestRevision;
	pendingTimelineBuild_.generation = timelineGeneration_;
	// The transport and seek revisions form the request's coalescing key.
	hasPendingTimelineBuild_ = true;
	hasCompletedTimelineBuild_ = false;
	requestedTimelineGeneration_ = timelineGeneration_;
	timelineBuildCondition_.notify_one();
}

void AudioPresentationBridge::pollTimelineBuild()
{
	TimelineBuildResult completed;
	{
		std::lock_guard<std::mutex> lock(timelineBuildMutex_);
		if (!hasCompletedTimelineBuild_) return;
		completed = std::move(completedTimelineBuild_);
		completedTimelineBuild_ = TimelineBuildResult();
		hasCompletedTimelineBuild_ = false;
	}
	if (completed.generation != timelineGeneration_) return;
	if (completed.preparedSeek)
	{
		if (completed.seekRevision != requestedSeekRevision_ ||
			completed.parameterRevision != audioParameterRevision_ ||
			completed.transportRevision != transportRevision_)
			return;
		seekPreparing_ = false;
		timelinePreparing_ = false;
		if (!completed.seekState)
		{
			currentSeekState_ = nullptr;
			timelineError_ = completed.error.empty() ? "Audio seek preparation failed." : completed.error;
			return;
		}
		timelineError_.clear();
		currentSeekState_ = completed.seekState.get();
		seekRevision_ = completed.seekRevision;
		seekStates_.push_back(std::move(completed.seekState));
		return;
	}
	timelinePreparing_ = false;
	if (!completed.timeline)
	{
		currentTimeline_ = nullptr;
		timelineError_ = completed.error.empty() ? "Audio timeline preparation failed." : completed.error;
		return;
	}
	timelineError_.clear();
	currentTimeline_ = completed.timeline.get();
	timelines_.push_back(std::move(completed.timeline));
}

void AudioPresentationBridge::timelineWorkerLoop()
{
	for (;;)
	{
		TimelineBuildRequest request;
		{
			std::unique_lock<std::mutex> lock(timelineBuildMutex_);
			timelineBuildCondition_.wait(lock, [this] { return stopTimelineWorker_ || hasPendingTimelineBuild_; });
			if (stopTimelineWorker_) return;
			request = std::move(pendingTimelineBuild_);
			pendingTimelineBuild_ = TimelineBuildRequest();
			hasPendingTimelineBuild_ = false;
		}
		TimelineBuildResult result;
		result.generation = request.generation;
		result.preparedSeek = request.prepareSeek;
		result.seekRevision = request.seekRevision;
		result.parameterRevision = request.parameterRevision;
		result.transportRevision = request.transport.revision;
		try
		{
			if (request.prepareSeek)
			{
				result.seekState.reset(new SoundEngine::PreparedAudioSeekState());
				if (!request.timeline || !SoundEngine::ReconstructedSynthesizer::prepareSeekState(*request.timeline,
					request.reconstructedParameters, request.timelineGains, request.transport,
					request.parameterRevision, *result.seekState, &result.error))
					result.seekState.reset();
			}
			else
				result.timeline = buildPreparedTimeline(*request.trace, request.durationSeconds, request.timingMode,
					request.parameters, result.error);
		}
		catch (const std::exception& exception)
		{
			result.error = exception.what();
		}
		catch (...)
		{
			result.error = "Audio timeline preparation failed with an unknown error.";
		}
		{
			std::lock_guard<std::mutex> lock(timelineBuildMutex_);
			if (stopTimelineWorker_) return;
			if (result.generation == requestedTimelineGeneration_ &&
				(!result.preparedSeek || result.seekRevision == requestedSeekRevision_))
			{
				completedTimelineBuild_ = std::move(result);
				hasCompletedTimelineBuild_ = true;
			}
		}
	}
}

void AudioPresentationBridge::recomputeComparison(const sortinglogic::SortTrace* trace, std::size_t cursor,
	SoundEngine::AudioComparison& comparison, bool& hasComparison, bool& isProbe) const
{
	hasComparison = false;
	isProbe = false;
	comparison = SoundEngine::AudioComparison();
	if (!trace) return;
	const std::size_t end = std::min(cursor, trace->events.size());
	const std::vector<std::size_t>& audioIndices = trace->comparisonEventIndices.empty()
		? trace->probeEventIndices : trace->comparisonEventIndices;
	std::vector<std::size_t>::const_iterator candidate = std::lower_bound(audioIndices.begin(), audioIndices.end(), end);
	while (candidate != audioIndices.begin())
	{
		--candidate;
		const std::size_t eventIndex = *candidate;
		if (eventIndex < trace->events.size())
		{
			SoundEngine::AudioComparison previous;
			if (makeComparison(*trace, trace->events[eventIndex], previous))
			{
				comparison = previous;
				hasComparison = true;
				isProbe = trace->events[eventIndex].type == sortinglogic::SemanticEventType::Probe;
				return;
			}
		}
	}
}

bool AudioPresentationBridge::makeComparison(const sortinglogic::SortTrace& trace,
	const sortinglogic::SemanticEvent& event, SoundEngine::AudioComparison& comparison) const
{
	(void)trace;
	if (event.type == sortinglogic::SemanticEventType::Probe)
	{
		const std::map<std::uint64_t, float>::const_iterator probed = valuesById_.find(event.firstItemId);
		if (probed == valuesById_.end()) return false;
		const float position = SoundEngine::normalizeValue(probed->second, minimumValue_, maximumValue_);
		comparison = SoundEngine::AudioComparison(position, position,
			SoundEngine::ComparisonRole::ProbedItem, SoundEngine::ComparisonRole::None);
		return true;
	}
	const std::map<std::uint64_t, float>::const_iterator first = valuesById_.find(event.firstItemId);
	const std::map<std::uint64_t, float>::const_iterator second = valuesById_.find(event.secondItemId);
	if (first == valuesById_.end() || second == valuesById_.end()) return false;
	comparison = SoundEngine::AudioComparison(
		SoundEngine::normalizeValue(first->second, minimumValue_, maximumValue_),
		SoundEngine::normalizeValue(second->second, minimumValue_, maximumValue_),
		audioRole(event.firstRole), audioRole(event.secondRole));
	return true;
}
