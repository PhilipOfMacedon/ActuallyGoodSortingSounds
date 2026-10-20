#include "AlgorithmBrowserCatalog.h"

#include "../SortingLogic/SortingLogic.h"

const std::vector<AlgorithmFamilyEntry>& algorithmBrowserCatalog()
{
	static const std::vector<AlgorithmFamilyEntry> families = []()
	{
		std::vector<AlgorithmFamilyEntry> result;
		const std::vector<sortinglogic::AlgorithmDescriptor>& registry = sortinglogic::algorithmCatalog();
		for (std::size_t i = 0; i < registry.size(); ++i)
		{
			std::size_t familyIndex = 0;
			for (; familyIndex < result.size(); ++familyIndex)
				if (result[familyIndex].id == registry[i].familyId) break;
			if (familyIndex == result.size())
			{
				AlgorithmFamilyEntry entry;
				entry.id = registry[i].familyId;
				entry.name = registry[i].familyDisplayName;
				result.push_back(entry);
			}
			AlgorithmBrowserVariant variant;
			variant.variantId = registry[i].variantId;
			variant.displayName = registry[i].displayName;
			result[familyIndex].variants.push_back(variant);
		}
		return result;
	}();
	return families;
}
