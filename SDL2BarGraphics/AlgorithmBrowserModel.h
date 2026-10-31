#pragma once

#include <string>
#include <vector>

#include "UiLayer.h"

struct AlgorithmBrowserVariant
{
	std::string variantId;
	std::string displayName;
};

struct AlgorithmFamilyEntry
{
	std::string id;
	std::string name;
	std::vector<AlgorithmBrowserVariant> variants;
};

struct AlgorithmBrowserInputResult
{
	bool closeRequested;
	bool selectionRequested;
	std::string familyId;
	std::string variantId;
	AlgorithmBrowserInputResult()
		: closeRequested(false), selectionRequested(false), familyId(), variantId() {}
};

// Owns the algorithm picker's selection/search/scroll state and catalog query
// behavior. It consumes platform-neutral UI events and returns semantic
// selection results; SDL lifecycle and painting remain with ControlPanel.
struct AlgorithmBrowserModel
{
	bool variantPane;
	bool searchFocused;
	bool pendingSelection;
	std::string status;
	std::string search;
	std::string selectedFamily;
	int familySelection;
	int variantSelection;
	int familyScroll;
	int variantScroll;
	UiRect bounds;
	UiRect searchBounds;
	UiRect familyBounds;
	UiRect variantBounds;
	UiRect familyScrollContentBounds;
	UiRect variantScrollContentBounds;
	UiScrollViewport familyViewport;
	UiScrollViewport variantViewport;
	UiScrollbar familyScrollbar;
	UiScrollbar variantScrollbar;
	std::vector<AlgorithmFamilyEntry> catalog;

	explicit AlgorithmBrowserModel(const std::vector<AlgorithmFamilyEntry>& algorithmCatalog);
	void begin(const std::string& initialFamily);
	void reset();
	void layout(const UiRect& anchor, int logicalWidth, int logicalHeight);
	void prepare();
	void configureViewports();
	AlgorithmBrowserInputResult handleInput(const UiInputEvent& event, bool setupEnabled,
		const std::string& selectedFamilyId, const std::string& selectedVariantId);
	void resetSearchResults();
	void appendSearch(const std::string& text, std::size_t maximumLength = 64);
	void eraseSearchCharacter();

	const std::vector<AlgorithmFamilyEntry>& families() const;
	std::vector<const AlgorithmFamilyEntry*> visibleFamilies() const;
	std::vector<const AlgorithmBrowserVariant*> visibleVariants(const AlgorithmFamilyEntry* family) const;
};
