#include "TracePresentationPolicy.h"

#include <algorithm>

namespace sortinglogic
{
	TraceAudioPresentationPolicy::TraceAudioPresentationPolicy()
		: algorithm_(SortAlgorithm::Insertion), hasAlgorithmComparisons_(false), mergeBufferVisualizationEnabled_(true),
		  reverseAudioOperandOrder_(false)
	{
	}

	TraceAudioPresentationPolicy::TraceAudioPresentationPolicy(const SortTrace& trace)
		: algorithm_(trace.algorithm), hasAlgorithmComparisons_(false),
		  mergeBufferVisualizationEnabled_(trace.mergeBufferVisualizationEnabled), reverseAudioOperandOrder_(false)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptorById(trace.familyId, trace.variantId);
		if (!descriptor) descriptor = algorithmDescriptor(trace.algorithm);
		reverseAudioOperandOrder_ = descriptor && descriptor->reverseAudioOperandOrder;
		if (!trace.comparisonEventIndices.empty())
		{
			const std::size_t firstComparison = trace.comparisonEventIndices.front();
			hasAlgorithmComparisons_ = firstComparison < trace.events.size() &&
				!trace.events[firstComparison].completionScan;
		}
	}

	TraceAudioEventKind TraceAudioPresentationPolicy::classify(const SemanticEvent& event) const
	{
		switch (event.type)
		{
		case SemanticEventType::Compare:
			return hasAlgorithmComparisons_ || event.completionScan
				? TraceAudioEventKind::Comparison : TraceAudioEventKind::Silent;
		case SemanticEventType::Probe:
			return hasAlgorithmComparisons_
				? TraceAudioEventKind::ProbeAccent : TraceAudioEventKind::ProbeAsComparison;
		case SemanticEventType::AuxiliaryItemWrite:
			if (!isWorkspaceVisible(algorithm_, mergeBufferVisualizationEnabled_, event.workspaceName))
				return TraceAudioEventKind::Silent;
			return TraceAudioEventKind::AuxiliaryItemWrite;
		case SemanticEventType::HoldKey:
		case SemanticEventType::ShiftItem:
		case SemanticEventType::PlaceKey:
		case SemanticEventType::CarryWrite:
		case SemanticEventType::Swap:
		case SemanticEventType::WriteItem:
		case SemanticEventType::WriteRange:
			return TraceAudioEventKind::MainChangeAccent;
		case SemanticEventType::CounterWrite:
		case SemanticEventType::ResizeAuxiliary:
		case SemanticEventType::ResizeCounterWorkspace:
			return TraceAudioEventKind::DistributionUpdate;
		case SemanticEventType::DiscardItem:
			return TraceAudioEventKind::DiscardAccent;
		default:
			return TraceAudioEventKind::Silent;
		}
	}

	bool TraceAudioPresentationPolicy::projectOperands(const SemanticEvent& event,
		TraceAudioOperandProjection& projection) const
	{
		projection = TraceAudioOperandProjection();
		const TraceAudioEventKind kind = classify(event);
		if (kind == TraceAudioEventKind::Comparison)
		{
			projection.firstItemId = event.firstItemId;
			projection.secondItemId = event.secondItemId;
			projection.firstRole = event.firstRole;
			projection.secondRole = event.secondRole;
			projection.hasSecondOperand = true;
			projection.reverseOrder = reverseAudioOperandOrder_;
			return true;
		}
		if (kind == TraceAudioEventKind::ProbeAsComparison)
		{
			projection.firstItemId = event.firstItemId;
			projection.firstRole = SemanticRole::ProbedItem;
			return true;
		}
		return false;
	}

	bool TraceAudioPresentationPolicy::contributesToReferenceCadence(const SemanticEvent& event) const
	{
		const TraceAudioEventKind kind = classify(event);
		return kind == TraceAudioEventKind::Comparison || kind == TraceAudioEventKind::ProbeAsComparison ||
			kind == TraceAudioEventKind::AuxiliaryItemWrite;
	}

	bool TraceAudioPresentationPolicy::frameAccentVisible(const SemanticEvent& event) const
	{
		// Preserve the existing Merge visualization switch behavior for live frame
		// accents. Timeline and cadence visibility are determined per merge lane.
		return algorithm_ != SortAlgorithm::Merge || mergeBufferVisualizationEnabled_ ||
			event.type == SemanticEventType::Compare;
	}

	std::vector<std::size_t> collectAudioEventIndices(const SortTrace& trace)
	{
		const TraceAudioPresentationPolicy policy(trace);
		std::vector<std::size_t> indices;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
			if (policy.contributesToReferenceCadence(trace.events[i])) indices.push_back(i);
		return indices;
	}

	void populateAudioEventIndices(SortTrace& trace)
	{
		trace.audioEventIndices = collectAudioEventIndices(trace);
	}

	bool projectAudioOperands(const SortTrace& trace, const SemanticEvent& event, TraceAudioOperandProjection& projection)
	{
		const TraceAudioPresentationPolicy policy(trace);
		return policy.projectOperands(event, projection);
	}

	bool isWorkspaceVisible(SortAlgorithm algorithm, bool mergeBufferVisualizationEnabled, const std::string& lane)
	{
		return algorithm != SortAlgorithm::Merge || mergeBufferVisualizationEnabled || lane != "merge";
	}

	std::string visibleRoleWorkspace(SortAlgorithm algorithm, bool mergeBufferVisualizationEnabled, const std::string& lane)
	{
		return isWorkspaceVisible(algorithm, mergeBufferVisualizationEnabled, lane) ? lane : "main";
	}

	std::vector<ActiveRole> visibleRolesOnMainArray(const RenderSnapshot& snapshot)
	{
		std::vector<ActiveRole> roles = snapshot.activeRoles;
		for (std::size_t i = 0; i < roles.size(); ++i)
			roles[i].lane = visibleRoleWorkspace(snapshot.algorithm,
				snapshot.mergeBufferVisualizationEnabled, roles[i].lane);
		return roles;
	}

	bool supportsAudioControlCategory(const AlgorithmDescriptor& algorithm, AudioControlCategory category)
	{
		switch (category)
		{
		case AudioControlCategory::Comparison:
			return true;
		case AudioControlCategory::Probe:
			return algorithm.id == SortAlgorithm::Counting || algorithm.id == SortAlgorithm::Radix;
		case AudioControlCategory::MainChange:
			// The bridge routes main changes, but the current contextual settings
			// surface did not expose a main-change control.
			return false;
		case AudioControlCategory::Auxiliary:
			return algorithm.id == SortAlgorithm::Merge || algorithm.id == SortAlgorithm::Counting ||
				algorithm.id == SortAlgorithm::Radix;
		case AudioControlCategory::Discard:
			return algorithm.behavior && std::string(algorithm.behavior).find("discard") != std::string::npos;
		}
		return false;
	}
}
