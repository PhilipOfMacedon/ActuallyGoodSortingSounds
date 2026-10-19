#include "LayoutMetrics.h"

#include <algorithm>

bool sameLayoutInputs(const LayoutCacheKey& left, const LayoutCacheKey& right)
{
	return left.logicalWidth == right.logicalWidth && left.logicalHeight == right.logicalHeight &&
		left.drawableWidth == right.drawableWidth && left.drawableHeight == right.drawableHeight &&
		left.panelExpanded == right.panelExpanded && left.presetId == right.presetId &&
		left.appearanceRevision == right.appearanceRevision && left.fontMetricRevision == right.fontMetricRevision;
}

BarSlotMetrics calculateBarSlotMetrics(float slotWidth, BarSpacingMode mode)
{
	const float safeWidth = std::max(0.0f, slotWidth);
	float gutter = 0.0f;
	switch (mode)
	{
	case BarSpacingMode::None:
		break;
	case BarSpacingMode::Compact:
		// Preserve the previous two-sided pad exactly: min(2.5 px total, 16% of a slot).
		gutter = std::min(2.5f, safeWidth * 0.16f);
		break;
	case BarSpacingMode::Comfortable:
		gutter = std::min(5.0f, safeWidth * 0.24f);
		break;
	}
	gutter = std::min(gutter, safeWidth * 0.45f);
	BarSlotMetrics result;
	result.gutter = gutter;
	result.sidePadding = gutter * 0.5f;
	result.barWidth = std::max(0.0f, safeWidth - gutter);
	return result;
}

ResponsivePanelLayout calculatePanelLayout(int logicalWidth, bool expanded)
{
	ResponsivePanelLayout result = {};
	result.wide = logicalWidth >= 900;
	result.rowCount = result.wide ? 2 : 4;
	result.rowY[0] = 48.0f;
	result.rowY[1] = result.wide ? 84.0f : 83.0f;
	result.rowY[2] = 118.0f;
	result.rowY[3] = 153.0f;
	result.timelineY = result.wide ? 126.0f : 194.0f;
	result.chartTop = !expanded ? 78.0f : (result.wide ? 184.0f : 250.0f);
	return result;
}
