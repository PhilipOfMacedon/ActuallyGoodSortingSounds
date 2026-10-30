#include "AlgorithmBrowserView.h"

namespace
{
	const float BrowserRowHeight = 30.0f;

	void submitRectangle(UiPaintContext& context, const UiRect& bounds, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer, const UiRect& clip = UiRect(), bool clipped = false)
	{
		context.submit(UiPaintCommand(material == UiPaintMaterial::Panel && layer == UiPaintLayer::Panel ? UiPaintPrimitive::Panel : UiPaintPrimitive::Rectangle, bounds, color, std::string(), std::string(),
			material, layer, clip, clipped, 0.0f));
	}

	void submitText(UiPaintContext& context, const std::string& elementId, const std::string& text,
		const UiRect& bounds, float textHeight, const UiColor& color, const UiRect& clip)
	{
		context.submit(UiPaintCommand(UiPaintPrimitive::Text, bounds, color, text, elementId,
			UiPaintMaterial::Solid, UiPaintLayer::Text, clip, true, textHeight));
	}
}

void AlgorithmBrowserView::compose(const AlgorithmBrowserModel& browser, const AlgorithmBrowserPalette& palette,
	bool setupEnabled, const std::string& selectedFamilyId, const std::string& selectedVariantId,
	int logicalWidth, int logicalHeight, UiPaintContext& paintContext) const
{
	const std::vector<const AlgorithmFamilyEntry*> families = browser.visibleFamilies();
	const AlgorithmFamilyEntry* selectedFamily = nullptr;
	for (std::size_t i = 0; i < families.size(); ++i)
		if (families[i]->id == browser.selectedFamily) { selectedFamily = families[i]; break; }
	const std::vector<const AlgorithmBrowserVariant*> variants = browser.visibleVariants(selectedFamily);
	const UiRect screen(0.0f, 0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight));
	const UiRect familyClip(browser.familyBounds.x, browser.familyBounds.y, browser.familyBounds.width, browser.familyBounds.height);
	const UiRect variantClip(browser.variantBounds.x, browser.variantBounds.y, browser.variantBounds.width, browser.variantBounds.height);

	submitRectangle(paintContext, screen, palette.overlayDim, UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop);
	submitRectangle(paintContext, browser.bounds, palette.panel, UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true);
	submitRectangle(paintContext, UiRect(browser.bounds.x, browser.bounds.y, browser.bounds.width, 2.0f),
		palette.accent, UiPaintMaterial::Accent, UiPaintLayer::Accent, screen, true);
	submitRectangle(paintContext, browser.searchBounds,
		browser.searchFocused ? palette.valueButton : palette.background,
		UiPaintMaterial::Popup, UiPaintLayer::Content, screen, true);
	submitText(paintContext, "#algorithm_search",
		std::string("Search algorithms: ") + browser.search + (browser.searchFocused ? "_" : ""),
		browser.searchBounds, 14.0f, palette.text, screen);
	submitRectangle(paintContext, browser.familyBounds, palette.background,
		UiPaintMaterial::Panel, UiPaintLayer::Content, screen, true);
	submitRectangle(paintContext, browser.variantBounds, palette.background,
		UiPaintMaterial::Panel, UiPaintLayer::Content, screen, true);

	const std::size_t familyFirst = browser.familyViewport.firstVisibleItem(BrowserRowHeight);
	const std::size_t familyRows = browser.familyViewport.visibleItemCount(families.size(), BrowserRowHeight);
	for (std::size_t row = 0; row < familyRows; ++row)
	{
		const std::size_t familyIndex = familyFirst + row;
		if (familyIndex >= families.size()) break;
		const AlgorithmFamilyEntry* family = families[familyIndex];
		const UiRect rowBounds = browser.familyViewport.itemBounds(familyIndex, BrowserRowHeight);
		const bool selected = family->id == browser.selectedFamily;
		submitRectangle(paintContext, rowBounds, selected ? palette.accent : palette.button,
			UiPaintMaterial::Popup, UiPaintLayer::Content, familyClip, true);
		submitText(paintContext, "#menu_family_" + family->id, family->name,
			UiRect(rowBounds.x + 6.0f, rowBounds.y, rowBounds.width - 18.0f, rowBounds.height),
			12.0f, palette.text, familyClip);
	}
	if (families.empty())
		submitText(paintContext, "#algorithm_no_family", "No matching families", browser.familyBounds,
			13.0f, palette.mutedText, familyClip);

	if (selectedFamily)
	{
		const std::size_t variantFirst = browser.variantViewport.firstVisibleItem(BrowserRowHeight);
		const std::size_t variantRows = browser.variantViewport.visibleItemCount(variants.size(), BrowserRowHeight);
		for (std::size_t row = 0; row < variantRows; ++row)
		{
			const std::size_t variantIndex = variantFirst + row;
			if (variantIndex >= variants.size()) break;
			const AlgorithmBrowserVariant* variant = variants[variantIndex];
			const UiRect rowBounds = browser.variantViewport.itemBounds(variantIndex, BrowserRowHeight);
			const bool selected = selectedFamily->id == selectedFamilyId && variant->variantId == selectedVariantId;
			const bool keyboardSelected = browser.variantPane && static_cast<int>(variantIndex) == browser.variantSelection;
			const UiColor fill = setupEnabled
				? (keyboardSelected || selected ? palette.accent : palette.button) : palette.disabledButton;
			submitRectangle(paintContext, rowBounds, fill, UiPaintMaterial::Popup, UiPaintLayer::Content, variantClip, true);
			submitText(paintContext, "#menu_variant_" + selectedFamily->id + "_" + variant->variantId,
				variant->displayName, UiRect(rowBounds.x + 8.0f, rowBounds.y, rowBounds.width - 16.0f, rowBounds.height),
				12.0f, setupEnabled ? palette.text : palette.mutedText, variantClip);
		}
		if (variants.empty())
			submitText(paintContext, "#algorithm_no_variant", "No matching variants", browser.variantBounds,
				13.0f, palette.mutedText, variantClip);
	}
	else
		submitText(paintContext, "#algorithm_choose_family", "Choose a family to see its variants",
			browser.variantBounds, 13.0f, palette.mutedText, variantClip);

	const std::string status = !browser.status.empty() ? browser.status :
		!setupEnabled ? "Pause playback before changing algorithms." : std::string();
	if (!status.empty())
		submitText(paintContext, "#algorithm_menu_status", status,
			UiRect(browser.bounds.x + 12.0f, browser.bounds.y + browser.bounds.height - 30.0f,
				browser.bounds.width - 24.0f, 18.0f), 11.0f, palette.mutedText, screen);
}
