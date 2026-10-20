#include "AlgorithmBrowserModel.h"

#include <algorithm>

#include "TextSearch.h"

namespace
{
	const std::vector<AlgorithmFamilyEntry>& algorithmFamilyCatalog()
	{
		static const std::vector<AlgorithmFamilyEntry> families = []()
		{
			std::vector<AlgorithmFamilyEntry> result;
			const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
			for (std::size_t i = 0; i < catalog.size(); ++i)
			{
				std::size_t familyIndex = 0;
				for (; familyIndex < result.size(); ++familyIndex)
					if (result[familyIndex].id == catalog[i].familyId) break;
				if (familyIndex == result.size())
				{
					AlgorithmFamilyEntry entry;
					entry.id = catalog[i].familyId;
					entry.name = catalog[i].familyDisplayName;
					result.push_back(entry);
				}
				result[familyIndex].variants.push_back(&catalog[i]);
			}
			return result;
		}();
		return families;
	}

}

AlgorithmBrowserModel::AlgorithmBrowserModel()
	: open(false), variantPane(false), searchFocused(false), pendingSelection(false), status(), search(), selectedFamily(),
	familySelection(0), variantSelection(0), familyScroll(0), variantScroll(0), bounds(), searchBounds(), familyBounds(),
	variantBounds(), familyViewport(), variantViewport()
{
}

void AlgorithmBrowserModel::begin(const sortinglogic::FamilyId& initialFamily)
{
	open = true;
	variantPane = false;
	searchFocused = true;
	pendingSelection = false;
	status.clear();
	search.clear();
	selectedFamily = initialFamily;
	familySelection = 0;
	variantSelection = 0;
	familyScroll = 0;
	variantScroll = 0;
}

void AlgorithmBrowserModel::close()
{
	open = false;
	pendingSelection = false;
	status.clear();
	variantPane = false;
	searchFocused = false;
}

void AlgorithmBrowserModel::resetSearchResults()
{
	familySelection = 0;
	variantSelection = 0;
	familyScroll = 0;
	variantScroll = 0;
}

void AlgorithmBrowserModel::appendSearch(const std::string& text, std::size_t maximumLength)
{
	if (search.size() < maximumLength) search += text;
	resetSearchResults();
}

void AlgorithmBrowserModel::eraseSearchCharacter()
{
	if (!search.empty()) search.erase(search.size() - 1);
	familyScroll = 0;
	variantScroll = 0;
}

const std::vector<AlgorithmFamilyEntry>& AlgorithmBrowserModel::families() const
{
	return algorithmFamilyCatalog();
}

std::vector<const AlgorithmFamilyEntry*> AlgorithmBrowserModel::visibleFamilies() const
{
	std::vector<const AlgorithmFamilyEntry*> visible;
	const std::vector<AlgorithmFamilyEntry>& catalog = algorithmFamilyCatalog();
	for (std::size_t i = 0; i < catalog.size(); ++i)
	{
		bool matches = containsTextInsensitive(catalog[i].name, search);
		for (std::size_t variant = 0; !matches && variant < catalog[i].variants.size(); ++variant)
			matches = containsTextInsensitive(catalog[i].variants[variant]->displayName, search);
		if (matches) visible.push_back(&catalog[i]);
	}
	return visible;
}

std::vector<const sortinglogic::AlgorithmDescriptor*> AlgorithmBrowserModel::visibleVariants(
	const AlgorithmFamilyEntry* family) const
{
	std::vector<const sortinglogic::AlgorithmDescriptor*> visible;
	if (!family) return visible;
	for (std::size_t i = 0; i < family->variants.size(); ++i)
		if (search.empty() || containsTextInsensitive(family->variants[i]->displayName, search) ||
			containsTextInsensitive(family->name, search))
			visible.push_back(family->variants[i]);
	return visible;
}
