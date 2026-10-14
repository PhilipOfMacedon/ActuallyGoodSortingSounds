#include "AudioPresentationBridge.h"

#include <algorithm>
#include <cmath>

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

	void addAccent(SoundEngine::EventAccentSummary& summary, SoundEngine::AccentCategory category, float position)
	{
		const std::size_t index = static_cast<std::size_t>(category);
		if (index >= SoundEngine::AccentCategoryCount) return;
		if (summary.counts[index] < 255) ++summary.counts[index];
		summary.representativePositions[index] = std::max(0.0f, std::min(1.0f, position));
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
	: lastTrace_(nullptr), lastCursor_(0), lastComparison_(), lastEventAccents_(), hasComparison_(false), valuesById_(),
	  workspaceSizes_(), minimumValue_(0.0f), maximumValue_(0.0f), lastPublished_(), hasPublished_(false), revision_(0)
{
}

void AudioPresentationBridge::update(const sortinglogic::Simulation& simulation,
	const sortinglogic::RenderSnapshot& snapshot, const SoundEngine::Parameters& parameters,
	LatestAudioStateMailbox& mailbox)
{
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
	}

	SoundEngine::EventAccentSummary crossedAccents;
	if (!rawTrace || !snapshot.hasTrace)
	{
		hasComparison_ = false;
		lastComparison_ = SoundEngine::AudioComparison();
		lastEventAccents_ = SoundEngine::EventAccentSummary();
		lastCursor_ = 0;
	}
	else if (snapshot.cursor < lastCursor_)
	{
		recomputeComparison(rawTrace, snapshot.cursor, lastComparison_, hasComparison_);
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
		hasComparison_ = false;

	AudioPresentationState current;
	current.parameters = parameters;
	current.eventAccents = lastEventAccents_;
	current.traceLoaded = rawTrace != nullptr && snapshot.hasTrace;
	current.hasComparison = current.traceLoaded && hasComparison_;
	current.comparison = lastComparison_;
	if (!hasPublished_ || current.traceLoaded != lastPublished_.traceLoaded ||
		current.hasComparison != lastPublished_.hasComparison ||
		!sameComparison(current.comparison, lastPublished_.comparison) ||
		!sameEventAccents(current.eventAccents, lastPublished_.eventAccents) ||
		!sameParameters(current.parameters, lastPublished_.parameters))
	{
		current.revision = ++revision_;
		lastPublished_ = current;
		hasPublished_ = true;
		mailbox.publish(current);
	}
}

void AudioPresentationBridge::recomputeComparison(const sortinglogic::SortTrace* trace, std::size_t cursor,
	SoundEngine::AudioComparison& comparison, bool& hasComparison) const
{
	hasComparison = false;
	comparison = SoundEngine::AudioComparison();
	if (!trace) return;
	const std::size_t end = std::min(cursor, trace->events.size());
	const std::vector<std::size_t>& comparisonIndices = trace->comparisonEventIndices;
	std::vector<std::size_t>::const_iterator candidate = std::lower_bound(
		comparisonIndices.begin(), comparisonIndices.end(), end);
	while (candidate != comparisonIndices.begin())
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
				return;
			}
		}
	}
}

bool AudioPresentationBridge::makeComparison(const sortinglogic::SortTrace&,
	const sortinglogic::SemanticEvent& event, SoundEngine::AudioComparison& comparison) const
{
	const std::map<std::uint64_t, float>::const_iterator first = valuesById_.find(event.firstItemId);
	const std::map<std::uint64_t, float>::const_iterator second = valuesById_.find(event.secondItemId);
	if (first == valuesById_.end() || second == valuesById_.end()) return false;
	comparison = SoundEngine::AudioComparison(
		SoundEngine::normalizeValue(first->second, minimumValue_, maximumValue_),
		SoundEngine::normalizeValue(second->second, minimumValue_, maximumValue_),
		audioRole(event.firstRole), audioRole(event.secondRole));
	return true;
}
