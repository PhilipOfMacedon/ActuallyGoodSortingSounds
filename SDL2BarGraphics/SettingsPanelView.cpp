#include "SettingsPanelView.h"

#include <algorithm>
#include <set>

#include "UiActionRouting.h"
#include "SettingsPanelLayout.h"

void SettingsPanelView::selectAudioBand(int index, int bandCount)
{
	model_.selectAudioBand(index, bandCount);
}

bool SettingsPanelView::handleNavigation(UiAction::ViewCommand command)
{
	const ui::SettingsNavigation navigation = ui::settingsNavigationFor(command);
	if (navigation.kind == ui::SettingsNavigationKind::None) return false;
	if (navigation.kind == ui::SettingsNavigationKind::AudioGroup)
		model_.showAudioGroup(navigation.audioGroup);
	else
		model_.showPage(navigation.page);
	controls_.closeCombo();
	return true;
}

bool SettingsPanelView::isScrollablePage() const
{
	return model_.isPage(SettingsPanelPage::Algorithm) || model_.isPage(SettingsPanelPage::GeneralAudio);
}

bool SettingsPanelView::handleScrollWheel(float x, float y, int wheelDelta)
{
	return isScrollablePage() && model_.scrollViewport().scrollWheel(x, y, wheelDelta);
}

void SettingsPanelView::setPanelBounds(const UiRect& panelBounds, bool modalOpen)
{
	panelBounds_ = panelBounds;
	controls_.layoutContainer("settings_panel", panelBounds_);
	if (modalOpen)
		controls_.setModalBounds(panelBounds_, UiAction::ViewCommand::CloseSettings);
}

void SettingsPanelView::setModalActive(bool active)
{
	controls_.setInputCaptured(active);
	if (active && panelBounds_.width > 0.0f)
		controls_.setModalBounds(panelBounds_, UiAction::ViewCommand::CloseSettings);
}

void SettingsPanelView::layoutControls(const std::vector<std::string>& activeControlIds,
	float scrollbarThickness)
{
	if (panelBounds_.width <= 0.0f) return;
	const float x = panelBounds_.x + 16.0f;
	const float width = std::max(1.0f, panelBounds_.width - 32.0f);
	const SettingsPanelGeometry panelGeometry = calculateSettingsPanelGeometry(panelBounds_, model_.page());
	controls_.find("appearance_tab")->setBounds(panelGeometry.tabs[0]);
	controls_.find("algorithm_tab")->setBounds(panelGeometry.tabs[1]);
	controls_.find("audio_tab")->setBounds(panelGeometry.tabs[2]);
	controls_.find("audio_basic_tab")->setBounds(panelGeometry.audioGroups[0]);
	controls_.find("audio_advanced_tab")->setBounds(panelGeometry.audioGroups[1]);
	contentBounds_ = panelGeometry.content;
	controls_.find("settings_close")->setBounds(panelGeometry.closeButton);
	controls_.find("audio_retry")->setBounds(panelGeometry.audioRetryButton);

	std::set<std::string> visibleWidgetIds;
	visibleWidgetIds.insert("appearance_tab");
	visibleWidgetIds.insert("algorithm_tab");
	visibleWidgetIds.insert("audio_tab");
	visibleWidgetIds.insert("settings_close");
	if (model_.isPage(SettingsPanelPage::GeneralAudio))
	{
		visibleWidgetIds.insert("audio_retry");
		visibleWidgetIds.insert("audio_basic_tab");
		visibleWidgetIds.insert("audio_advanced_tab");
	}
	const float rowHeight = 48.0f;
	const float contentBottom = contentBounds_.y + contentBounds_.height;
	const float totalHeight = static_cast<float>(activeControlIds.size()) * rowHeight;
	model_.scrollViewport().configure(contentBounds_, totalHeight, rowHeight, model_.scrollOffset());
	for (std::size_t i = 0; i < activeControlIds.size(); ++i)
	{
		const UiRect rowBounds = model_.scrollViewport().itemBounds(i, rowHeight);
		const float rowY = rowBounds.y;
		const bool visible = model_.scrollViewport().itemIntersectsViewport(i, rowHeight) &&
			rowY + rowHeight <= contentBottom + 0.5f;
		if (visible) visibleWidgetIds.insert(activeControlIds[i]);
		UiWidget* widget = controls_.find(activeControlIds[i]);
		if (!widget) continue;
		const SettingsWidgetGeometry geometry = calculateSettingsWidgetGeometry(*widget, rowBounds,
			x, width, panelBounds_, scrollbarThickness);
		widget->setBounds(geometry.bounds);
		widget->setInteractiveSubregions(geometry.interactiveSubregions);
		if (geometry.setPopupAbove) widget->setPopupAbove(geometry.popupAbove);
	}
	for (std::size_t i = 0; i < controls_.widgets().size(); ++i)
	{
		const std::string& id = controls_.widgets()[i]->id();
		controls_.setWidgetVisible(id, visibleWidgetIds.find(id) != visibleWidgetIds.end());
	}
}

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
