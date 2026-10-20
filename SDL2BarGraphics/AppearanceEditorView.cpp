#include "AppearanceEditorView.h"

#include <algorithm>

AppearanceEditorView::AppearanceEditorView(const std::vector<std::string>& presetNames,
	const std::vector<std::string>& paletteNames, const UiAppearancePreferences& initial,
	int initialPaletteIndex)
	: controls_(), session_(), layout_(), description_()
{
	controls_.addComboBox("appearance_preset", presetNames, static_cast<int>(initial.activePreset));
	controls_.addComboBox("appearance_palette", paletteNames, initialPaletteIndex);
	controls_.addComboBox("appearance_category", std::vector<std::string>{
		"All categories", "Colors", "Typography", "Spacing / layout", "Decorations" }, 0);
	controls_.addButton("appearance_search", "Search appearance tokens");
	controls_.addComboBox("appearance_token", std::vector<std::string>(), 0);
	controls_.addButton("appearance_value_down", "-");
	controls_.addButton("appearance_value", "0");
	controls_.addButton("appearance_value_up", "+");
	controls_.addSlider("appearance_value_slider", "Property value", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	controls_.addButton("appearance_color", "Pick color...");
	controls_.addButton("appearance_color_alpha", "Alpha 1.000");
	controls_.addSlider("appearance_color_r", "Red", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	controls_.addSlider("appearance_color_g", "Green", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	controls_.addSlider("appearance_color_b", "Blue", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	controls_.addSlider("appearance_color_a", "Alpha", 0.0f, 1.0f, 0.01f, 1.0f, AppAction());
	controls_.addButton("appearance_boolean", "Toggle value");
	controls_.addComboBox("appearance_choice", std::vector<std::string>(), 0);
	controls_.addComboBox("appearance_font_family", std::vector<std::string>(), 0);
	controls_.addComboBox("appearance_font_style", std::vector<std::string>(), 0);
	controls_.addButton("appearance_reset_token", "Reset property");
	controls_.addButton("appearance_reset_theme", "Reset this preset");
	controls_.addButton("appearance_import", "Import profile...");
	controls_.addButton("appearance_export", "Export profile...");
	controls_.addButton("appearance_apply", "Apply");
	controls_.addButton("appearance_cancel", "Cancel");
	session_.begin(initial);
}

void AppearanceEditorView::begin(const UiAppearancePreferences& current)
{
	session_.begin(current);
	controls_.find("appearance_preset")->setSelectedIndex(static_cast<int>(current.activePreset));
	description_.clear();
}

void AppearanceEditorView::restore()
{
	session_.restore();
	controls_.find("appearance_preset")->setSelectedIndex(static_cast<int>(session_.draft().activePreset));
}

void AppearanceEditorView::accept(const UiAppearancePreferences& committed)
{
	session_.accept(committed);
	controls_.find("appearance_preset")->setSelectedIndex(static_cast<int>(session_.draft().activePreset));
}

void AppearanceEditorView::updateLayout(int logicalWidth, int logicalHeight)
{
	if (logicalWidth <= 0 || logicalHeight <= 0) return;
	layout_ = calculateAppearanceEditorLayout(static_cast<float>(logicalWidth), static_cast<float>(logicalHeight));
	if (!layout_.valid) return;
	for (std::map<std::string, UiRect>::const_iterator control = layout_.controls.begin();
		control != layout_.controls.end(); ++control)
	{
		UiWidget* widget = controls_.find(control->first);
		if (widget) widget->setBounds(control->second);
	}
	const char* popupIds[] = { "appearance_category", "appearance_token", "appearance_choice",
		"appearance_font_family", "appearance_font_style" };
	for (std::size_t i = 0; i < sizeof(popupIds) / sizeof(popupIds[0]); ++i)
	{
		UiWidget* widget = controls_.find(popupIds[i]);
		if (widget)
			widget->setPopupAbove(widget->bounds().y + widget->bounds().height *
				(static_cast<float>(widget->visibleOptionCount()) + 1.0f) >
				layout_.panelBounds.y + layout_.panelBounds.height - 8.0f);
	}
	controls_.setModalBounds(layout_.panelBounds, UiAction::ViewCommand::CloseAppearanceEditor);
}

void AppearanceEditorView::compose(int screenWidth, int screenHeight, const std::string& status, bool statusIsError,
	const UiTokenValue& selectedValue, const AppearanceEditorPalette& palette,
	UiPaintContext& paintContext) const
{
	if (!layout_.valid || screenWidth <= 0 || screenHeight <= 0) return;
	const UiRect screen(0.0f, 0.0f, static_cast<float>(screenWidth), static_cast<float>(screenHeight));
	const UiRect& panel = layout_.panelBounds;
	const UiRect panelClip(panel.x, panel.y, panel.width, panel.height);
	const UiRect contentClip = layout_.contentClip;

	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, screen, palette.overlayDim,
		std::string(), std::string(), UiPaintMaterial::ModalDim, UiPaintLayer::Backdrop, UiRect(), false, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, panel, palette.panel,
		std::string(), std::string(), UiPaintMaterial::Panel, UiPaintLayer::Panel, screen, true, 0.0f));
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle,
		UiRect(panel.x, panel.y, panel.width, 2.0f), palette.accent,
		std::string(), std::string(), UiPaintMaterial::Accent, UiPaintLayer::Accent, screen, true, 0.0f));

	UiPaintCommand title(UiPaintPrimitive::Text, layout_.titleBounds, palette.text,
		"Appearance customization", "#appearance_title", UiPaintMaterial::Solid,
		UiPaintLayer::SettingsText, panelClip, true, 17.0f);
	title.textOverflow = UiTextOverflow::Fit;
	paintContext.submit(title);

	if (selectedValue.type == UiTokenType::Color)
	{
		const UiRect& swatch = layout_.colorSwatchBounds;
		const float edge = (std::max)(1.0f, layout_.scale);
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, swatch, selectedValue.color,
			std::string(), std::string(), UiPaintMaterial::Button, UiPaintLayer::SettingsControls, contentClip, true, 0.0f));
		const UiRect borders[] = {
			UiRect(swatch.x, swatch.y, swatch.width, edge),
			UiRect(swatch.x, swatch.y + swatch.height - edge, swatch.width, edge),
			UiRect(swatch.x, swatch.y, edge, swatch.height),
			UiRect(swatch.x + swatch.width - edge, swatch.y, edge, swatch.height)
		};
		for (std::size_t i = 0; i < sizeof(borders) / sizeof(borders[0]); ++i)
			paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, borders[i], palette.border,
				std::string(), std::string(), UiPaintMaterial::Button, UiPaintLayer::SettingsControls, contentClip, true, 0.0f));
	}

	UiPaintCommand description(UiPaintPrimitive::Text, layout_.descriptionBounds, palette.mutedText,
		description_, "#appearance_description", UiPaintMaterial::Solid,
		UiPaintLayer::SettingsText, contentClip, true, 12.0f);
	description.textOverflow = UiTextOverflow::Fit;
	paintContext.submit(description);

	UiPaintCommand statusCommand(UiPaintPrimitive::Text, layout_.statusBounds,
		statusIsError ? palette.accent : palette.mutedText, status, "#appearance_status",
		UiPaintMaterial::Solid, UiPaintLayer::SettingsText, panelClip, true, 11.0f);
	statusCommand.textOverflow = UiTextOverflow::Fit;
	paintContext.submit(statusCommand);
}
