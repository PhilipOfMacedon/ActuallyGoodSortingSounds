#include "AppearanceEditorView.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace
{
	std::string tokenDisplayValue(const UiTokenValue& value)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(3);
		switch (value.type)
		{
		case UiTokenType::Color:
			text << "R " << value.color.r << "  G " << value.color.g << "  B " << value.color.b << "  A " << value.color.a;
			break;
		case UiTokenType::Number: text << value.number; break;
		case UiTokenType::Boolean: text << (value.boolean ? "ON" : "OFF"); break;
		case UiTokenType::Choice: text << value.text; break;
		case UiTokenType::Font: text << value.font.family << " | " << value.font.style; break;
		}
		return text.str();
	}
}

AppearanceEditorView::AppearanceEditorView(const std::vector<std::string>& presetNames,
	const std::vector<std::string>& paletteNames, const UiAppearancePreferences& initial,
	int initialPaletteIndex)
	: controls_(), session_(), layout_(), description_(), searchFocused_(false), copySourcePreset_(static_cast<int>(initial.activePreset))
{
	controls_.addComboBox("appearance_preset", presetNames, static_cast<int>(initial.activePreset));
	controls_.addComboBox("appearance_palette", paletteNames, initialPaletteIndex);
	controls_.addComboBox("appearance_copy_source", presetNames, static_cast<int>(initial.activePreset));
	controls_.addButton("appearance_copy_colors", "Copy colors from selected preset");
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
	controls_.addButton("appearance_cancel", "Close");
	session_.begin(initial);
}

void AppearanceEditorView::begin(const UiAppearancePreferences& current)
{
	session_.begin(current);
	searchFocused_ = false;
	copySourcePreset_ = static_cast<int>(current.activePreset);
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

bool AppearanceEditorView::beginInlineValueEdit(AppearanceInlineEditField field, std::string& initialText)
{
	const UiPresetId preset = session_.model().preset();
	const std::string& selectedToken = session_.model().selectedToken();
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	if (!descriptor || (field == AppearanceInlineEditField::Number && descriptor->type != UiTokenType::Number) ||
		(field == AppearanceInlineEditField::Alpha && descriptor->type != UiTokenType::Color))
		return false;

	const UiTokenValue value = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
	std::ostringstream text;
	text << std::setprecision(9) << (field == AppearanceInlineEditField::Alpha ? value.color.a : value.number);
	initialText = text.str();
	return true;
}

void AppearanceEditorView::showInlineValueEdit(AppearanceInlineEditField field, const std::string& text)
{
	const char* controlId = field == AppearanceInlineEditField::Alpha ? "appearance_color_alpha" : "appearance_value";
	UiWidget* control = controls_.find(controlId);
	if (control) control->setLabel(text + "|");
}

bool AppearanceEditorView::commitInlineValueEdit(AppearanceInlineEditField field, bool parsedSuccessfully, double value)
{
	const UiPresetId preset = session_.model().preset();
	const std::string& selectedToken = session_.model().selectedToken();
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	const UiTokenType expectedType = field == AppearanceInlineEditField::Alpha ? UiTokenType::Color : UiTokenType::Number;
	if (!descriptor || descriptor->type != expectedType)
	{
		session_.error() = "The selected appearance property is unavailable.";
		return false;
	}
	if (!parsedSuccessfully || value < descriptor->minimum || value > descriptor->maximum)
	{
		session_.error() = "Enter a value between " + std::to_string(descriptor->minimum) + " and " +
			std::to_string(descriptor->maximum) + ".";
		return false;
	}

	UiTokenValue nextValue;
	if (field == AppearanceInlineEditField::Alpha)
	{
		nextValue = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
		nextValue.color.a = static_cast<float>(value);
	}
	else nextValue = UiTokenValue::fromNumber(value);

	std::string error;
	if (!setUiTokenValue(preset, session_.draft().themes[preset], selectedToken, nextValue, &error))
	{
		session_.error() = error;
		return false;
	}
	session_.error().clear();
	return true;
}

void AppearanceEditorView::synchronizeControls(const AppearanceFontFamilyProvider& fontFamilies,
	const AppearanceFontStyleProvider& fontStyles)
{
	const UiPresetId preset = session_.model().preset();
	const std::string& selectedToken = session_.model().selectedToken();
	const std::vector<AppearanceTokenOption>& tokenOptions = session_.model().options();
	std::vector<std::string> tokenLabels;
	tokenLabels.reserve(tokenOptions.size());
	for (std::size_t i = 0; i < tokenOptions.size(); ++i) tokenLabels.push_back(tokenOptions[i].label);

	UiWidget* presetControl = controls_.find("appearance_preset");
	UiWidget* paletteControl = controls_.find("appearance_palette");
	UiWidget* categoryControl = controls_.find("appearance_category");
	UiWidget* copySourceControl = controls_.find("appearance_copy_source");
	UiWidget* searchControl = controls_.find("appearance_search");
	UiWidget* tokenControl = controls_.find("appearance_token");
	if (presetControl) presetControl->setSelectedIndex(static_cast<int>(preset));
	if (paletteControl) paletteControl->setSelectedIndex(static_cast<int>(session_.draft().themes[preset].palette));
	if (copySourceControl) copySourceControl->setSelectedIndex(copySourcePreset_);
	if (categoryControl) categoryControl->setSelectedIndex(session_.model().categoryFilter());
	if (searchControl) searchControl->setLabel(session_.model().search().empty()
		? "Search tokens" : "Search: " + session_.model().search());
	if (tokenControl)
	{
		if (tokenControl->options() != tokenLabels) tokenControl->setOptions(tokenLabels);
		tokenControl->setSelectedIndex((std::max)(0, session_.model().selectedIndex()));
	}

	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	const bool hasToken = descriptor != nullptr;
	const UiTokenValue value = hasToken
		? effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken)
		: UiTokenValue();
	const bool hasTokenOptions = !tokenOptions.empty();
	controls_.setWidgetVisible("appearance_token", hasTokenOptions);
	controls_.setWidgetVisible("appearance_value_down", hasToken && descriptor->type == UiTokenType::Number);
	controls_.setWidgetVisible("appearance_value", hasToken && descriptor->type == UiTokenType::Number);
	controls_.setWidgetVisible("appearance_value_up", hasToken && descriptor->type == UiTokenType::Number);
	controls_.setWidgetVisible("appearance_value_slider", hasToken && descriptor->type == UiTokenType::Number);
	controls_.setWidgetVisible("appearance_color", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_color_alpha", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_color_r", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_color_g", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_color_b", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_color_a", hasToken && descriptor->type == UiTokenType::Color);
	controls_.setWidgetVisible("appearance_boolean", hasToken && descriptor->type == UiTokenType::Boolean);
	controls_.setWidgetVisible("appearance_choice", hasToken && descriptor->type == UiTokenType::Choice);
	controls_.setWidgetVisible("appearance_font_family", hasToken && descriptor->type == UiTokenType::Font);
	controls_.setWidgetVisible("appearance_font_style", hasToken && descriptor->type == UiTokenType::Font);

	if (hasToken)
	{
		std::string description = appearanceTokenLabel(*descriptor);
		if (descriptor->type == UiTokenType::Color)
			description += " | Chart bars and marks use the same color tokens as the interface.";
		setDescription(description);
	}
	else
		setDescription("No appearance tokens match this search.");

	if (hasToken && value.type == UiTokenType::Number)
	{
		UiWidget* valueControl = controls_.find("appearance_value");
		UiWidget* slider = controls_.find("appearance_value_slider");
		if (valueControl) valueControl->setLabel(tokenDisplayValue(value));
		if (slider)
		{
			const float minimum = static_cast<float>(descriptor->minimum);
			const float maximum = static_cast<float>(descriptor->maximum);
			const float step = maximum - minimum <= 1.0f ? 0.01f : 1.0f;
			slider->setSliderRange(minimum, maximum, step, static_cast<float>(value.number));
		}
	}
	else if (hasToken && value.type == UiTokenType::Color)
	{
		UiWidget* color = controls_.find("appearance_color");
		UiWidget* alpha = controls_.find("appearance_color_alpha");
		if (color) color->setLabel("Pick RGB | " + tokenDisplayValue(value));
		if (alpha) alpha->setLabel("Alpha " + tokenDisplayValue(UiTokenValue::fromNumber(value.color.a)));
		if (controls_.find("appearance_color_r")) controls_.find("appearance_color_r")->setSliderValue(value.color.r);
		if (controls_.find("appearance_color_g")) controls_.find("appearance_color_g")->setSliderValue(value.color.g);
		if (controls_.find("appearance_color_b")) controls_.find("appearance_color_b")->setSliderValue(value.color.b);
		if (controls_.find("appearance_color_a")) controls_.find("appearance_color_a")->setSliderValue(value.color.a);
	}
	else if (hasToken && value.type == UiTokenType::Boolean)
	{
		UiWidget* boolean = controls_.find("appearance_boolean");
		if (boolean) boolean->setLabel(value.boolean ? "Checked" : "Unchecked");
	}
	else if (hasToken && value.type == UiTokenType::Choice)
	{
		UiWidget* choice = controls_.find("appearance_choice");
		if (choice)
		{
			choice->setOptions(descriptor->choices);
			const std::vector<std::string>::const_iterator at = std::find(descriptor->choices.begin(), descriptor->choices.end(), value.text);
			choice->setSelectedIndex(at == descriptor->choices.end() ? 0 : static_cast<int>(at - descriptor->choices.begin()));
		}
	}
	else if (hasToken && value.type == UiTokenType::Font)
	{
		std::vector<std::string> families = fontFamilies ? fontFamilies() : std::vector<std::string>();
		if (std::find(families.begin(), families.end(), value.font.family) == families.end()) families.push_back(value.font.family);
		UiWidget* family = controls_.find("appearance_font_family");
		if (family)
		{
			family->setOptions(families);
			family->setSelectedIndex(static_cast<int>(std::find(families.begin(), families.end(), value.font.family) - families.begin()));
		}
		std::vector<std::string> styles = fontStyles ? fontStyles(value.font.family) : std::vector<std::string>();
		if (styles.empty()) styles.push_back(value.font.style);
		else if (std::find(styles.begin(), styles.end(), value.font.style) == styles.end()) styles.push_back(value.font.style);
		UiWidget* style = controls_.find("appearance_font_style");
		if (style)
		{
			style->setOptions(styles);
			style->setSelectedIndex(static_cast<int>(std::find(styles.begin(), styles.end(), value.font.style) - styles.begin()));
		}
	}
}

AppearanceEditorActionResult AppearanceEditorView::handleAction(const UiAction& action,
	const AppearanceFontStyleProvider& fontStyles)
{
	const std::string& id = action.event.controlId;
	const UiPresetId preset = session_.model().preset();
	const std::string& selectedToken = session_.model().selectedToken();
	const AppearanceEditorActionResult unhandled = { false, false, AppearanceEditorEffect::None };
	const AppearanceEditorActionResult handled = { true, false, AppearanceEditorEffect::None };
	const AppearanceEditorActionResult changed = { true, true, AppearanceEditorEffect::None };

	auto storeValue = [this, preset, &selectedToken](const UiTokenValue& value)
	{
		std::string error;
		if (!setUiTokenValue(preset, session_.draft().themes[preset], selectedToken, value, &error))
			session_.error() = error;
		else session_.error().clear();
	};
	auto editNumber = [this, preset, &selectedToken, &storeValue, &handled, &changed](float direction)
	{
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
		if (!descriptor || descriptor->type != UiTokenType::Number) return handled;
		const UiTokenValue current = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
		const double increment = descriptor->maximum - descriptor->minimum > 100.0 ? 1.0 : 0.5;
		const double next = (std::max)(descriptor->minimum,
			(std::min)(descriptor->maximum, current.number + direction * increment));
		storeValue(UiTokenValue::fromNumber(next));
		return changed;
	};

	if (id == "appearance_preset")
	{
		const int lastPreset = static_cast<int>(uiPresetCatalog().size()) - 1;
		session_.model().setPreset(static_cast<UiPresetId>((std::max)(0, (std::min)(action.event.selection, lastPreset))));
		session_.draft().activePreset = session_.model().preset();
		controls_.closeCombo();
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_palette")
	{
		const UiWidget* palette = controls_.find(id);
		if (!palette || palette->options().empty()) return handled;
		const int lastPalette = static_cast<int>(palette->options().size()) - 1;
		selectUiPalette(preset, session_.draft().themes[preset],
			static_cast<PaletteId>((std::max)(0, (std::min)(action.event.selection, lastPalette))));
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_category")
	{
		session_.model().setCategoryFilter(action.event.selection);
		controls_.closeCombo();
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_copy_source")
	{
		if (action.event.selection >= 0 && static_cast<std::size_t>(action.event.selection) < uiPresetCatalog().size())
			copySourcePreset_ = action.event.selection;
		return handled;
	}
	if (id == "appearance_copy_colors")
	{
		if (copySourcePreset_ < 0 || static_cast<std::size_t>(copySourcePreset_) >= uiPresetCatalog().size()) return handled;
		const UiPresetId sourcePreset = uiPresetCatalog()[static_cast<std::size_t>(copySourcePreset_)];
		if (!copyEffectiveUiPalette(sourcePreset, session_.draft().themes[sourcePreset],
			preset, session_.draft().themes[preset]))
		{
			session_.error() = "Could not copy colors from that interface preset.";
			return handled;
		}
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_token")
	{
		session_.model().selectOption(action.event.selection);
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_search") return unhandled;
	if (action.event.kind == UiEventKind::ValueChanged &&
		(id == "appearance_value_slider" || id == "appearance_color_r" || id == "appearance_color_g" ||
		 id == "appearance_color_b" || id == "appearance_color_a"))
	{
		UiTokenValue value = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
		if (id == "appearance_value_slider" && value.type == UiTokenType::Number)
			value.number = action.event.value;
		else if (value.type == UiTokenType::Color)
		{
			if (id == "appearance_color_r") value.color.r = action.event.value;
			else if (id == "appearance_color_g") value.color.g = action.event.value;
			else if (id == "appearance_color_b") value.color.b = action.event.value;
			else if (id == "appearance_color_a") value.color.a = action.event.value;
		}
		else return handled;
		storeValue(value);
		return changed;
	}
	if (id == "appearance_value")
		return { true, false, AppearanceEditorEffect::BeginNumericEdit };
	if (id == "appearance_color_alpha")
		return { true, false, AppearanceEditorEffect::BeginAlphaEdit };
	if (id == "appearance_value_down") return editNumber(-1.0f);
	if (id == "appearance_value_up") return editNumber(1.0f);
	if (id == "appearance_color")
		return { true, false, AppearanceEditorEffect::ChooseColor };
	if (id == "appearance_boolean")
	{
		const UiTokenValue before = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
		if (before.type != UiTokenType::Boolean) return handled;
		storeValue(UiTokenValue::fromBoolean(!before.boolean));
		return changed;
	}
	if (id == "appearance_choice")
	{
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
		if (descriptor && descriptor->type == UiTokenType::Choice && action.event.selection >= 0 &&
			static_cast<std::size_t>(action.event.selection) < descriptor->choices.size())
			storeValue(UiTokenValue::fromChoice(descriptor->choices[static_cast<std::size_t>(action.event.selection)]));
		return changed;
	}
	if (id == "appearance_font_family" || id == "appearance_font_style")
	{
		const UiTokenValue current = effectiveUiTokenValue(preset, session_.draft().themes[preset], selectedToken);
		if (current.type == UiTokenType::Font)
		{
			UiFontReference font = current.font;
			const UiWidget* widget = controls_.find(id);
			if (widget && action.event.selection >= 0 && static_cast<std::size_t>(action.event.selection) < widget->options().size())
			{
				if (id == "appearance_font_family")
				{
					font.family = widget->options()[static_cast<std::size_t>(action.event.selection)];
					const std::vector<std::string> styles = fontStyles ? fontStyles(font.family) : std::vector<std::string>();
					if (!styles.empty()) font.style = styles[0];
				}
				else font.style = widget->options()[static_cast<std::size_t>(action.event.selection)];
				storeValue(UiTokenValue::fromFont(font));
			}
		}
		return changed;
	}
	if (id == "appearance_reset_token")
	{
		if (selectedToken.empty()) return handled;
		UiThemeCustomization& theme = session_.draft().themes[preset];
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
		if (descriptor && descriptor->category == UiTokenCategory::Colors && theme.palette == PaletteId::Customized)
			theme.customColors[selectedToken] = uiPresetPaletteColor(preset, theme.customPaletteBase, selectedToken);
		else
			theme.overrides.erase(selectedToken);
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_reset_theme")
	{
		session_.draft().themes[preset] = defaultUiThemeCustomization(preset);
		session_.error().clear();
		return changed;
	}
	if (id == "appearance_import") return { true, false, AppearanceEditorEffect::ImportProfile };
	if (id == "appearance_export") return { true, false, AppearanceEditorEffect::ExportProfile };
	if (id == "appearance_cancel") return { true, false, AppearanceEditorEffect::Cancel };
	if (id == "appearance_apply")
	{
		session_.draft().activePreset = preset;
		return { true, false, AppearanceEditorEffect::Apply };
	}
	return unhandled;
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
		"appearance_font_family", "appearance_font_style", "appearance_copy_source" };
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
	paintContext.submit(UiPaintCommand(UiPaintPrimitive::Panel, panel, palette.panel,
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
