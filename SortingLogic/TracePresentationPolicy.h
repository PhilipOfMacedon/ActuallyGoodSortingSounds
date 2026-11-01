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
		Reconstruction,
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
		Discard,
		Reconstruction
	};

	struct TraceAudioOperandProjection
	{
		std::uint64_t firstItemId;
		std::uint64_t secondItemId;
		SemanticRole firstRole;
		SemanticRole secondRole;
		bool hasSecondOperand;
		bool reverseOrder;
		float firstValue;
		float secondValue;
		bool hasCapturedValues;
		TraceAudioOperandProjection()
			: firstItemId(0), secondItemId(0), firstRole(SemanticRole::ComparedItem), secondRole(SemanticRole::ComparedItem),
			  hasSecondOperand(false), reverseOrder(false), firstValue(0.0f), secondValue(0.0f), hasCapturedValues(false) {}
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
		bool usesComparisonCadence() const { return presentation_.comparisonCadenceOnly; }
		bool fixedDurationUsesEligibleEvents() const { return presentation_.fixedDurationUsesEligibleEvents; }
		bool hasAlgorithmComparisons() const { return hasAlgorithmComparisons_; }
		bool reverseAudioOperandOrder() const { return presentation_.reverseAudioOperandOrder; }

	private:
		ResolvedAlgorithmPresentation presentation_;
		bool hasAlgorithmComparisons_;
	};

	std::vector<std::size_t> collectAudioEventIndices(const SortTrace& trace);
	void populateAudioEventIndices(SortTrace& trace);
	bool projectAudioOperands(const SortTrace& trace, const SemanticEvent& event, TraceAudioOperandProjection& projection);
	bool isWorkspaceVisible(const AlgorithmDescriptor& algorithm, const AlgorithmConfiguration& configuration, const std::string& lane);
	bool isWorkspaceVisible(const SortTrace& trace, const std::string& lane);
	bool isWorkspaceVisible(const RenderSnapshot& snapshot, const std::string& lane);
	std::string visibleRoleWorkspace(const RenderSnapshot& snapshot, const std::string& lane);
	std::vector<ActiveRole> visibleRolesOnMainArray(const RenderSnapshot& snapshot);
	bool supportsAudioControlCategory(const AlgorithmDescriptor& algorithm, AudioControlCategory category);
}
