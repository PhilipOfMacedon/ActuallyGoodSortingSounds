#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Renderer2D.h"
#include "ThemePalette.h"
#include "TextLabel.h"
#include "UserSettings.h"
#include "Window.h"
#include "../SortingLogic/SortingLogic.h"

class ArrayRenderer
{
public:
	ArrayRenderer(Window& window, Renderer2D& renderer);
	bool isReady() const;
	void render(
		const sortinglogic::RenderSnapshot& snapshot,
		const UserSettings& settings,
		float chartTop);

private:
	Window& window_;
	Renderer2D& renderer_;
	std::array<std::vector<QuadInstance>, 6> materialBuckets_;
	std::vector<QuadInstance> gapMarkers_;
	std::map<std::string, std::unique_ptr<TextLabel> > laneLabels_;
	std::map<std::string, glm::vec2> laneLabelPositions_;
	RenderDescriptor descriptorForRole(sortinglogic::SemanticRole role, const ThemePalette& palette) const;
	RenderDescriptor descriptorForItem(
		std::uint64_t itemId,
		const std::vector<sortinglogic::ActiveRole>& roles,
		const ThemePalette& palette,
		const std::string& lane = "main") const;
	void submitLaneLabel(const std::string& lane, float x, float y, const glm::vec4& color);
};
