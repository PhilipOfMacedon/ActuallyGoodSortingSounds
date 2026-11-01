#include "SortingLogicInternal.h"
#include "AlgorithmConfigurationResolver.h"

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
		request.domain = input.empty() || !input[0].hasIntegerKey
			? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys;
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
		resolvedRequest.configuration = detail::resolveAlgorithmConfiguration(request, *descriptor);
		resolvedRequest.domain = effectiveDomain(request);
		const InputDomain domain = resolvedRequest.domain;
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
