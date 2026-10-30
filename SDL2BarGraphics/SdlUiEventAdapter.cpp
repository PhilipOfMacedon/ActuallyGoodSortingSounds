#include "SdlUiEventAdapter.h"

bool translateSdlUiEvent(const SDL_Event& event, UiInputEvent& input, SdlUiKeyProfile keyProfile,
	const SdlUiEventContext& context)
{
	UiInputEvent translated(UiInputType::FocusLost);
	switch (event.type)
	{
	case SDL_MOUSEMOTION:
		translated = UiInputEvent(UiInputType::PointerMove,
			static_cast<float>(event.motion.x), static_cast<float>(event.motion.y));
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (event.button.button != SDL_BUTTON_LEFT) return false;
		translated = UiInputEvent(UiInputType::PointerDown,
			static_cast<float>(event.button.x), static_cast<float>(event.button.y));
		break;
	case SDL_MOUSEBUTTONUP:
		if (event.button.button != SDL_BUTTON_LEFT) return false;
		translated = UiInputEvent(UiInputType::PointerUp,
			static_cast<float>(event.button.x), static_cast<float>(event.button.y));
		break;
	case SDL_MOUSEWHEEL:
		translated = UiInputEvent(UiInputType::Wheel,
			context.pointerX, context.pointerY, event.wheel.y);
		break;
	case SDL_TEXTINPUT:
		translated = UiInputEvent(UiInputType::TextInput, std::string(event.text.text));
		break;
	case SDL_WINDOWEVENT:
		if (event.window.event != SDL_WINDOWEVENT_FOCUS_LOST) return false;
		translated = UiInputEvent(UiInputType::FocusLost);
		break;
	case SDL_KEYDOWN:
	{
		UiKey key = UiKey::None;
		switch (event.key.keysym.sym)
		{
		case SDLK_TAB: key = UiKey::Tab; break;
		case SDLK_RETURN: case SDLK_KP_ENTER: key = UiKey::Enter; break;
		case SDLK_SPACE:
			if (keyProfile != SdlUiKeyProfile::Controls) return false;
			key = UiKey::Space;
			break;
		case SDLK_BACKSPACE: key = UiKey::Backspace; break;
		case SDLK_ESCAPE: key = UiKey::Escape; break;
		case SDLK_UP: key = UiKey::Up; break;
		case SDLK_DOWN: key = UiKey::Down; break;
		case SDLK_LEFT: key = UiKey::Left; break;
		case SDLK_RIGHT: key = UiKey::Right; break;
		default: return false;
		}
		translated = UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, key,
			(event.key.keysym.mod & KMOD_SHIFT) != 0);
		translated.control = (event.key.keysym.mod & KMOD_CTRL) != 0;
		translated.alt = (event.key.keysym.mod & KMOD_ALT) != 0;
		break;
	}
	default:
		return false;
	}

	if (event.type == SDL_MOUSEBUTTONDOWN)
	{
		translated.shift = context.shift;
		translated.control = context.control;
		translated.alt = context.alt;
	}
	input = translated;
	return true;
}
