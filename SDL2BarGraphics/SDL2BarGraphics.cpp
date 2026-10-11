#include <cstdlib>
#include <mutex>
#include <vector>

#include <SDL.h>

#include "ArrayWorker.h"
#include "ArrayRenderer.h"
#include "Window.h"

namespace
{
	constexpr GLfloat MAX_VALUE = 1.0f;
	constexpr std::size_t ARRAY_SIZE = 80;

	std::vector<GLfloat> generateArray(GLfloat maxValue, std::size_t size)
	{
		std::vector<GLfloat> values(size);
		if (size == 0)
			return values;
		if (size == 1)
		{
			values[0] = maxValue;
			return values;
		}

		const GLfloat start = maxValue / static_cast<GLfloat>(size + 1);
		for (std::size_t i = 0; i < size; ++i)
		{
			values[i] = start + static_cast<GLfloat>(i) *
				(maxValue - start) / static_cast<GLfloat>(size - 1);
		}
		return values;
	}

	void handleEvents(Window& window, ArrayWorker& worker)
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

			if (event.type != SDL_KEYDOWN)
				continue;
			if (event.key.repeat &&
				event.key.keysym.sym != SDLK_UP &&
				event.key.keysym.sym != SDLK_DOWN)
				continue;

			switch (event.key.keysym.sym)
			{
			case SDLK_ESCAPE:
				window.setShouldClose(true);
				break;
			case SDLK_UP:
				worker.selectOperation(SorterOperation::IncDelay);
				break;
			case SDLK_DOWN:
				worker.selectOperation(SorterOperation::DecDelay);
				break;
			case SDLK_r:
				worker.selectOperation(SorterOperation::Randomize);
				break;
			case SDLK_i:
				worker.selectOperation(SorterOperation::Revert);
				break;
			case SDLK_1:
				worker.selectOperation(SorterOperation::InsertionSort);
				break;
			default:
				break;
			}
		}
	}
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	std::vector<GLfloat> array = generateArray(MAX_VALUE, ARRAY_SIZE);
	std::vector<int> highlights(2, -1);
	std::mutex stateMutex;

	Window mainWindow(800, 600, "Actually Good Sorting Sounds");
	if (mainWindow.initialize() != 0)
		return EXIT_FAILURE;

	ArrayWorker worker(array, highlights, stateMutex);
	ArrayRenderer arrayRenderer(mainWindow, array, highlights, stateMutex, 1.0f, 1.0f, 0.95f);
	if (!arrayRenderer.isReady())
		return EXIT_FAILURE;

	while (!mainWindow.getShouldClose())
	{
		handleEvents(mainWindow, worker);
		mainWindow.clearWindow();
		arrayRenderer.render();
		mainWindow.swapBuffers();
	}

	worker.stopAndJoin();
	return EXIT_SUCCESS;
}
