#pragma once

#include "VisualSettingsTypes.h"

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
	int presetId;
	unsigned long long appearanceRevision;
	unsigned long long fontMetricRevision;
	LayoutCacheKey(int logicalW = 0, int logicalH = 0, int drawableW = 0, int drawableH = 0, bool expanded = true,
		int preset = 0, unsigned long long appearance = 0, unsigned long long fonts = 0)
		: logicalWidth(logicalW), logicalHeight(logicalH), drawableWidth(drawableW), drawableHeight(drawableH),
			panelExpanded(expanded), presetId(preset), appearanceRevision(appearance), fontMetricRevision(fonts) {}
};

bool sameLayoutInputs(const LayoutCacheKey& left, const LayoutCacheKey& right);

BarSlotMetrics calculateBarSlotMetrics(float slotWidth, BarSpacingMode mode);
ResponsivePanelLayout calculatePanelLayout(int logicalWidth, bool expanded);
