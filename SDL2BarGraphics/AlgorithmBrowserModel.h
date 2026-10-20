#pragma once

#include <string>
#include <vector>

#include "../SortingLogic/SortingLogic.h"
#include "UiLayer.h"

struct AlgorithmFamilyEntry
{
	sortinglogic::FamilyId id;
	std::string name;
	std::vector<const sortinglogic::AlgorithmDescriptor*> variants;
};

struct AlgorithmBrowserInputResult
{
	bool closeRequested;
	bool selectionRequested;
	sortinglogic::FamilyId familyId;
	sortinglogic::VariantId variantId;
	AlgorithmBrowserInputResult()
		: closeRequested(false), selectionRequested(false), familyId(), variantId() {}
};

// Owns the algorithm picker's selection/search/scroll state and catalog query
// behavior. It consumes platform-neutral UI events and returns semantic
// selection results; SDL lifecycle and painting remain with ControlPanel.
struct AlgorithmBrowserModel
{
	bool open;
	bool variantPane;
	bool searchFocused;
	bool pendingSelection;
	std::string status;
	std::string search;
	sortinglogic::FamilyId selectedFamily;
	int familySelection;
	int variantSelection;
	int familyScroll;
	int variantScroll;
	UiRect bounds;
	UiRect searchBounds;
	UiRect familyBounds;
	UiRect variantBounds;
	UiScrollViewport familyViewport;
	UiScrollViewport variantViewport;

	AlgorithmBrowserModel();
	void begin(const sortinglogic::FamilyId& initialFamily);
	void close();
	void layout(const UiRect& anchor, int logicalWidth, int logicalHeight);
	void prepare();
	void configureViewports();
	AlgorithmBrowserInputResult handleInput(const UiInputEvent& event, bool setupEnabled, int selectedAlgorithm);
	void resetSearchResults();
	void appendSearch(const std::string& text, std::size_t maximumLength = 64);
	void eraseSearchCharacter();

	const std::vector<AlgorithmFamilyEntry>& families() const;
	std::vector<const AlgorithmFamilyEntry*> visibleFamilies() const;
	std::vector<const sortinglogic::AlgorithmDescriptor*> visibleVariants(const AlgorithmFamilyEntry* family) const;
};
