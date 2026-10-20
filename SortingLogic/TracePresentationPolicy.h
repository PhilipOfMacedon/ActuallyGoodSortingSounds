#pragma once

#include "SortingLogic.h"

namespace sortinglogic
{
	enum class TraceAudioEventKind
	{
		Silent,
		Comparison,
		ProbeAsComparison,
		ProbeAccent,
		MainChangeAccent,
		AuxiliaryItemWrite,
		DistributionUpdate,
		DiscardAccent
	};

	enum class AudioControlCategory
	{
		Comparison,
		Probe,
		MainChange,
		Auxiliary,
		Discard
	};

	struct TraceAudioOperandProjection
	{
		std::uint64_t firstItemId;
		std::uint64_t secondItemId;
		SemanticRole firstRole;
		SemanticRole secondRole;
		bool hasSecondOperand;
		bool reverseOrder;
		TraceAudioOperandProjection()
			: firstItemId(0), secondItemId(0), firstRole(SemanticRole::ComparedItem), secondRole(SemanticRole::ComparedItem),
			  hasSecondOperand(false), reverseOrder(false) {}
	};

	// Resolves the trace-level presentation rules once so cadence, timeline
	// preparation, live accents, and contextual controls use the same policy.
	class TraceAudioPresentationPolicy
	{
	public:
		TraceAudioPresentationPolicy();
		explicit TraceAudioPresentationPolicy(const SortTrace& trace);

		TraceAudioEventKind classify(const SemanticEvent& event) const;
		bool projectOperands(const SemanticEvent& event, TraceAudioOperandProjection& projection) const;
		bool contributesToReferenceCadence(const SemanticEvent& event) const;
		bool frameAccentVisible(const SemanticEvent& event) const;
		bool hasAlgorithmComparisons() const { return hasAlgorithmComparisons_; }
		bool reverseAudioOperandOrder() const { return reverseAudioOperandOrder_; }

	private:
		SortAlgorithm algorithm_;
		bool hasAlgorithmComparisons_;
		bool mergeBufferVisualizationEnabled_;
		bool reverseAudioOperandOrder_;
	};

	std::vector<std::size_t> collectAudioEventIndices(const SortTrace& trace);
	void populateAudioEventIndices(SortTrace& trace);
	bool projectAudioOperands(const SortTrace& trace, const SemanticEvent& event, TraceAudioOperandProjection& projection);
	bool isWorkspaceVisible(SortAlgorithm algorithm, bool mergeBufferVisualizationEnabled, const std::string& lane);
	std::string visibleRoleWorkspace(SortAlgorithm algorithm, bool mergeBufferVisualizationEnabled, const std::string& lane);
	std::vector<ActiveRole> visibleRolesOnMainArray(const RenderSnapshot& snapshot);
	bool supportsAudioControlCategory(const AlgorithmDescriptor& algorithm, AudioControlCategory category);
}
