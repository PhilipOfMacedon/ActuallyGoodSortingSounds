#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Renderer2D.h"
#include "ActiveRoleLookup.h"
#include "ThemePalette.h"
#include "TextLabel.h"
#include "VisualSettingsTypes.h"
#include "Geometry2D.h"
#include "Window.h"
#include "../SortingLogic/SortingLogic.h"

class ArrayRenderer
{
public:
	ArrayRenderer(Window& window, Renderer2D& renderer);
	bool isReady() const;
	void render(
		const sortinglogic::RenderSnapshot& snapshot,
		BarSpacingMode spacing,
		const Rect2D& chartViewport,
		const ThemePalette& palette);

private:
	Window& window_;
	Renderer2D& renderer_;
	std::array<std::vector<QuadInstance>, 6> materialBuckets_;
	std::vector<QuadInstance> gapMarkers_;
	ActiveRoleLookup mainRoleLookup_;
	ActiveRoleLookup workspaceRoleLookup_;
	std::map<std::string, std::unique_ptr<TextLabel> > laneLabels_;
	std::map<std::string, glm::vec2> laneLabelPositions_;
	std::unique_ptr<TextLabel> reconstructionLabel_;
	RenderDescriptor descriptorForRole(sortinglogic::SemanticRole role, const ThemePalette& palette) const;
	RenderDescriptor descriptorForItem(
		std::uint64_t itemId,
		const ActiveRoleLookup& roles,
		const ThemePalette& palette,
		const std::string& lane = "main") const;
	void submitLaneLabel(const std::string& lane, const std::string& caption, float x, float y, const glm::vec4& color);
};
