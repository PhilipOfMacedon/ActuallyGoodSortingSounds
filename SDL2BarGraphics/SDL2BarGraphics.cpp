#include <cstdlib>

#include <SDL.h>

#include "AppController.h"
#include "AudioPresentationBridge.h"
#include "ArrayRenderer.h"
#include "ControlPanel.h"
#include "FrameCoordinator.h"
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
		if (key.repeat && key.keysym.sym != SDLK_UP && key.keysym.sym != SDLK_DOWN &&
			key.keysym.sym != SDLK_LEFTBRACKET && key.keysym.sym != SDLK_RIGHTBRACKET)
			return;

		AppAction action;
		switch (key.keysym.sym)
		{
		case SDLK_ESCAPE: window.setShouldClose(true); return;
		case SDLK_r: action.kind = AppActionKind::Shuffle; break;
		case SDLK_a: action.kind = AppActionKind::InstaSort; break;
		case SDLK_i: action.kind = AppActionKind::InstaInvert; break;
		case SDLK_1: action.kind = AppActionKind::Run; break;
		case SDLK_SPACE: action.kind = AppActionKind::TogglePlayback; break;
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

	void handleEvents(Window& window, ControlPanel& controls, AppController& app, SdlAudioOutput& audioOutput)
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
			AppAction action;
			if (controls.handleEvent(event, action))
			{
				if (action.kind != AppActionKind::None)
				{
					app.dispatch(action);
					if (action.kind == AppActionKind::RetryAudioDevice)
						audioOutput.retry();
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
	LatestAudioStateMailbox audioMailbox;
	AudioPresentationBridge audioBridge;
	SdlAudioOutput audioOutput(audioMailbox);
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

		app.update(deltaSeconds);
		handleEvents(mainWindow, controlPanel, app, audioOutput);
		audioOutput.serviceDiagnosticCapture();

		const sortinglogic::RenderSnapshot snapshot = app.snapshot();
		audioBridge.update(app.simulation(), snapshot, app.effectiveAudio(), audioMailbox);
		frameCoordinator.beginFrame(paletteFor(app.preferences().visual.palette).background);
		arrayRenderer.render(snapshot, app.preferences(), controlPanel.chartTop());
		controlPanel.render(snapshot, app.persistenceStatus(), audioOutput.status());
		frameCoordinator.finishFrame();
	}
	TextLabel::clearFontCache();
	return EXIT_SUCCESS;
}
