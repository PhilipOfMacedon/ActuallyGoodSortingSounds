#include "SortingLiveWindow.h"

void SortingLiveWindow::handleEvents()
{
	SDL_Event e;
	while (SDL_PollEvent(&e))
	{
		Window::handleEvents();
		handleKeyPresses();
	}
}

void SortingLiveWindow::handleKeyPresses()
{
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if (state[SDL_SCANCODE_UP])
	{
		worker->selectOperation(SorterOperation::IncDelay);
	}
	if (state[SDL_SCANCODE_DOWN])
	{
		worker->selectOperation(SorterOperation::DecDelay);
	}
	if (state[SDL_SCANCODE_R])
	{
		worker->selectOperation(SorterOperation::Randomize);
	}
	if (state[SDL_SCANCODE_I])
	{
		worker->selectOperation(SorterOperation::Revert);
	}
	if (state[SDL_SCANCODE_1])
	{
		worker->selectOperation(SorterOperation::InsertionSort);
	}
}