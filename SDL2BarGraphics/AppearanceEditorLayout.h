#pragma once

#include <map>
#include <string>

#include "UiLayer.h"

struct AppearanceEditorLayoutResult
{
	bool valid;
	UiRect panelBounds;
	UiRect titleBounds;
	UiRect descriptionBounds;
	UiRect statusBounds;
	UiRect colorSwatchBounds;
	UiRect contentClip;
	float scale;
	std::map<std::string, UiRect> controls;

	AppearanceEditorLayoutResult();
	UiRect control(const std::string& id) const;
};

// Pure logical-coordinate layout for the appearance editor. Widget state and
// SDL input remain with the owning view; this function only resolves geometry.
AppearanceEditorLayoutResult calculateAppearanceEditorLayout(float logicalWidth, float logicalHeight);
