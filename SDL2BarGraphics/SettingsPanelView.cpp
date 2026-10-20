#include "SettingsPanelView.h"

SettingsPanelChrome::SettingsPanelChrome()
	: screenWidth(0), screenHeight(0), panelBounds(), contentBounds(), footerBounds(), title(), footer()
{
}

void SettingsPanelView::compose(const SettingsPanelChrome& chrome, const SettingsPanelPalette& palette,
	UiPaintContext& paintContext) const
{
	const UiRect screen(0.0f, 0.0f, static_cast<float>(chrome.screenWidth), static_cast<float>(chrome.screenHeight));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, screen, palette.overlayDim, std::string(), std::string(),
		UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop, UiRect(), false, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, chrome.panelBounds, palette.panel, std::string(), std::string(),
		UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle,
		UiRect(chrome.panelBounds.x, chrome.panelBounds.y, chrome.panelBounds.width, 2.0f), palette.accent,
		std::string(), std::string(), UiPaintMaterial::Accent, UiPaintLayer::Accent, screen, true, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Text,
		UiRect(chrome.panelBounds.x + 16.0f, chrome.panelBounds.y + 13.0f, chrome.panelBounds.width - 32.0f, 28.0f),
		palette.text, chrome.title, "#settings_title", UiPaintMaterial::Solid, UiPaintLayer::SettingsText,
		screen, true, 17.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, chrome.contentBounds, palette.background,
		std::string(), std::string(), UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true, 0.0f));
	if (!chrome.footer.empty() && chrome.footerBounds.width > 0.0f && chrome.footerBounds.height > 0.0f)
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Text, chrome.footerBounds, palette.mutedText, chrome.footer,
			"#settings_footer", UiPaintMaterial::Solid, UiPaintLayer::SettingsText, screen, true, 11.0f));
}
