#include "AlgorithmProfileView.h"

#include <algorithm>
#include <sstream>

namespace
{
	void submitRectangle(UiPaintContext& context, const UiRect& bounds, const UiColor& color,
		UiPaintMaterial material, UiPaintLayer layer)
	{
		if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
		context.submit(UiPaintCommand(UiPaintPrimitive::Rectangle, bounds, color, std::string(),
			std::string(), material, layer, UiRect(), false, 12.0f));
	}

	void submitText(UiPaintContext& context, const UiRect& bounds, const std::string& text,
		const std::string& elementId, float height, const UiColor& color, UiTextAlignment alignment,
		UiTextOverflow overflow, const UiRect& clip, bool clipped = true)
	{
		if (bounds.width <= 0.0f || bounds.height <= 0.0f || text.empty()) return;
		UiPaintCommand command(UiPaintPrimitive::Text, bounds, color, text, elementId,
			UiPaintMaterial::Solid, UiPaintLayer::MainText, clip, clipped, height);
		command.textAlignment = alignment;
		command.textOverflow = overflow;
		context.submit(command);
	}

	void submitBorder(UiPaintContext& context, const UiRect& bounds, const UiColor& color, float thickness)
	{
		submitRectangle(context, UiRect(bounds.x, bounds.y, bounds.width, thickness), color,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay);
		submitRectangle(context, UiRect(bounds.x, bounds.y + bounds.height - thickness, bounds.width, thickness), color,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay);
		submitRectangle(context, UiRect(bounds.x, bounds.y, thickness, bounds.height), color,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay);
		submitRectangle(context, UiRect(bounds.x + bounds.width - thickness, bounds.y, thickness, bounds.height), color,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay);
	}
}

AlgorithmProfileView::AlgorithmProfileView(const TextBlockLayout::MeasureTextWidth& measureTextWidth)
	: complexityLayout_(measureTextWidth), descriptionLayout_(measureTextWidth), scrollViewport_(), scrollTrack_(), scrollThumb_(),
	algorithm_(), lineHeight_(18.0f), visible_(false), dragging_(false), dragStartY_(0.0f), dragStartOffset_(0.0f)
{
}

void AlgorithmProfileView::beginFrame()
{
	visible_ = false;
	scrollTrack_ = UiRect();
	scrollThumb_ = UiRect();
}

void AlgorithmProfileView::compose(const std::string& algorithm, const UiAlgorithmCapabilities& capabilities,
	bool enabled, int logicalWidth, const UiRect& setupRail, const UiRect& seedBounds,
	const ResolvedTextStyle& profileTextStyle, const ResolvedTextStyle& complexityTextStyle,
	const AlgorithmProfilePalette& palette, UiPaintContext& paintContext)
{
	beginFrame();
	if (!enabled || logicalWidth < 900 || setupRail.height <= 220.0f) return;
	if (algorithm_ != algorithm)
	{
		algorithm_ = algorithm;
		scrollViewport_.setOffset(0.0f);
	}

	const float cardY = (std::max)(seedBounds.y + seedBounds.height + 12.0f, setupRail.y + 258.0f);
	const float cardBottom = setupRail.y + setupRail.height - 10.0f;
	const float cardHeight = (std::max)(0.0f, cardBottom - cardY);
	if (cardHeight < 78.0f) return;
	visible_ = true;

	const UiRect card(setupRail.x + 8.0f, cardY, (std::max)(0.0f, setupRail.width - 16.0f), cardHeight);
	submitRectangle(paintContext, card, palette.background, UiPaintMaterial::Panel, UiPaintLayer::MainControls);
	submitBorder(paintContext, card, palette.border, 1.0f);

	const float profileWidth = (std::max)(0.0f, card.width - 16.0f);
	std::ostringstream inputTypes;
	inputTypes << "Supported input: ";
	if (capabilities.supportsNormalizedFloat) inputTypes << "FLOAT";
	if (capabilities.supportsNormalizedFloat && capabilities.supportsIntegerKeys) inputTypes << " / ";
	if (capabilities.supportsIntegerKeys) inputTypes << "INTEGER";
	if (!capabilities.supportsNormalizedFloat && !capabilities.supportsIntegerKeys) inputTypes << "NONE";
	std::ostringstream safeLimit;
	safeLimit << "Safe setup limit: " << capabilities.safeMaximumItemCount << " bars";

	const bool showShortcuts = cardHeight >= 112.0f;
	float shortcutY = card.y + 70.0f;
	if (showShortcuts && cardHeight >= 150.0f) shortcutY = card.y + card.height - 58.0f;
	const float profileBottom = showShortcuts ? shortcutY - 10.0f : card.y + card.height - 8.0f;
	const std::string complexity = (capabilities.available ? capabilities.timeComplexity : "Unknown") +
		std::string(" time  |  ") + (capabilities.available ? capabilities.spaceComplexity : "Unknown") + " space";
	complexityLayout_.update(complexity, profileWidth, complexityTextStyle);
	const std::vector<std::string>& complexityLines = complexityLayout_.lines();
	const float complexityLineHeight = 13.0f;
	const UiRect cardClip(card.x + 2.0f, card.y + 2.0f, (std::max)(0.0f, card.width - 4.0f),
		(std::max)(0.0f, card.height - 4.0f));
	submitText(paintContext, UiRect(card.x + 8.0f, card.y + 8.0f, profileWidth, 15.0f),
		"ALGORITHM PROFILE", "#rail_profile_title", 12.0f, palette.text, UiTextAlignment::Center,
		UiTextOverflow::Clamp, cardClip);
	for (std::size_t lineIndex = 0; lineIndex < complexityLines.size(); ++lineIndex)
	{
		submitText(paintContext, UiRect(card.x + 8.0f, card.y + 27.0f + lineIndex * complexityLineHeight,
			profileWidth, complexityLineHeight), complexityLines[lineIndex],
			"#rail_profile_complexity_" + std::to_string(lineIndex), complexityTextStyle.logicalHeight,
			palette.mutedText, UiTextAlignment::Center, UiTextOverflow::Clamp, cardClip);
	}

	const float factsY = card.y + 30.0f + static_cast<float>(complexityLines.size()) * complexityLineHeight + 3.0f;
	const std::string properties = std::string(capabilities.stable ? "STABLE" : "UNSTABLE") + "  |  " +
		(capabilities.inPlace ? "IN-PLACE" : "AUXILIARY SPACE");
	const std::string factTexts[] = { properties, inputTypes.str(), safeLimit.str() };
	const char* factIds[] = { "#rail_profile_properties", "#rail_profile_input", "#rail_profile_limit" };
	const float factLineHeight = 18.0f;
	for (std::size_t factIndex = 0; factIndex < 3; ++factIndex)
	{
		if (cardHeight < factsY - card.y + (factIndex + 1) * factLineHeight + 8.0f) break;
		submitText(paintContext, UiRect(card.x + 8.0f, factsY + factIndex * factLineHeight,
			profileWidth, factLineHeight), factTexts[factIndex], factIds[factIndex], 13.0f,
			palette.text, UiTextAlignment::Center, UiTextOverflow::Clamp, cardClip);
	}

	const float descriptionTop = factsY + 3.0f * factLineHeight + 3.0f;
	const UiRect profileFrame(card.x + 6.0f, descriptionTop, (std::max)(0.0f, card.width - 12.0f),
		(std::max)(0.0f, profileBottom - descriptionTop));
	const UiRect profileTextViewport(profileFrame.x + 7.0f, profileFrame.y + 6.0f,
		(std::max)(0.0f, profileFrame.width - 24.0f), (std::max)(0.0f, profileFrame.height - 12.0f));
	if (!capabilities.behavior.empty() && profileTextViewport.height > 0.0f)
	{
		submitRectangle(paintContext, profileFrame, palette.panel, UiPaintMaterial::Button, UiPaintLayer::MainControls);
		submitBorder(paintContext, profileFrame, palette.border, 1.0f);
		descriptionLayout_.update(capabilities.behavior, profileTextViewport.width, profileTextStyle);
		const std::vector<std::string>& descriptionLines = descriptionLayout_.lines();
		lineHeight_ = (std::max)(17.0f, profileTextStyle.logicalHeight * 1.42f);
		scrollViewport_.configure(profileTextViewport,
			static_cast<float>(descriptionLines.size()) * lineHeight_, lineHeight_, scrollViewport_.offset());
		const std::size_t firstLine = scrollViewport_.firstVisibleItem(lineHeight_);
		const std::size_t visibleLines = scrollViewport_.visibleItemCount(descriptionLines.size(), lineHeight_);
		const std::size_t endLine = (std::min)(descriptionLines.size(), firstLine + visibleLines);
		for (std::size_t lineIndex = firstLine; lineIndex < endLine; ++lineIndex)
		{
			const float lineY = profileTextViewport.y + static_cast<float>(lineIndex) * lineHeight_ - scrollViewport_.offset();
			submitText(paintContext, UiRect(profileTextViewport.x, lineY, profileTextViewport.width, lineHeight_),
				descriptionLines[lineIndex], "#rail_profile_line_" + std::to_string(lineIndex),
				profileTextStyle.logicalHeight, palette.text, UiTextAlignment::Left,
				UiTextOverflow::Clamp, profileTextViewport);
		}
	}
	else
		scrollViewport_.configure(UiRect(), 0.0f, profileTextStyle.logicalHeight, 0.0f);

	if (scrollViewport_.maximumOffset() > 0.0f && profileFrame.height > 0.0f)
	{
		scrollTrack_ = UiRect(profileFrame.x + profileFrame.width - 11.0f,
			profileTextViewport.y, 5.0f, profileTextViewport.height);
		submitRectangle(paintContext, scrollTrack_, palette.timelineRail,
			UiPaintMaterial::Timeline, UiPaintLayer::MainControls);
		const float thumbHeight = (std::min)(scrollTrack_.height,
			(std::max)(18.0f, scrollTrack_.height * profileTextViewport.height /
				(std::max)(1.0f, scrollViewport_.contentExtent())));
		const float thumbTravel = (std::max)(0.0f, scrollTrack_.height - thumbHeight);
		const float scrollFraction = scrollViewport_.maximumOffset() > 0.0f
			? scrollViewport_.offset() / scrollViewport_.maximumOffset() : 0.0f;
		scrollThumb_ = UiRect(scrollTrack_.x, scrollTrack_.y + thumbTravel * scrollFraction,
			scrollTrack_.width, thumbHeight);
		submitRectangle(paintContext, scrollThumb_, palette.accent,
			UiPaintMaterial::TimelineThumb, UiPaintLayer::MainControlsOverlay);
	}

	if (showShortcuts)
	{
		const float separatorY = shortcutY - 8.0f;
		submitRectangle(paintContext, UiRect(card.x + 8.0f, separatorY,
			(std::max)(0.0f, card.width - 16.0f), 1.0f), palette.border,
			UiPaintMaterial::Button, UiPaintLayer::MainControlsOverlay);
		submitText(paintContext, UiRect(card.x + 8.0f, shortcutY, card.width - 16.0f, 15.0f),
			"KEYBOARD", "#rail_shortcuts_title", 12.0f, palette.text, UiTextAlignment::Center,
			UiTextOverflow::Fit, UiRect(), false);
		submitText(paintContext, UiRect(card.x + 8.0f, shortcutY + 18.0f, card.width - 16.0f, 15.0f),
			"SPACE Play  |  1 Build  |  D Order", "#rail_shortcuts", 11.0f,
			palette.mutedText, UiTextAlignment::Center, UiTextOverflow::Fit, UiRect(), false);
		submitText(paintContext, UiRect(card.x + 8.0f, shortcutY + 34.0f, card.width - 16.0f, 15.0f),
			"ALT+ENTER Fullscreen", "#rail_fullscreen", 11.0f, palette.mutedText,
			UiTextAlignment::Center, UiTextOverflow::Fit, UiRect(), false);
	}
}

bool AlgorithmProfileView::handleWheel(float x, float y, int wheelDelta)
{
	return visible_ && scrollViewport_.scrollWheel(x, y, wheelDelta);
}

bool AlgorithmProfileView::pointerDown(float x, float y)
{
	if (!visible_ || scrollViewport_.maximumOffset() <= 0.0f || !scrollTrack_.contains(x, y)) return false;
	const float trackTravel = scrollTrack_.height - scrollThumb_.height;
	if (trackTravel > 0.0f && !scrollThumb_.contains(x, y))
	{
		const float fraction = (std::max)(0.0f, (std::min)(1.0f,
			(y - scrollTrack_.y - scrollThumb_.height * 0.5f) / trackTravel));
		scrollViewport_.setOffset(fraction * scrollViewport_.maximumOffset());
	}
	dragging_ = true;
	dragStartY_ = y;
	dragStartOffset_ = scrollViewport_.offset();
	return true;
}

bool AlgorithmProfileView::pointerMove(float y)
{
	if (!dragging_) return false;
	const float trackTravel = scrollTrack_.height - scrollThumb_.height;
	if (trackTravel > 0.0f)
	{
		const float ratio = scrollViewport_.maximumOffset() / trackTravel;
		scrollViewport_.setOffset(dragStartOffset_ + (y - dragStartY_) * ratio);
	}
	return true;
}

bool AlgorithmProfileView::pointerUp()
{
	if (!dragging_) return false;
	dragging_ = false;
	return true;
}
