#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "UserSettings.h"

struct ThemePalette
{
	const char* id;
	const char* displayName;
	glm::vec4 background;
	glm::vec4 bar;
	glm::vec4 compared;
	glm::vec4 held;
	glm::vec4 shifted;
	glm::vec4 writeTarget;
	glm::vec4 gap;
	glm::vec4 panel;
	glm::vec4 button;
	glm::vec4 disabledButton;
	glm::vec4 valueButton;
	glm::vec4 accent;
	glm::vec4 run;
	glm::vec4 playActive;
	glm::vec4 timelineRail;
	glm::vec4 timelineThumb;
	glm::vec4 reset;
	glm::vec4 text;
	glm::vec4 mutedText;
	glm::vec4 overlayDim;
	glm::vec4 border;
};

const ThemePalette& paletteFor(PaletteId id);
const char* paletteDisplayName(PaletteId id);
const std::vector<PaletteId>& paletteCatalog();
