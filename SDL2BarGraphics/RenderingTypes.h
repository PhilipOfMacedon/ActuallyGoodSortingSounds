#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "RenderPipeline.h"

struct RenderDescriptor
{
	glm::vec4 tint;
	std::uint32_t textureId;
	MaterialId materialId;

	RenderDescriptor()
		: tint(1.0f), textureId(0), materialId(MaterialId::Bar) {}

	RenderDescriptor(
		const glm::vec4& tintValue,
		std::uint32_t texture,
		MaterialId material)
		: tint(tintValue), textureId(texture), materialId(material) {}
};

struct ClipBounds
{
	float x;
	float y;
	float width;
	float height;
	bool enabled;

	ClipBounds() : x(0.0f), y(0.0f), width(0.0f), height(0.0f), enabled(false) {}
	ClipBounds(float left, float top, float clipWidth, float clipHeight)
		: x(left), y(top), width(clipWidth), height(clipHeight), enabled(true) {}
};

struct QuadInstance
{
	glm::vec2 position;
	glm::vec2 size;
	RenderDescriptor render;
	RenderLayer layer;
	ClipBounds clip;

	QuadInstance(
		const glm::vec2& positionValue,
		const glm::vec2& sizeValue,
		const RenderDescriptor& descriptor,
		RenderLayer layerValue = RenderLayer::ChartBars,
		const ClipBounds& clipValue = ClipBounds())
		: position(positionValue), size(sizeValue), render(descriptor), layer(layerValue), clip(clipValue) {}
};

// Text layout is shared by every skin. Fit preserves the existing compact
// behavior; Clamp keeps the requested type size and clips; Marquee keeps the
// type size and scrolls overflow inside the same measured control bounds.
enum class TextOverflowPolicy { Fit, Clamp, Marquee };
enum class TextHorizontalAlignment { Left, Center, Right };

struct TextQuadLayout
{
	glm::vec2 position;
	glm::vec2 size;
	ClipBounds clip;
	bool visible;
	TextQuadLayout() : position(0.0f), size(0.0f), clip(), visible(false) {}
};

inline ClipBounds intersectClipBounds(const ClipBounds& first, const ClipBounds& second)
{
	if (!first.enabled) return second;
	if (!second.enabled) return first;
	const float left = (std::max)(first.x, second.x);
	const float top = (std::max)(first.y, second.y);
	const float right = (std::min)(first.x + first.width, second.x + second.width);
	const float bottom = (std::min)(first.y + first.height, second.y + second.height);
	return ClipBounds(left, top, (std::max)(0.0f, right - left), (std::max)(0.0f, bottom - top));
}

inline TextQuadLayout layoutTextQuad(const ClipBounds& bounds, const glm::vec2& textureSize,
	float targetHeight, TextOverflowPolicy policy, std::uint32_t tickMilliseconds,
	const ClipBounds& outerClip = ClipBounds(),
	TextHorizontalAlignment alignment = TextHorizontalAlignment::Center)
{
	TextQuadLayout result;
	result.clip = intersectClipBounds(bounds, outerClip);
	if (!bounds.enabled || bounds.width <= 0.0f || bounds.height <= 0.0f ||
		result.clip.width <= 0.0f || result.clip.height <= 0.0f ||
		textureSize.x <= 0.0f || textureSize.y <= 0.0f || targetHeight <= 0.0f)
		return result;

	float height = (std::min)(targetHeight, (std::max)(1.0f, bounds.height - 2.0f));
	float width = textureSize.x * height / textureSize.y;
	const float availableWidth = (std::max)(1.0f, bounds.width - 8.0f);
	const float leftAlignedX = bounds.x + 4.0f;
	const float centeredX = bounds.x + (bounds.width - width) * 0.5f;
	const float rightAlignedX = bounds.x + bounds.width - width - 4.0f;
	float x = leftAlignedX;
	float y = bounds.y + (bounds.height - height) * 0.5f;
	float horizontalOffset = 0.0f;
	if (width <= availableWidth)
		x = alignment == TextHorizontalAlignment::Left ? leftAlignedX :
			alignment == TextHorizontalAlignment::Right ? rightAlignedX : centeredX;
	else if (policy == TextOverflowPolicy::Fit)
	{
		const float factor = availableWidth / width;
		width *= factor;
		height *= factor;
		x = alignment == TextHorizontalAlignment::Left ? leftAlignedX :
			alignment == TextHorizontalAlignment::Right ? rightAlignedX : bounds.x + (bounds.width - width) * 0.5f;
		y = bounds.y + (bounds.height - height) * 0.5f;
	}
	else if (policy == TextOverflowPolicy::Marquee)
	{
		const float overflow = width - availableWidth;
		const float travelMs = overflow / 42.0f * 1000.0f;
		const float pauseMs = 850.0f;
		const float cycleMs = pauseMs * 2.0f + travelMs * 2.0f;
		if (cycleMs > 0.0f)
		{
			const float phase = std::fmod(static_cast<float>(tickMilliseconds), cycleMs);
			if (phase < pauseMs) horizontalOffset = 0.0f;
			else if (phase < pauseMs + travelMs)
				horizontalOffset = -overflow * ((phase - pauseMs) / travelMs);
			else if (phase < pauseMs * 2.0f + travelMs) horizontalOffset = -overflow;
			else horizontalOffset = -overflow * (1.0f - (phase - pauseMs * 2.0f - travelMs) / travelMs);
		}
	}
	result.position = glm::vec2(x + horizontalOffset, y);
	result.size = glm::vec2(width, height);
	result.visible = true;
	return result;
}
