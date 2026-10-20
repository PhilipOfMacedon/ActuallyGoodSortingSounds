#include "SortingLogicInternal.h"

#include <cmath>
#include <set>
#include <stdexcept>

namespace sortinglogic
{
	using detail::effectiveDomain;

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
		if (request.mergeWritebackMode != MergeWritebackMode::AnimatedWrites &&
			request.mergeWritebackMode != MergeWritebackMode::AtomicRangeWrite)
			throw std::invalid_argument("The requested Merge Sort writeback mode is invalid.");
		if (request.mergeExecutionMode != MergeExecutionMode::BottomUp &&
			request.mergeExecutionMode != MergeExecutionMode::TopDown)
			throw std::invalid_argument("The requested Merge Sort execution mode is invalid.");
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(request.algorithm);
		if (!descriptor || !descriptor->buildRequest) throw std::invalid_argument("The requested sorting algorithm is not registered.");
		if ((!request.familyId.empty() || !request.variantId.empty()) &&
			(request.familyId.empty() || request.variantId.empty() ||
			 algorithmDescriptorById(request.familyId, request.variantId) != descriptor))
			throw std::invalid_argument("The requested family and variant IDs do not match the registered algorithm.");
		const InputDomain domain = effectiveDomain(request);
		if (!inputPatternCompatible(domain, request.pattern))
			throw std::invalid_argument("The selected input pattern is incompatible with this data domain.");
		if (request.input.size() > descriptor->safeMaxItemCount)
			throw std::invalid_argument((std::string(descriptor->displayName) + " is limited to " +
				std::to_string(descriptor->safeMaxItemCount) + " items for safe trace generation.").c_str());
		if (!algorithmSupports(request.algorithm, domain)) throw std::invalid_argument("The selected algorithm does not support this data domain.");
		std::set<std::uint64_t> identities;
		std::set<std::int32_t> uniqueKeys;
		for (std::size_t i = 0; i < request.input.size(); ++i)
		{
			const SortItem& item = request.input[i];
			if (!std::isfinite(item.value)) throw std::invalid_argument("Sorting input values must be finite floats.");
			if (!identities.insert(item.stableId).second) throw std::invalid_argument("Sorting input stable IDs must be unique.");
			if (domain == InputDomain::NormalizedFloats && item.hasIntegerKey) throw std::invalid_argument("Float domain cannot contain integer-tagged items.");
			if (domain != InputDomain::NormalizedFloats && !item.hasIntegerKey) throw std::invalid_argument("Integer domains require integer keys.");
			if (domain == InputDomain::NonnegativeIntegerKeys &&
				(item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= request.input.size()))
				throw std::invalid_argument("Nonnegative integer keys must lie in the range [0, N-1].");
			if (domain == InputDomain::SignedIntegerKeys &&
				(std::abs(static_cast<std::int64_t>(item.integerKey)) >= static_cast<std::int64_t>(request.input.size())))
				throw std::invalid_argument("Signed integer keys must lie in the range [-(N-1), N-1].");
			if (domain == InputDomain::IntegerPermutation)
			{
				if (item.integerKey < 0 || static_cast<std::size_t>(item.integerKey) >= request.input.size())
					throw std::invalid_argument("Permutation keys must lie in the range [0, N-1].");
				if (!uniqueKeys.insert(item.integerKey).second)
					throw std::invalid_argument("Permutation domain requires each integer key exactly once.");
			}
		}
		if (domain == InputDomain::IntegerPermutation && uniqueKeys.size() != request.input.size())
			throw std::invalid_argument("Permutation domain requires a complete unique key range.");
		return descriptor->buildRequest(request);
	}

}
