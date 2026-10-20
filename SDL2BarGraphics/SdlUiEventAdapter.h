#pragma once

#include <SDL.h>

#include "UiLayer.h"

enum class SdlUiKeyProfile { Controls, Search };

struct SdlUiEventContext
{
	float pointerX;
	float pointerY;
	bool shift;
	bool control;
	bool alt;
	SdlUiEventContext() : pointerX(0.0f), pointerY(0.0f), shift(false), control(false), alt(false) {}
};

// Converts platform events into the shared UI input contract. Returns false
// for events that do not represent input consumed by retained UI widgets.
// The platform facade supplies current pointer/modifier state where SDL's
// queued event does not carry it (wheel position and mouse-down modifiers).
bool translateSdlUiEvent(const SDL_Event& event, UiInputEvent& input,
	SdlUiKeyProfile keyProfile = SdlUiKeyProfile::Controls,
	const SdlUiEventContext& context = SdlUiEventContext());
