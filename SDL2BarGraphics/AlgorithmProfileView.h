#pragma once

#include <string>

#include "TextBlockLayout.h"
#include "UiArchitecture.h"

struct AlgorithmProfilePalette
{
	UiColor background;
	UiColor panel;
	UiColor border;
	UiColor timelineRail;
	UiColor accent;
	UiColor text;
	UiColor mutedText;
};

// Composes the read-only algorithm profile pane and owns its description
// wrapping and scroll interaction. It consumes UI presentation values only.
class AlgorithmProfileView
{
public:
	explicit AlgorithmProfileView(const TextBlockLayout::MeasureTextWidth& measureTextWidth);

	void beginFrame();
	void compose(const std::string& algorithm, const UiAlgorithmCapabilities& capabilities,
		bool enabled, int logicalWidth, const UiRect& setupRail, const UiRect& seedBounds,
		const ResolvedTextStyle& profileTextStyle, const ResolvedTextStyle& complexityTextStyle,
		const AlgorithmProfilePalette& palette, UiPaintContext& paintContext);

	bool visible() const { return visible_; }
	bool handleWheel(float x, float y, int wheelDelta);
	bool pointerDown(float x, float y);
	bool pointerMove(float y);
	bool pointerUp();
	float scrollOffset() const { return scrollViewport_.offset(); }
	float maximumScrollOffset() const { return scrollViewport_.maximumOffset(); }
	const UiRect& scrollViewportBounds() const { return scrollViewport_.viewport(); }
	const UiRect& scrollTrackBounds() const { return scrollTrack_; }
	const UiRect& scrollThumbBounds() const { return scrollThumb_; }

private:
	TextBlockLayout complexityLayout_;
	TextBlockLayout descriptionLayout_;
	UiScrollViewport scrollViewport_;
	UiRect scrollTrack_;
	UiRect scrollThumb_;
	std::string algorithm_;
	float lineHeight_;
	bool visible_;
	bool dragging_;
	float dragStartY_;
	float dragStartOffset_;
};
