#include "TracePresentationPolicy.h"

namespace sortinglogic
{
	namespace
	{
		const AlgorithmDescriptor* descriptorFor(const SortTrace& trace)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptorById(trace.familyId, trace.variantId);
			return descriptor && descriptor->id == trace.algorithm ? descriptor : algorithmDescriptor(trace.algorithm);
		}

		const AlgorithmDescriptor* descriptorFor(const RenderSnapshot& snapshot)
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptorById(snapshot.familyId, snapshot.variantId);
			return descriptor && descriptor->id == snapshot.algorithm ? descriptor : algorithmDescriptor(snapshot.algorithm);
		}

	}

	TraceAudioPresentationPolicy::TraceAudioPresentationPolicy()
		: presentation_(resolveAlgorithmPresentation(algorithmDescriptor(SortAlgorithm::Insertion), AlgorithmConfiguration())),
		  hasAlgorithmComparisons_(false)
	{
	}

	TraceAudioPresentationPolicy::TraceAudioPresentationPolicy(const SortTrace& trace)
		: presentation_(resolveAlgorithmPresentation(descriptorFor(trace), trace.configuration)), hasAlgorithmComparisons_(false)
	{
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
			// Keep the established probe-only algorithm behavior: when a trace has
			// no comparisons, its real item probes occupy the comparison timeline
			// as single-operand notes. Registered probe accents apply when the
			// algorithm also has comparison events.
			return hasAlgorithmComparisons_ && presentation_.probeAccentsSupported
				? TraceAudioEventKind::ProbeAccent : TraceAudioEventKind::ProbeAsComparison;
		case SemanticEventType::AuxiliaryItemWrite:
			return !presentation_.registeredAlgorithm || presentation_.auxiliaryWritesAudible
				? TraceAudioEventKind::AuxiliaryItemWrite : TraceAudioEventKind::Silent;
		default:
			break;
		}

		// Reconstruction is a semantic primary event with captured operands. It
		// gets its own channel before the legacy movement accent can see it.
		if (event.reconstructive)
			return TraceAudioEventKind::Reconstruction;

		switch (event.type)
		{
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
			return presentation_.discardAccentsSupported
				? TraceAudioEventKind::DiscardAccent : TraceAudioEventKind::Silent;
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
			projection.reverseOrder = presentation_.reverseAudioOperandOrder;
			projection.firstValue = event.item.value;
			projection.secondValue = event.secondItem.value;
			projection.hasCapturedValues = true;
			return true;
		}
		if (kind == TraceAudioEventKind::ProbeAsComparison || kind == TraceAudioEventKind::ProbeAccent)
		{
			projection.firstItemId = event.firstItemId;
			projection.firstRole = event.firstRole;
			projection.firstValue = event.item.value;
			projection.hasCapturedValues = true;
			return true;
		}
		if (kind == TraceAudioEventKind::Reconstruction)
		{
			projection.firstItemId = event.item.stableId;
			projection.firstRole = event.firstRole;
			projection.firstValue = event.item.value;
			projection.hasCapturedValues = true;
			if (event.type == SemanticEventType::Swap)
			{
				projection.secondItemId = event.secondItem.stableId;
				projection.secondRole = event.secondRole;
				projection.secondValue = event.secondItem.value;
				projection.hasSecondOperand = true;
				projection.hasCapturedValues = true;
			}
			return true;
		}
		return false;
	}

	bool TraceAudioPresentationPolicy::contributesToReferenceCadence(const SemanticEvent& event) const
	{
		const TraceAudioEventKind kind = classify(event);
		if (presentation_.comparisonCadenceOnly)
			return kind == TraceAudioEventKind::Comparison || kind == TraceAudioEventKind::Reconstruction;
		return kind == TraceAudioEventKind::Comparison || kind == TraceAudioEventKind::ProbeAsComparison ||
			kind == TraceAudioEventKind::AuxiliaryItemWrite || kind == TraceAudioEventKind::Reconstruction;
	}

	bool TraceAudioPresentationPolicy::frameAccentVisible(const SemanticEvent& event) const
	{
		return !presentation_.comparisonCadenceOnly || classify(event) == TraceAudioEventKind::Comparison ||
			classify(event) == TraceAudioEventKind::Reconstruction;
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

	bool isWorkspaceVisible(const AlgorithmDescriptor& algorithm, const AlgorithmConfiguration& configuration,
		const std::string& lane)
	{
		return resolveAlgorithmPresentation(&algorithm, configuration).workspaceVisible(lane);
	}

	bool isWorkspaceVisible(const SortTrace& trace, const std::string& lane)
	{
		const AlgorithmDescriptor* descriptor = descriptorFor(trace);
		return !descriptor || isWorkspaceVisible(*descriptor, trace.configuration, lane);
	}

	bool isWorkspaceVisible(const RenderSnapshot& snapshot, const std::string& lane)
	{
		const AlgorithmDescriptor* descriptor = descriptorFor(snapshot);
		return !descriptor || isWorkspaceVisible(*descriptor, snapshot.configuration, lane);
	}

	std::string visibleRoleWorkspace(const RenderSnapshot& snapshot, const std::string& lane)
	{
		return isWorkspaceVisible(snapshot, lane) ? lane : "main";
	}

	std::vector<ActiveRole> visibleRolesOnMainArray(const RenderSnapshot& snapshot)
	{
		std::vector<ActiveRole> roles = snapshot.activeRoles;
		for (std::size_t i = 0; i < roles.size(); ++i)
			roles[i].lane = visibleRoleWorkspace(snapshot, roles[i].lane);
		return roles;
	}

	bool supportsAudioControlCategory(const AlgorithmDescriptor& algorithm, AudioControlCategory category)
	{
		switch (category)
		{
		case AudioControlCategory::Comparison: return true;
		case AudioControlCategory::Probe: return resolveAlgorithmPresentation(&algorithm, AlgorithmConfiguration()).probeAccentsSupported;
		case AudioControlCategory::MainChange: return resolveAlgorithmPresentation(&algorithm, AlgorithmConfiguration()).mainChangeAccentsVisible;
		case AudioControlCategory::Auxiliary: return resolveAlgorithmPresentation(&algorithm, AlgorithmConfiguration()).auxiliaryWritesAudible;
		case AudioControlCategory::Discard: return resolveAlgorithmPresentation(&algorithm, AlgorithmConfiguration()).discardAccentsSupported;
		case AudioControlCategory::Reconstruction: return resolveAlgorithmPresentation(&algorithm, AlgorithmConfiguration()).reconstructionSupported;
		}
		return false;
	}
}
