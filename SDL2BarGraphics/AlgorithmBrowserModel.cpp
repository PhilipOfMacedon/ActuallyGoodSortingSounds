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

void AlgorithmBrowserModel::layout(const UiRect& anchor, int logicalWidth, int logicalHeight)
{
	if (!open || logicalWidth <= 0 || logicalHeight <= 0) return;
	const float width = static_cast<float>(logicalWidth);
	const float height = static_cast<float>(logicalHeight);
	const float menuWidth = std::min(700.0f, std::max(1.0f, width - 24.0f));
	const float menuHeight = std::min(520.0f, std::max(180.0f, height - 24.0f));
	const float left = std::max(12.0f, std::min(width - menuWidth - 12.0f, anchor.x));
	float top = anchor.y + anchor.height + 6.0f;
	if (top + menuHeight > height - 12.0f)
		top = std::max(12.0f, anchor.y - menuHeight - 6.0f);

	bounds = UiRect(left, top, menuWidth, menuHeight);
	searchBounds = UiRect(left + 12.0f, top + 12.0f, menuWidth - 24.0f, 30.0f);
	const float paneTop = top + 52.0f;
	const float paneHeight = std::max(64.0f, menuHeight - 96.0f);
	const float familyWidth = (menuWidth - 36.0f) * 0.46f;
	familyBounds = UiRect(left + 12.0f, paneTop, familyWidth, paneHeight);
	variantBounds = UiRect(familyBounds.x + familyWidth + 12.0f, paneTop,
		menuWidth - familyWidth - 36.0f, paneHeight);
}

void AlgorithmBrowserModel::prepare()
{
	const std::vector<const AlgorithmFamilyEntry*> matches = visibleFamilies();
	const AlgorithmFamilyEntry* selected = nullptr;
	for (std::size_t i = 0; i < matches.size(); ++i)
		if (matches[i]->id == selectedFamily)
		{
			selected = matches[i];
			familySelection = static_cast<int>(i);
			break;
		}
	if (!selected && !matches.empty())
	{
		familySelection = std::max(0, std::min(familySelection, static_cast<int>(matches.size()) - 1));
		selected = matches[static_cast<std::size_t>(familySelection)];
		selectedFamily = selected->id;
		variantSelection = 0;
	}
	configureViewports();
}

void AlgorithmBrowserModel::configureViewports()
{
	const std::vector<const AlgorithmFamilyEntry*> matches = visibleFamilies();
	const AlgorithmFamilyEntry* selected = nullptr;
	for (std::size_t i = 0; i < matches.size(); ++i)
		if (matches[i]->id == selectedFamily) { selected = matches[i]; break; }
	const std::vector<const sortinglogic::AlgorithmDescriptor*> variants = visibleVariants(selected);
	familyViewport.configure(familyBounds, static_cast<float>(matches.size()) * 30.0f, 30.0f,
		static_cast<float>(familyScroll) * 30.0f);
	variantViewport.configure(variantBounds, static_cast<float>(variants.size()) * 30.0f, 30.0f,
		static_cast<float>(variantScroll) * 30.0f);
	familyScroll = static_cast<int>(familyViewport.offset() / 30.0f + 0.5f);
	variantScroll = static_cast<int>(variantViewport.offset() / 30.0f + 0.5f);
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

AlgorithmBrowserInputResult AlgorithmBrowserModel::handleInput(const UiInputEvent& event, bool setupEnabled,
	int selectedAlgorithm)
{
	AlgorithmBrowserInputResult result;
	if (!open) return result;
	prepare();
	const std::vector<const AlgorithmFamilyEntry*> matches = visibleFamilies();
	const AlgorithmFamilyEntry* selected = nullptr;
	for (std::size_t i = 0; i < matches.size(); ++i)
		if (matches[i]->id == selectedFamily) { selected = matches[i]; break; }
	std::vector<const sortinglogic::AlgorithmDescriptor*> variants = visibleVariants(selected);
	const int familyVisible = std::max(1, static_cast<int>(familyBounds.height / 30.0f));
	const int variantVisible = std::max(1, static_cast<int>(variantBounds.height / 30.0f));
	auto keepSelectionVisible = [](int selection, int visible, int count, int& scroll)
	{
		if (selection < scroll) scroll = selection;
		else if (selection >= scroll + visible) scroll = selection - visible + 1;
		scroll = std::max(0, std::min(scroll, std::max(0, count - visible)));
	};
	if (event.type == UiInputType::TextInput && searchFocused)
	{
		appendSearch(event.text);
		return result;
	}
	if (event.type == UiInputType::Wheel)
	{
		if (familyViewport.scrollWheel(event.x, event.y, event.wheelDelta))
			familyScroll = static_cast<int>(familyViewport.offset() / 30.0f + 0.5f);
		else if (variantViewport.scrollWheel(event.x, event.y, event.wheelDelta))
			variantScroll = static_cast<int>(variantViewport.offset() / 30.0f + 0.5f);
		return result;
	}
	if (event.type == UiInputType::KeyDown)
	{
		if (pendingSelection) return result;
		if (event.key == UiKey::Escape)
		{
			if (variantPane) { variantPane = false; searchFocused = false; }
			else result.closeRequested = true;
			return result;
		}
		if (event.key == UiKey::Backspace && searchFocused)
		{
			eraseSearchCharacter();
			return result;
		}
		if (event.key == UiKey::Tab)
		{
			searchFocused = !searchFocused;
			return result;
		}
		if (event.key == UiKey::Up || event.key == UiKey::Down)
		{
			searchFocused = false;
			int& selection = variantPane ? variantSelection : familySelection;
			const int count = variantPane ? static_cast<int>(variants.size()) : static_cast<int>(matches.size());
			if (count > 0)
				selection = std::max(0, std::min(count - 1, selection + (event.key == UiKey::Down ? 1 : -1)));
			if (variantPane)
			{
				variantViewport.ensureItemVisible(static_cast<std::size_t>(selection), 30.0f);
				variantScroll = static_cast<int>(variantViewport.offset() / 30.0f + 0.5f);
				keepSelectionVisible(selection, variantVisible, count, variantScroll);
			}
			else
			{
				familyViewport.ensureItemVisible(static_cast<std::size_t>(selection), 30.0f);
				familyScroll = static_cast<int>(familyViewport.offset() / 30.0f + 0.5f);
				keepSelectionVisible(selection, familyVisible, count, familyScroll);
				if (count > 0) selectedFamily = matches[static_cast<std::size_t>(selection)]->id;
			}
			return result;
		}
		if ((event.key == UiKey::Right || event.key == UiKey::Enter) && !variantPane)
		{
			searchFocused = false;
			if (!matches.empty())
			{
				familySelection = std::max(0, std::min(familySelection, static_cast<int>(matches.size()) - 1));
				selectedFamily = matches[static_cast<std::size_t>(familySelection)]->id;
				selected = matches[static_cast<std::size_t>(familySelection)];
				variants = visibleVariants(selected);
				variantSelection = 0;
				for (std::size_t i = 0; i < variants.size(); ++i)
					if (static_cast<int>(variants[i]->id) == selectedAlgorithm) { variantSelection = static_cast<int>(i); break; }
				variantPane = true;
				variantScroll = 0;
				variantViewport.configure(variantBounds, static_cast<float>(variants.size()) * 30.0f, 30.0f, 0.0f);
			}
			return result;
		}
		if (event.key == UiKey::Left)
		{
			variantPane = false;
			searchFocused = false;
			return result;
		}
		if (event.key == UiKey::Enter && variantPane && !variants.empty() && variantSelection >= 0 &&
			static_cast<std::size_t>(variantSelection) < variants.size())
		{
			if (!setupEnabled)
				status = "Pause playback and wait for trace preparation before changing algorithms.";
			else
			{
				result.selectionRequested = true;
				result.familyId = variants[static_cast<std::size_t>(variantSelection)]->familyId;
				result.variantId = variants[static_cast<std::size_t>(variantSelection)]->variantId;
				pendingSelection = true;
				status.clear();
			}
			return result;
		}
		return result;
	}
	if (event.type == UiInputType::PointerDown)
	{
		if (pendingSelection) return result;
		if (!bounds.contains(event.x, event.y))
		{
			result.closeRequested = true;
			return result;
		}
		if (searchBounds.contains(event.x, event.y))
		{
			searchFocused = true;
			return result;
		}
		searchFocused = false;
		if (familyBounds.contains(event.x, event.y) && !matches.empty())
		{
			const int row = familyViewport.itemAt(event.x, event.y, matches.size(), 30.0f);
			if (row >= 0 && static_cast<std::size_t>(row) < matches.size())
			{
				familySelection = row;
				selectedFamily = matches[static_cast<std::size_t>(row)]->id;
				selected = matches[static_cast<std::size_t>(row)];
				variants = visibleVariants(selected);
				variantSelection = 0;
				for (std::size_t i = 0; i < variants.size(); ++i)
					if (static_cast<int>(variants[i]->id) == selectedAlgorithm) { variantSelection = static_cast<int>(i); break; }
				variantPane = true;
				variantScroll = 0;
				variantViewport.configure(variantBounds, static_cast<float>(variants.size()) * 30.0f, 30.0f, 0.0f);
			}
			return result;
		}
		if (variantBounds.contains(event.x, event.y) && !variants.empty())
		{
			const int row = variantViewport.itemAt(event.x, event.y, variants.size(), 30.0f);
			if (row >= 0 && static_cast<std::size_t>(row) < variants.size())
			{
				variantSelection = row;
				if (!setupEnabled)
					status = "Pause playback and wait for trace preparation before changing algorithms.";
				else
				{
					result.selectionRequested = true;
					result.familyId = variants[static_cast<std::size_t>(row)]->familyId;
					result.variantId = variants[static_cast<std::size_t>(row)]->variantId;
					pendingSelection = true;
					status.clear();
				}
			}
		}
	}
	return result;
}
