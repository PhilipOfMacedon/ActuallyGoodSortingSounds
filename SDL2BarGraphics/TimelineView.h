#pragma once

#include "AppActions.h"
#include "UiArchitecture.h"

namespace sortinglogic
{
	struct RenderSnapshot;
}

struct TimelineViewPalette
{
	UiColor rail;
	UiColor accent;
	UiColor thumb;
	UiColor button;
	UiColor text;
	UiColor border;
	UiColor brightEdge;
	UiColor darkEdge;
};

// Owns timeline hit regions, pointer capture, seek projection, and paint
// composition. SDL coordinates are translated by the host; drawing remains in
// the shared UI paint pipeline.
class TimelineView
{
public:
	TimelineView();

	void applyLayout(const UiLayoutResult& layout);
	bool pointerDown(float x, float y, bool enabled, AppAction& action);
	bool pointerMove(float x, AppAction& action);
	bool pointerUp(float x, AppAction& action);
	void cancelDrag();
	float fractionAt(float x) const;

	const UiRect& trackBounds() const { return trackBounds_; }
	const UiRect& controlBounds() const { return controlBounds_; }
	const UiRect& leftArrowBounds() const { return leftArrowBounds_; }
	const UiRect& rightArrowBounds() const { return rightArrowBounds_; }
	bool dragging() const { return dragging_; }

	void compose(bool enabled, bool classicVga, bool windows95, double timelineDuration,
		const sortinglogic::RenderSnapshot& snapshot, const TimelineViewPalette& palette,
		const UiRect& screen, UiPaintContext& paintContext) const;

private:
	UiRect trackBounds_;
	UiRect controlBounds_;
	UiRect leftArrowBounds_;
	UiRect rightArrowBounds_;
	bool dragging_;
};
