#include <cstdlib>
#include <string>

#include <SDL.h>

#include "AppController.h"
#include "AudioPresentationBridge.h"
#include "ArrayRenderer.h"
#include "ControlPanel.h"
#include "FrameCoordinator.h"
#include "PlaybackFrameCoordinator.h"
#include "Renderer2D.h"
#include "SdlAudioOutput.h"
#include "TextLabel.h"
#include "ThemePalette.h"
#include "UserSettings.h"
#include "Window.h"

namespace
{
	void handleKeyEvent(const SDL_KeyboardEvent& key, Window& window, AppController& app)
	{
		if ((key.keysym.sym == SDLK_RETURN || key.keysym.sym == SDLK_KP_ENTER) && (key.keysym.mod & KMOD_ALT) != 0)
		{
			window.toggleBorderlessFullscreen();
			return;
		}
		if (key.repeat && key.keysym.sym != SDLK_UP && key.keysym.sym != SDLK_DOWN &&
			key.keysym.sym != SDLK_LEFTBRACKET && key.keysym.sym != SDLK_RIGHTBRACKET)
			return;

		AppAction action;
		switch (key.keysym.sym)
		{
		case SDLK_ESCAPE:
			if (window.isBorderlessFullscreen()) window.toggleBorderlessFullscreen();
			else window.setShouldClose(true);
			return;
		case SDLK_r: action.kind = AppActionKind::Shuffle; break;
		case SDLK_a: action.kind = AppActionKind::InstaSort; break;
		case SDLK_i: action.kind = AppActionKind::InstaInvert; break;
		case SDLK_1: action.kind = AppActionKind::BuildTrace; break;
		case SDLK_SPACE: action.kind = AppActionKind::GenerateAndPlay; break;
		case SDLK_HOME: action.kind = AppActionKind::Reset; break;
		case SDLK_LEFT: action.kind = AppActionKind::PreviousEvent; break;
		case SDLK_RIGHT: action.kind = AppActionKind::NextEvent; break;
		case SDLK_d:
			action.kind = AppActionKind::SetDirection;
			action.selection = app.simulation().direction() == sortinglogic::SortDirection::Ascending ? 1 : 0;
			break;
		case SDLK_UP: action.kind = AppActionKind::IncreaseDuration; break;
		case SDLK_DOWN: action.kind = AppActionKind::DecreaseDuration; break;
		case SDLK_LEFTBRACKET: action.kind = AppActionKind::DecreaseCountCoarse; break;
		case SDLK_RIGHTBRACKET: action.kind = AppActionKind::IncreaseCountCoarse; break;
		default: return;
		}
		app.dispatch(action);
	}

	void handleEvents(Window& window, ControlPanel& controls, AppController& app, SdlAudioOutput& audioOutput,
		AudioPresentationBridge& audioBridge, LatestAudioStateMailbox& audioMailbox)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_QUIT ||
				(event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
			{
				window.setShouldClose(true);
				continue;
			}
			if (event.type == SDL_WINDOWEVENT &&
				(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || event.window.event == SDL_WINDOWEVENT_RESIZED ||
				 event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED))
				window.refreshMetrics();
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F9 && !event.key.repeat)
			{
				audioOutput.armDiagnosticCapture();
				continue;
			}
			if (event.type == SDL_KEYDOWN && !controls.textEntryActive())
			{
				const SDL_KeyboardEvent& key = event.key;
				const bool fullscreenShortcut = (key.keysym.sym == SDLK_RETURN || key.keysym.sym == SDLK_KP_ENTER) &&
					(key.keysym.mod & KMOD_ALT) != 0;
				const SDL_Keymod spaceModifiers = static_cast<SDL_Keymod>(KMOD_SHIFT | KMOD_CTRL | KMOD_ALT | KMOD_GUI);
				const bool playShortcut = key.keysym.sym == SDLK_SPACE &&
					(key.keysym.mod & spaceModifiers) == 0 && !controls.modalUiOpen();
				if (fullscreenShortcut || playShortcut)
				{
					handleKeyEvent(key, window, app);
					continue;
				}
			}
			AppAction action;
			if (controls.handleEvent(event, action))
			{
				if (action.kind != AppActionKind::None)
				{
					if (action.kind == AppActionKind::ToggleBorderlessFullscreen)
					{
						window.toggleBorderlessFullscreen();
						continue;
					}
					const sortinglogic::CommandResult result = app.dispatch(action);
					controls.completeActionResult(action, result);
					if (result.accepted && (action.kind == AppActionKind::SetUiPreset || action.kind == AppActionKind::ApplyAppearance))
						window.setClassicWindows95Caption(app.preferences().appearance.activePreset == UiPresetId::ClassicWindows95);
					if (action.kind == AppActionKind::RetryAudioDevice)
					{
						if (audioOutput.retry()) audioBridge.republish(audioMailbox);
					}
				}
				continue;
			}
			if (event.type == SDL_KEYDOWN)
				handleKeyEvent(event.key, window, app);
		}
	}
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	Window mainWindow(1000, 640, "Actually Good Sorting Sounds");
	if (mainWindow.initialize() != 0)
		return EXIT_FAILURE;

	SettingsStore settingsStore;
	AppController app(settingsStore);
	mainWindow.setClassicWindows95Caption(app.preferences().appearance.activePreset == UiPresetId::ClassicWindows95);
	LatestAudioStateMailbox audioMailbox;
	AudioPresentationBridge audioBridge;
	SdlAudioOutput audioOutput(audioMailbox);
	PlaybackFrameCoordinator playbackFrameCoordinator(audioMailbox, audioBridge, audioOutput);
	FrameCoordinator frameCoordinator(mainWindow);
	ArrayRenderer arrayRenderer(mainWindow, frameCoordinator.renderer());
	ControlPanel controlPanel(mainWindow, frameCoordinator.renderer(), app.preferences());
	if (!arrayRenderer.isReady() || !controlPanel.isReady())
		return EXIT_FAILURE;

	Uint64 previousCounter = SDL_GetPerformanceCounter();
	const double performanceFrequency = static_cast<double>(SDL_GetPerformanceFrequency());
	while (!mainWindow.getShouldClose())
	{
		const Uint64 currentCounter = SDL_GetPerformanceCounter();
		const float deltaSeconds = performanceFrequency > 0.0
			? static_cast<float>(static_cast<double>(currentCounter - previousCounter) / performanceFrequency)
			: 0.0f;
		previousCounter = currentCounter;

		app.update(deltaSeconds, playbackFrameCoordinator.audioClockAvailable());
		handleEvents(mainWindow, controlPanel, app, audioOutput, audioBridge, audioMailbox);
		audioOutput.serviceDiagnosticCapture();

		sortinglogic::RenderSnapshot snapshot = app.snapshot();
		const std::string audioStatus = playbackFrameCoordinator.updateAudioPresentation(app, snapshot);
		snapshot = app.snapshot();
		const UiThemeCustomization& activeTheme = app.preferences().appearance.themes.find(app.preferences().appearance.activePreset)->second;
		const ThemePalette palette = paletteForAppearance(app.preferences().appearance.activePreset, activeTheme);
		controlPanel.prepareFrame(snapshot, app.persistenceStatus(), app.playbackPending());
		frameCoordinator.beginFrame(palette.background);
		arrayRenderer.render(snapshot, app.preferences(), controlPanel.chartViewport(), palette);
		controlPanel.render(snapshot, audioStatus);
		frameCoordinator.finishFrame();
	}
	TextLabel::clearFontCache();
	return EXIT_SUCCESS;
}
