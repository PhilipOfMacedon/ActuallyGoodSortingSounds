#include "AudioPresentationBridge.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "PlaybackTimebase.h"

namespace
{
	bool sameComparison(const SoundEngine::AudioComparison& a, const SoundEngine::AudioComparison& b)
	{
		return a.firstPosition == b.firstPosition && a.secondPosition == b.secondPosition && a.heldKey == b.heldKey &&
			a.firstRole == b.firstRole && a.secondRole == b.secondRole;
	}

	bool sameEventAccents(const SoundEngine::EventAccentSummary& a, const SoundEngine::EventAccentSummary& b)
	{
		return a.counts == b.counts && a.representativePositions == b.representativePositions;
	}

	bool sameTimelineGains(const SoundEngine::AudioTimelineGainSettings& a, const SoundEngine::AudioTimelineGainSettings& b)
	{
		if (a.comparisonGain != b.comparisonGain || a.heldKeyGain != b.heldKeyGain || a.probeGain != b.probeGain ||
			a.auxiliaryGain != b.auxiliaryGain) return false;
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
		const sortinglogic::TraceAudioPresentationPolicy policy(trace);
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
		std::vector<SoundEngine::AudioTimelineInputEvent> events(trace.events.size());
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const sortinglogic::SemanticEvent& source = trace.events[i];
			SoundEngine::AudioTimelineInputEvent& event = events[i];
			event.sourceCursor = i + 1;
			const sortinglogic::TraceAudioEventKind kind = policy.classify(source);
			sortinglogic::TraceAudioOperandProjection operands;
			if (policy.projectOperands(source, operands))
			{
				const auto first = normalizedById.find(operands.firstItemId);
				if (first == normalizedById.end()) continue;
				if (operands.hasSecondOperand)
				{
					const auto second = normalizedById.find(operands.secondItemId);
					if (second == normalizedById.end()) continue;
					event.kind = SoundEngine::TimelineEventKind::Comparison;
					event.firstPosition = first->second;
					event.secondPosition = second->second;
					event.firstRole = static_cast<std::uint8_t>(audioRole(operands.firstRole));
					event.secondRole = static_cast<std::uint8_t>(audioRole(operands.secondRole));
					if (operands.reverseOrder)
					{
						std::swap(event.firstPosition, event.secondPosition);
						std::swap(event.firstRole, event.secondRole);
					}
					event.firstGain = event.secondGain = 1.0;
				}
				else
				{
					event.kind = SoundEngine::TimelineEventKind::Probe;
					event.firstPosition = event.secondPosition = first->second;
					event.firstRole = static_cast<std::uint8_t>(audioRole(operands.firstRole));
					event.firstGain = 1.0;
					event.secondGain = 0.0;
				}
			}
			else if (kind == sortinglogic::TraceAudioEventKind::AuxiliaryItemWrite)
			{
				const auto item = normalizedById.find(source.firstItemId);
				if (item == normalizedById.end()) continue;
				event.kind = SoundEngine::TimelineEventKind::Auxiliary;
				event.firstPosition = item->second;
				event.secondPosition = item->second;
				event.firstGain = 1.0;
				event.secondGain = 0.0;
			}
		}
		std::unique_ptr<SoundEngine::PreparedAudioTimeline> timeline(new SoundEngine::PreparedAudioTimeline());
		if (!timeline->prepare(events, trace.events.size(), timingMode, durationSeconds,
			playbacktimebase::ReferenceTimelineSampleRate, playbacktimebase::ReferenceCadenceEventsPerSecond, &error,
			policy.usesComparisonCadence()
				? SoundEngine::FixedDurationSchedule::EligibleEvents
				: SoundEngine::FixedDurationSchedule::SourceCursor))
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
	// An empty poll must not return the reader-owned slot to the producer.
	// Only this consumer clears DirtyBit; the producer can only set it.
	if ((middle_.load(std::memory_order_acquire) & DirtyBit) == 0)
		return false;
	const unsigned int received = middle_.exchange(consumerIndex_, std::memory_order_acq_rel);
	consumerIndex_ = received & IndexMask;
	state = slots_[consumerIndex_];
	return true;
}

AudioPresentationBridge::AudioPresentationBridge()
	: lastTrace_(nullptr), lastSessionIdentity_(0), tracePolicy_(), lastCursor_(0), lastComparison_(), lastEventAccents_(), hasComparison_(false), lastComparisonIsProbe_(false), valuesById_(),
	  workspaceSizes_(), minimumValue_(0.0f), maximumValue_(0.0f), lastPublished_(), hasPublished_(false), revision_(0),
	  timelines_(), currentTimeline_(nullptr), timelineGeneration_(0), seekRevision_(0), audioParameterRevision_(1),
	  transportRevision_(0), lastTransportCursor_(0),
	  lastTimelineDurationSeconds_(-1.0), lastTimingMode_(SoundEngine::PlaybackTimingMode::FixedDuration),
	  currentSeekState_(nullptr), seekStates_(),
	  timelineBuildMutex_(), timelineBuildCondition_(), pendingTimelineBuild_(), completedTimelineBuild_(),
	  requestedTimelineGeneration_(0), hasPendingTimelineBuild_(false), hasCompletedTimelineBuild_(false),
	  stopTimelineWorker_(false), lastAudioDeviceAvailable_(false), seekPreparing_(false), requestedSeekRevision_(0),
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
	const sortinglogic::SimulationPlaybackState& playbackState, const SoundEngine::Parameters& parameters,
	LatestAudioStateMailbox& mailbox, const SoundEngine::ReconstructedParameters& reconstructedParameters,
	SoundEngine::PlaybackTimingMode timingMode, bool audioDeviceAvailable, std::uint64_t sessionIdentity)
{
	pollTimelineBuild();
	const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation.currentTrace();
	const sortinglogic::SortTrace* rawTrace = trace.get();
	const bool sessionChanged = sessionIdentity != lastSessionIdentity_;
	if (sessionChanged)
	{
		lastSessionIdentity_ = sessionIdentity;
		// A new RunSession owns a distinct cursor and transport even if allocator
		// reuse gives its trace the same address as the previous session.
		lastTrace_ = nullptr;
		lastTimelineDurationSeconds_ = -1.0;
		++transportRevision_;
	}
	const bool traceChanged = sessionChanged || rawTrace != lastTrace_;
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
		tracePolicy_ = rawTrace
			? sortinglogic::TraceAudioPresentationPolicy(*rawTrace)
			: sortinglogic::TraceAudioPresentationPolicy();
		lastCursor_ = 0;
		lastComparison_ = SoundEngine::AudioComparison();
		lastEventAccents_ = SoundEngine::EventAccentSummary();
		hasComparison_ = false;
		lastComparisonIsProbe_ = false;
	}
	const bool rebuildTimeline = traceChanged || lastTimelineDurationSeconds_ != playbackState.durationSeconds ||
		lastTimingMode_ != timingMode;
	if (rebuildTimeline)
	{
		++timelineGeneration_;
		requestTimelineBuild(trace, playbackState.durationSeconds, timingMode, parameters);
		lastTimelineDurationSeconds_ = playbackState.durationSeconds;
		lastTimingMode_ = timingMode;
	}

	SoundEngine::EventAccentSummary crossedAccents;
	if (!rawTrace || !playbackState.hasTrace)
	{
		hasComparison_ = false;
		lastComparison_ = SoundEngine::AudioComparison();
		lastEventAccents_ = SoundEngine::EventAccentSummary();
		lastCursor_ = 0;
		lastComparisonIsProbe_ = false;
	}
	else if (playbackState.cursor < lastCursor_)
	{
		recomputeComparison(rawTrace, playbackState.cursor, lastComparison_, hasComparison_, lastComparisonIsProbe_);
		lastCursor_ = playbackState.cursor;
		lastEventAccents_ = SoundEngine::EventAccentSummary();
	}
	else
	{
		const std::size_t end = std::min(playbackState.cursor, rawTrace->events.size());
		for (std::size_t i = lastCursor_; i < end; ++i)
		{
			const sortinglogic::SemanticEvent& event = rawTrace->events[i];
			const sortinglogic::TraceAudioEventKind kind = tracePolicy_.classify(event);
			if (kind == sortinglogic::TraceAudioEventKind::Comparison)
			{
				SoundEngine::AudioComparison comparison;
				if (makeComparison(*rawTrace, event, comparison))
				{
					lastComparison_ = comparison;
					hasComparison_ = true;
					lastComparisonIsProbe_ = false;
				}
			}
			else if (kind == sortinglogic::TraceAudioEventKind::ProbeAsComparison)
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
				if (!tracePolicy_.frameAccentVisible(event)) continue;
				SoundEngine::AccentCategory category;
				bool routed = true;
				switch (kind)
				{
				case sortinglogic::TraceAudioEventKind::ProbeAccent: category = SoundEngine::AccentCategory::Probe; break;
				case sortinglogic::TraceAudioEventKind::MainChangeAccent: category = SoundEngine::AccentCategory::Movement; break;
				case sortinglogic::TraceAudioEventKind::AuxiliaryItemWrite:
				case sortinglogic::TraceAudioEventKind::DistributionUpdate: category = SoundEngine::AccentCategory::Distribution; break;
				case sortinglogic::TraceAudioEventKind::DiscardAccent: category = SoundEngine::AccentCategory::Discard; break;
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

	if (playbackState.cursor == 0 || playbackState.complete)
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
	const std::size_t auxiliaryCategory = static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution);
	current.timelineGains.auxiliaryGain = parameters.accentRouting[auxiliaryCategory] ? parameters.accentGain[auxiliaryCategory] : 0.0;
	for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
		current.timelineGains.roleGain[i] = gainFromDecibels(parameters.roleBoostDb[i]);
	current.eventAccents = lastEventAccents_;
	current.traceLoaded = rawTrace != nullptr && playbackState.hasTrace;
	current.hasComparison = current.traceLoaded && hasComparison_;
	current.transportPlaying = playbackState.playing;
	current.releaseSelection = current.hasComparison && !playbackState.playing &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
	current.comparison = lastComparison_;
	SoundEngine::AudioComparison burstOrder = lastComparison_;
	if (!lastComparisonIsProbe_ && tracePolicy_.reverseAudioOperandOrder())
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
	current.transport.sourceCursor = playbackState.cursor;
	current.transport.playing = playbackState.playing;
	current.transport.holdSelection = !playbackState.playing && current.hasComparison && !playbackState.complete &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::HoldSelection;
	current.transport.stopAtEnd = playbackState.complete || !current.traceLoaded;
	const bool playbackChanged = !hasPublished_ || playbackState.playing != lastPublished_.transportPlaying;
	const bool cursorChangedWhilePaused = !playbackState.playing && playbackState.cursor != lastTransportCursor_;
	const bool timelineInstallationChanged = currentTimeline_ != lastPublished_.timeline;
	const bool clockRecovery = audioDeviceAvailable && !lastAudioDeviceAvailable_;
	const bool audioSettingsChanged = hasPublished_ &&
		(!(current.reconstructedParameters == lastPublished_.reconstructedParameters) ||
		 !sameTimelineGains(current.timelineGains, lastPublished_.timelineGains));
	if (audioSettingsChanged) ++audioParameterRevision_;
	const bool transportChanged = rebuildTimeline || timelineInstallationChanged || playbackChanged || cursorChangedWhilePaused || clockRecovery;
	current.transport.releaseSelection = !playbackState.playing && (playbackChanged || cursorChangedWhilePaused) && current.hasComparison &&
		reconstructedParameters.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
	if (transportChanged)
	{
		++transportRevision_;
		current.transport.timelineFrame = currentTimeline_ ? currentTimeline_->frameForCursor(playbackState.cursor) : 0;
		current.transport.revision = transportRevision_;
	}
	else
	{
		current.transport.timelineFrame = lastPublished_.transport.timelineFrame;
		current.transport.revision = lastPublished_.transport.revision;
	}
	lastTransportCursor_ = playbackState.cursor;
	lastAudioDeviceAvailable_ = audioDeviceAvailable;
	if (seekPreparing_ && (!currentTimeline_ || playbackState.cursor == 0 || playbackState.complete))
	{
		std::lock_guard<std::mutex> lock(timelineBuildMutex_);
		++requestedSeekRevision_;
		hasPendingTimelineBuild_ = false;
		hasCompletedTimelineBuild_ = false;
		seekPreparing_ = false;
		timelinePreparing_ = false;
		currentSeekState_ = nullptr;
	}
	const bool seekRequired = currentTimeline_ && playbackState.cursor > 0 && !playbackState.complete &&
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
		!(current.parameters == lastPublished_.parameters) ||
		!(current.reconstructedParameters == lastPublished_.reconstructedParameters))
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

bool AudioPresentationBridge::playheadReady(const AudioTimelineAcknowledgement& acknowledgement) const
{
	return timelineReady() && acknowledgement.timelineGeneration == timelineGeneration_ &&
		(!currentSeekState_ || acknowledgement.seekRevision >= seekRevision_);
}

void AudioPresentationBridge::collectRetiredTimelines(const AudioTimelineAcknowledgement& acknowledgement,
	bool callbackAvailable)
{
	if (timelinePreparing_ || seekPreparing_) return;
	for (std::vector<std::unique_ptr<SoundEngine::PreparedAudioTimeline> >::iterator i = timelines_.begin(); i != timelines_.end();)
	{
		const bool current = i->get() == currentTimeline_;
		if (!current && (!callbackAvailable || acknowledgement.timelineGeneration >= timelineGeneration_))
			i = timelines_.erase(i);
		else ++i;
	}
	for (std::vector<std::unique_ptr<SoundEngine::PreparedAudioSeekState> >::iterator i = seekStates_.begin(); i != seekStates_.end();)
	{
		const bool current = i->get() == currentSeekState_;
		if (!current && (!callbackAvailable || acknowledgement.seekRevision >= seekRevision_))
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
	const std::vector<std::size_t>& audioIndices = trace->audioEventIndices;
	std::vector<std::size_t>::const_iterator candidate = std::lower_bound(audioIndices.begin(), audioIndices.end(), end);
	while (candidate != audioIndices.begin())
	{
		--candidate;
		const std::size_t eventIndex = *candidate;
		if (eventIndex < trace->events.size())
		{
			const sortinglogic::TraceAudioEventKind kind = tracePolicy_.classify(trace->events[eventIndex]);
			if (kind != sortinglogic::TraceAudioEventKind::Comparison &&
				kind != sortinglogic::TraceAudioEventKind::ProbeAsComparison)
				continue;
			SoundEngine::AudioComparison previous;
			if (makeComparison(*trace, trace->events[eventIndex], previous))
			{
				comparison = previous;
				hasComparison = true;
				isProbe = kind == sortinglogic::TraceAudioEventKind::ProbeAsComparison;
				return;
			}
		}
	}
}

bool AudioPresentationBridge::makeComparison(const sortinglogic::SortTrace& trace,
	const sortinglogic::SemanticEvent& event, SoundEngine::AudioComparison& comparison) const
{
	sortinglogic::TraceAudioOperandProjection operands;
	if (!sortinglogic::projectAudioOperands(trace, event, operands)) return false;
	const std::map<std::uint64_t, float>::const_iterator first = valuesById_.find(operands.firstItemId);
	if (first == valuesById_.end()) return false;
	if (!operands.hasSecondOperand)
	{
		const float position = SoundEngine::normalizeValue(first->second, minimumValue_, maximumValue_);
		comparison = SoundEngine::AudioComparison(position, position,
		audioRole(operands.firstRole), SoundEngine::ComparisonRole::None);
		return true;
	}
	const std::map<std::uint64_t, float>::const_iterator second = valuesById_.find(operands.secondItemId);
	if (first == valuesById_.end() || second == valuesById_.end()) return false;
	comparison = SoundEngine::AudioComparison(
		SoundEngine::normalizeValue(first->second, minimumValue_, maximumValue_),
		SoundEngine::normalizeValue(second->second, minimumValue_, maximumValue_),
		audioRole(operands.firstRole), audioRole(operands.secondRole));
	return true;
}
