#pragma once

#include <cstdint>

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
