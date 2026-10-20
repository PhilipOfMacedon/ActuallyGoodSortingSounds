#include "SettingsPanelLayout.h"

#include <algorithm>

SettingsPanelGeometry calculateSettingsPanelGeometry(const UiRect& panelBounds, SettingsPanelPage page)
{
	SettingsPanelGeometry result;
	const float x = panelBounds.x + 16.0f;
	const float width = std::max(1.0f, panelBounds.width - 32.0f);
	const float tabWidth = (width - 8.0f) / 3.0f;
	result.tabs[0] = UiRect(x, panelBounds.y + 42.0f, tabWidth, 28.0f);
	result.tabs[1] = UiRect(x + tabWidth + 4.0f, panelBounds.y + 42.0f, tabWidth, 28.0f);
	result.tabs[2] = UiRect(x + 2.0f * (tabWidth + 4.0f), panelBounds.y + 42.0f, tabWidth, 28.0f);
	result.audioGroups[0] = UiRect(x, panelBounds.y + 77.0f, (width - 4.0f) * 0.5f, 24.0f);
	result.audioGroups[1] = UiRect(x + (width + 4.0f) * 0.5f, panelBounds.y + 77.0f,
		(width - 4.0f) * 0.5f, 24.0f);
	const float contentOffset = page == SettingsPanelPage::GeneralAudio ? 112.0f : 82.0f;
	result.content = UiRect(x, panelBounds.y + contentOffset, width,
		std::max(28.0f, panelBounds.height - contentOffset - 60.0f));
	const float footerY = panelBounds.y + panelBounds.height - 38.0f;
	result.closeButton = UiRect(panelBounds.x + panelBounds.width - 92.0f, footerY, 76.0f, 26.0f);
	result.audioRetryButton = UiRect(x, footerY, std::min(190.0f, width - 90.0f), 26.0f);
	return result;
}

SettingsWidgetGeometry calculateSettingsWidgetGeometry(const UiWidget& widget, const UiRect& rowBounds,
	float x, float width, const UiRect& panelBounds, float scrollbarThickness)
{
	SettingsWidgetGeometry result;
	if (widget.kind() == UiWidgetKind::ComboBox)
		result.bounds = UiRect(x, rowBounds.y + 17.0f, width, 27.0f);
	else if (widget.kind() == UiWidgetKind::Button)
		result.bounds = UiRect(x, rowBounds.y + 8.0f, width, 28.0f);
	else
		result.bounds = UiRect(x, rowBounds.y, width, 43.0f);

	if (widget.presentation() == UiWidgetPresentation::RadioGroup && !widget.options().empty())
	{
		const float segment = result.bounds.width / static_cast<float>(widget.options().size());
		for (std::size_t option = 0; option < widget.options().size(); ++option)
			result.interactiveSubregions.push_back(UiRect(result.bounds.x + static_cast<float>(option) * segment,
				result.bounds.y, segment, result.bounds.height));
	}
	else if (widget.kind() == UiWidgetKind::Slider && widget.presentation() == UiWidgetPresentation::DosScrollbar)
	{
		const float arrow = std::min(widget.scrollbarArrowSize(), result.bounds.width * 0.25f);
		const float trackHeight = std::min(scrollbarThickness, std::max(1.0f, result.bounds.height - 29.0f));
		const float y = result.bounds.y + 28.0f;
		if (arrow > 0.0f)
		{
			result.interactiveSubregions.push_back(UiRect(result.bounds.x, y - 2.0f, arrow, trackHeight + 4.0f));
			result.interactiveSubregions.push_back(UiRect(result.bounds.x + arrow, y,
				std::max(1.0f, result.bounds.width - 2.0f * arrow), trackHeight));
			result.interactiveSubregions.push_back(UiRect(result.bounds.x + result.bounds.width - arrow,
				y - 2.0f, arrow, trackHeight + 4.0f));
		}
	}
	if (widget.id() == "band_selector")
	{
		result.setPopupAbove = true;
		result.popupAbove = result.bounds.y + result.bounds.height * 9.0f > panelBounds.y + panelBounds.height - 46.0f;
	}
	return result;
}
