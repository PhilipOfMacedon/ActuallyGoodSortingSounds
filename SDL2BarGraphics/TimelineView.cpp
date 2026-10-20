#include "TimelineView.h"

#include <algorithm>
#include <string>

#include "../SortingLogic/SortingLogic.h"

namespace
{
	void submitRectangle(UiPaintContext& paintContext, const UiRect& bounds, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer, const UiRect& screen, const std::string& id)
	{
		if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
		paintContext.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, bounds, color, std::string(), id,
			material, layer, screen, true, 0.0f));
	}

	UiRect clampedProgressRect(const UiRect& track, float fraction)
	{
		return UiRect(track.x, track.y, std::max(0.0f, track.width * fraction), track.height);
	}
}

TimelineView::TimelineView()
	: trackBounds_(), controlBounds_(), leftArrowBounds_(), rightArrowBounds_(), dragging_(false)
{
}

void TimelineView::applyLayout(const UiLayoutResult& layout)
{
	trackBounds_ = layout.timelineTrack.width > 0.0f ? layout.timelineTrack : layout.timeline;
	controlBounds_ = layout.timeline;
	leftArrowBounds_ = layout.timelineLeftArrow;
	rightArrowBounds_ = layout.timelineRightArrow;
}

bool TimelineView::pointerDown(float x, float y, bool enabled, AppAction& action)
{
	if (!enabled || !controlBounds_.contains(x, y)) return false;
	if (leftArrowBounds_.contains(x, y))
	{
		action = AppAction(AppActionKind::PreviousEvent);
		return true;
	}
	if (rightArrowBounds_.contains(x, y))
	{
		action = AppAction(AppActionKind::NextEvent);
		return true;
	}
	if (!trackBounds_.contains(x, y)) return false;
	dragging_ = true;
	action = AppAction(AppActionKind::SeekFraction, fractionAt(x));
	return true;
}

bool TimelineView::pointerMove(float x, AppAction& action)
{
	if (!dragging_) return false;
	action = AppAction(AppActionKind::SeekFraction, fractionAt(x));
	return true;
}

bool TimelineView::pointerUp(float x, AppAction& action)
{
	if (!dragging_) return false;
	dragging_ = false;
	action = AppAction(AppActionKind::SeekFraction, fractionAt(x));
	return true;
}

void TimelineView::cancelDrag()
{
	dragging_ = false;
}

float TimelineView::fractionAt(float x) const
{
	if (trackBounds_.width <= 0.0f) return 0.0f;
	return std::max(0.0f, std::min(1.0f, (x - trackBounds_.x) / trackBounds_.width));
}

void TimelineView::compose(bool enabled, bool classicVga, bool windows95, double timelineDuration,
	const sortinglogic::RenderSnapshot& snapshot, const TimelineViewPalette& palette,
	const UiRect& screen, UiPaintContext& paintContext) const
{
	if (!enabled || trackBounds_.width <= 0.0f || trackBounds_.height <= 0.0f ||
		(screen.width <= 0.0f || screen.height <= 0.0f)) return;
	if (!snapshot.hasTrace && !snapshot.buildingTrace && !classicVga) return;
	const double rawFraction = (!snapshot.hasTrace && !snapshot.buildingTrace) || timelineDuration <= 0.0 ? 0.0 :
		static_cast<double>(snapshot.elapsedSeconds) / timelineDuration;
	const float fraction = static_cast<float>(std::max(0.0, std::min(1.0, rawFraction)));
	submitRectangle(paintContext, trackBounds_, palette.rail, UiPaintMaterial::Timeline,
		UiPaintLayer::MainControls, screen, "timeline.track");
	submitRectangle(paintContext, clampedProgressRect(trackBounds_, fraction), palette.accent,
		UiPaintMaterial::Accent, UiPaintLayer::MainControlsOverlay, screen, "timeline.progress");
	if (classicVga)
	{
		const float tickHeight = std::min(7.0f, trackBounds_.height * 0.45f);
		for (int tick = 0; tick <= 10; ++tick)
		{
			const float x = trackBounds_.x + trackBounds_.width * static_cast<float>(tick) / 10.0f;
			submitRectangle(paintContext, UiRect(x, trackBounds_.y + trackBounds_.height - tickHeight, 1.0f, tickHeight),
				palette.text, UiPaintMaterial::PixelIcon, UiPaintLayer::MainControlsTop, screen,
				"timeline.tick." + std::to_string(tick));
		}
	}
	const float thumbWidth = classicVga ? std::min(12.0f, trackBounds_.width) : 8.0f;
	submitRectangle(paintContext, UiRect(trackBounds_.x + std::max(0.0f, trackBounds_.width * fraction - thumbWidth * 0.5f),
		trackBounds_.y - 3.0f, thumbWidth, trackBounds_.height + 6.0f), palette.thumb,
		UiPaintMaterial::TimelineThumb, UiPaintLayer::MainControlsOverlay, screen, "timeline.thumb");

	if (leftArrowBounds_.width <= 0.0f || rightArrowBounds_.width <= 0.0f) return;
	const UiRect arrows[] = { leftArrowBounds_, rightArrowBounds_ };
	for (int side = 0; side < 2; ++side)
	{
		const UiRect& arrow = arrows[side];
		submitRectangle(paintContext, arrow, palette.button, UiPaintMaterial::Button,
			UiPaintLayer::MainControls, screen, side == 0 ? "timeline.left_arrow" : "timeline.right_arrow");
		submitRectangle(paintContext, UiRect(arrow.x, arrow.y, arrow.width, 1.0f), palette.brightEdge,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay, screen, "timeline.arrow.top_edge");
		submitRectangle(paintContext, UiRect(arrow.x, arrow.y, 1.0f, arrow.height), palette.brightEdge,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay, screen, "timeline.arrow.left_edge");
		submitRectangle(paintContext, UiRect(arrow.x, arrow.y + arrow.height - 1.0f, arrow.width, 1.0f), palette.darkEdge,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay, screen, "timeline.arrow.bottom_edge");
		submitRectangle(paintContext, UiRect(arrow.x + arrow.width - 1.0f, arrow.y, 1.0f, arrow.height), palette.darkEdge,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay, screen, "timeline.arrow.right_edge");
		const char* leftArrow[] = { "..#", ".##", "###", ".##", "..#" };
		const char* rightArrow[] = { "#..", "##.", "###", "##.", "#.." };
		const char* const* pattern = side == 0 ? leftArrow : rightArrow;
		const float pixel = std::max(1.0f, std::min(2.0f, arrow.height / 8.0f));
		const float iconWidth = pixel * 3.0f;
		const float iconHeight = pixel * 5.0f;
		const float iconX = arrow.x + (arrow.width - iconWidth) * 0.5f;
		const float iconY = arrow.y + (arrow.height - iconHeight) * 0.5f;
		for (int row = 0; row < 5; ++row)
			for (int column = 0; column < 3; ++column)
				if (pattern[row][column] == '#')
					submitRectangle(paintContext, UiRect(iconX + column * pixel, iconY + row * pixel, pixel, pixel),
						palette.text, UiPaintMaterial::PixelIcon, UiPaintLayer::MainControlsTop, screen, "timeline.arrow.pixel");
	}
}
