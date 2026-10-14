#pragma once

#include "UserSettings.h"

struct BarSlotMetrics
{
	float gutter;
	float barWidth;
	float sidePadding;
};

struct ResponsivePanelLayout
{
	bool wide;
	int rowCount;
	float rowY[4];
	float timelineY;
	float chartTop;
};

struct LayoutCacheKey
{
	int logicalWidth;
	int logicalHeight;
	int drawableWidth;
	int drawableHeight;
	bool panelExpanded;
	LayoutCacheKey(int logicalW = 0, int logicalH = 0, int drawableW = 0, int drawableH = 0, bool expanded = true)
		: logicalWidth(logicalW), logicalHeight(logicalH), drawableWidth(drawableW), drawableHeight(drawableH), panelExpanded(expanded) {}
};

bool sameLayoutInputs(const LayoutCacheKey& left, const LayoutCacheKey& right);

BarSlotMetrics calculateBarSlotMetrics(float slotWidth, BarSpacingMode mode);
ResponsivePanelLayout calculatePanelLayout(int logicalWidth, bool expanded);
