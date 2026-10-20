#include "SortingLogicInternal.h"

#include <cmath>
#include <set>
#include <stdexcept>

namespace sortinglogic
{
	using detail::effectiveDomain;

	namespace
	{
		void applyOptionSelections(TraceBuildRequest& request, const AlgorithmDescriptor& descriptor)
		{
			for (std::map<std::string, std::string>::const_iterator selected = request.optionSelections.begin();
				selected != request.optionSelections.end(); ++selected)
			{
				const AlgorithmOptionDescriptor* resolved = nullptr;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
				{
					const AlgorithmOptionDescriptor& option = descriptor.supportedOptions[i];
					if (selected->first == option.groupId && selected->second == option.stableId)
					{
						resolved = &option;
						break;
					}
				}
				if (!resolved)
					throw std::invalid_argument("The requested algorithm option ID is not registered for this algorithm.");

				const int value = resolved->value;
				if (selected->first == "gap_sequence")
					request.shellGapSequence = static_cast<ShellGapSequence>(value);
				else if (selected->first == "execution_mode")
					request.mergeExecutionMode = static_cast<MergeExecutionMode>(value);
				else if (selected->first == "writeback_mode")
					request.mergeWritebackMode = static_cast<MergeWritebackMode>(value);
				else if (selected->first == "heap_arity" || selected->first == "bucket_count" ||
					selected->first == "grail_buffer_mode")
					request.bucketCount = value;
				else if (selected->first == "radix_base")
					request.radixBase = value;
				else if (selected->first == "time_magnitude")
					request.timeMagnitude = value;
				else if (selected->first == "rotate_buffer_capacity")
					request.rotateMergeBufferSize = value;
				else
					throw std::invalid_argument("The requested algorithm option group has no trace configuration binding.");
			}
		}
	}

	SortTrace SortTraceBuilder::build(const std::vector<SortItem>& input, SortDirection direction, std::uint32_t inputSeed)
	{
		TraceBuildRequest request;
		request.input = input;
		request.direction = direction;
		request.inputSeed = inputSeed;
		request.algorithm = SortAlgorithm::Insertion;
		request.profile = input.empty() || !input[0].hasIntegerKey ? InputProfile::NormalizedFloat : InputProfile::IntegerKeys;
		request.domain = request.profile == InputProfile::NormalizedFloat ? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys;
		return build(request);
	}

	SortTrace SortTraceBuilder::build(const TraceBuildRequest& request)
	{
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(request.algorithm);
		if (!descriptor || !descriptor->buildRequest) throw std::invalid_argument("The requested sorting algorithm is not registered.");
		if ((!request.familyId.empty() || !request.variantId.empty()) &&
			(request.familyId.empty() || request.variantId.empty() ||
			 algorithmDescriptorById(request.familyId, request.variantId) != descriptor))
			throw std::invalid_argument("The requested family and variant IDs do not match the registered algorithm.");
		TraceBuildRequest resolvedRequest = request;
		applyOptionSelections(resolvedRequest, *descriptor);
		if (resolvedRequest.mergeWritebackMode != MergeWritebackMode::AnimatedWrites &&
			resolvedRequest.mergeWritebackMode != MergeWritebackMode::AtomicRangeWrite)
			throw std::invalid_argument("The requested Merge Sort writeback mode is invalid.");
		if (resolvedRequest.mergeExecutionMode != MergeExecutionMode::BottomUp &&
			resolvedRequest.mergeExecutionMode != MergeExecutionMode::TopDown)
			throw std::invalid_argument("The requested Merge Sort execution mode is invalid.");
		const InputDomain domain = effectiveDomain(resolvedRequest);
		if (!inputPatternCompatible(domain, resolvedRequest.pattern))
			throw std::invalid_argument("The selected input pattern is incompatible with this data domain.");
		if (resolvedRequest.input.size() > descriptor->safeMaxItemCount)
			throw std::invalid_argument((std::string(descriptor->displayName) + " is limited to " +
				std::to_string(descriptor->safeMaxItemCount) + " items for safe trace generation.").c_str());
		if (!algorithmSupports(resolvedRequest.algorithm, domain)) throw std::invalid_argument("The selected algorithm does not support this data domain.");
		std::set<std::uint64_t> identities;
		std::set<std::int32_t> uniqueKeys;
		for (std::size_t i = 0; i < resolvedRequest.input.size(); ++i)
		{
			const SortItem& item = resolvedRequest.input[i];
			if (!std::isfinite(item.value)) throw std::invalid_argument("Sorting input values must be finite floats.");
			if (!identities.insert(item.stableId).second) throw std::invalid_argument("Sorting input stable IDs must be unique.");
			if (domain == InputDomain::NormalizedFloats && item.hasIntegerKey) throw std::invalid_argument("Float domain cannot contain integer-tagged items.");
			if (domain != InputDomain::NormalizedFloats && !item.hasIntegerKey) throw std::invalid_argument("Integer domains require integer keys.");
			if (domain == InputDomain::NonnegativeIntegerKeys &&
				(item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= resolvedRequest.input.size()))
				throw std::invalid_argument("Nonnegative integer keys must lie in the range [0, N-1].");
			if (domain == InputDomain::SignedIntegerKeys &&
				(std::abs(static_cast<std::int64_t>(item.integerKey)) >= static_cast<std::int64_t>(resolvedRequest.input.size())))
				throw std::invalid_argument("Signed integer keys must lie in the range [-(N-1), N-1].");
			if (domain == InputDomain::IntegerPermutation)
			{
				if (item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= resolvedRequest.input.size())
					throw std::invalid_argument("Permutation keys must lie in the range [0, N-1].");
				if (!uniqueKeys.insert(item.integerKey).second)
					throw std::invalid_argument("Permutation domain requires each integer key exactly once.");
			}
		}
		if (domain == InputDomain::IntegerPermutation && uniqueKeys.size() != resolvedRequest.input.size())
			throw std::invalid_argument("Permutation domain requires a complete unique key range.");
		return descriptor->buildRequest(resolvedRequest);
	}

}
