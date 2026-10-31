#include "AppearanceEditorLayout.h"

#include <algorithm>

AppearanceEditorLayoutResult::AppearanceEditorLayoutResult()
	: valid(false), panelBounds(), titleBounds(), descriptionBounds(), statusBounds(), colorSwatchBounds(), contentClip(),
	  scale(1.0f), controls()
{
}

UiRect AppearanceEditorLayoutResult::control(const std::string& id) const
{
	const std::map<std::string, UiRect>::const_iterator found = controls.find(id);
	return found == controls.end() ? UiRect() : found->second;
}

AppearanceEditorLayoutResult calculateAppearanceEditorLayout(float logicalWidth, float logicalHeight)
{
	AppearanceEditorLayoutResult result;
	if (logicalWidth <= 0.0f || logicalHeight <= 0.0f) return result;
	result.valid = true;

	const float margin = 12.0f;
	const float panelWidth = std::min(880.0f, std::max(1.0f, logicalWidth - margin * 2.0f));
	const float panelHeight = std::min(660.0f, std::max(1.0f, logicalHeight - margin * 2.0f));
	result.panelBounds = UiRect((logicalWidth - panelWidth) * 0.5f,
		(logicalHeight - panelHeight) * 0.5f, panelWidth, panelHeight);
	const float left = result.panelBounds.x + 18.0f;
	const float contentWidth = std::max(1.0f, result.panelBounds.width - 36.0f);
	result.scale = std::min(1.0f, panelHeight / 660.0f);
	const float gap = 8.0f * result.scale;
	const float row = 30.0f * result.scale;
	const float top = result.panelBounds.y + 58.0f * result.scale;
	const float half = std::max(1.0f, (contentWidth - gap) * 0.5f);
	const float filterY = top + row + 8.0f * result.scale;
	const float copyRowY = filterY + 2.0f * row + 16.0f * result.scale;
	const float propertyY = result.panelBounds.y + 250.0f * result.scale;
	const float propertyButton = std::min(54.0f, half);
	const float colorActionsY = propertyY + 50.0f * result.scale;
	const float colorSliderY = colorActionsY + row + 8.0f * result.scale;
	const float bottomY = result.panelBounds.y + panelHeight - 40.0f * result.scale;
	const float actionsY = bottomY - 2.0f * row - 2.0f * gap;

	result.titleBounds = UiRect(result.panelBounds.x + 18.0f, result.panelBounds.y + 14.0f,
		result.panelBounds.width - 36.0f, 26.0f);
	result.descriptionBounds = UiRect(left, result.panelBounds.y + 210.0f * result.scale,
		contentWidth, 32.0f * result.scale);
	result.statusBounds = UiRect(left, actionsY - 26.0f * result.scale,
		contentWidth, 18.0f * result.scale);
	result.contentClip = UiRect(result.panelBounds.x + 1.0f, result.panelBounds.y + 40.0f * result.scale,
		result.panelBounds.width - 2.0f,
		std::max(0.0f, result.panelBounds.height - 120.0f * result.scale));
	result.colorSwatchBounds = UiRect(left, propertyY, contentWidth, 42.0f * result.scale);
	result.controls["appearance_preset"] = UiRect(left, top, half, row);
	result.controls["appearance_palette"] = UiRect(left + half + gap, top, half, row);
	result.controls["appearance_category"] = UiRect(left, filterY, half, row);
	result.controls["appearance_search"] = UiRect(left + half + gap, filterY, half, row);
	result.controls["appearance_token"] = UiRect(left, filterY + row + 8.0f * result.scale, contentWidth, row);
	result.controls["appearance_copy_source"] = UiRect(left, copyRowY, half, row);
	result.controls["appearance_copy_colors"] = UiRect(left + half + gap, copyRowY, half, row);
	result.controls["appearance_value_down"] = UiRect(left, propertyY, propertyButton, row);
	result.controls["appearance_value"] = UiRect(left + propertyButton + gap, propertyY,
		std::max(1.0f, contentWidth - 2.0f * (propertyButton + gap)), row);
	result.controls["appearance_value_up"] = UiRect(left + contentWidth - propertyButton, propertyY, propertyButton, row);
	result.controls["appearance_value_slider"] = UiRect(left, propertyY + row + gap, contentWidth, 42.0f * result.scale);
	result.controls["appearance_color"] = UiRect(left, colorActionsY, half, row);
	result.controls["appearance_color_alpha"] = UiRect(left + half + gap, colorActionsY, half, row);
	const char* colorSliderIds[] = { "appearance_color_r", "appearance_color_g", "appearance_color_b", "appearance_color_a" };
	for (std::size_t i = 0; i < sizeof(colorSliderIds) / sizeof(colorSliderIds[0]); ++i)
		result.controls[colorSliderIds[i]] = UiRect(left, colorSliderY + static_cast<float>(i) * 42.0f * result.scale,
			contentWidth, 40.0f * result.scale);
	result.controls["appearance_boolean"] = UiRect(left, propertyY, contentWidth, row);
	result.controls["appearance_choice"] = UiRect(left, propertyY, contentWidth, row);
	result.controls["appearance_font_family"] = UiRect(left, propertyY, contentWidth, row);
	result.controls["appearance_font_style"] = UiRect(left, propertyY + row + gap, contentWidth, row);
	result.controls["appearance_reset_token"] = UiRect(left, actionsY, half, row);
	result.controls["appearance_reset_theme"] = UiRect(left + half + gap, actionsY, half, row);
	result.controls["appearance_import"] = UiRect(left, actionsY + row + gap, half, row);
	result.controls["appearance_export"] = UiRect(left + half + gap, actionsY + row + gap, half, row);
	result.controls["appearance_cancel"] = UiRect(left, bottomY, half, row);
	result.controls["appearance_apply"] = UiRect(left + half + gap, bottomY, half, row);
	return result;
}
