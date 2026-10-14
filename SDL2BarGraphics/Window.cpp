#include "Window.h"

#include <cstdlib>
#include <iostream>

Window::Window()
	: Window(800, 600, "Untitled")
{
}

Window::Window(GLint width, GLint height, const char* title)
	: title(title ? title : "Untitled"),
	clearColor(0.1f, 0.1f, 0.1f, 1.0f),
	mainWindow(nullptr),
	mainContext(nullptr),
	width(width),
	height(height),
	bufferWidth(0),
	bufferHeight(0),
	shouldClose(true),
	sdlInitialized(false),
	ttfInitialized(false)
{
}

Window::~Window()
{
	shutdown();
}

void Window::setTitle(const char* newTitle)
{
	title = newTitle ? newTitle : "Untitled";
	if (mainWindow)
		SDL_SetWindowTitle(mainWindow, title.c_str());
}

void Window::swapBuffers() const
{
	if (mainWindow && mainContext)
		SDL_GL_SwapWindow(mainWindow);
}

void Window::clearWindow()
{
	if (!mainWindow || !mainContext)
		return;
	refreshMetrics();

	glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Window::refreshMetrics()
{
	if (!mainWindow)
		return;
	int logicalWidth = 0;
	int logicalHeight = 0;
	int drawableWidth = 0;
	int drawableHeight = 0;
	SDL_GetWindowSize(mainWindow, &logicalWidth, &logicalHeight);
	SDL_GL_GetDrawableSize(mainWindow, &drawableWidth, &drawableHeight);
	if (logicalWidth > 0) width = logicalWidth;
	if (logicalHeight > 0) height = logicalHeight;
	if (drawableWidth > 0 && drawableHeight > 0 &&
		(drawableWidth != bufferWidth || drawableHeight != bufferHeight))
	{
		bufferWidth = drawableWidth;
		bufferHeight = drawableHeight;
		glViewport(0, 0, bufferWidth, bufferHeight);
	}
}

int Window::initialize()
{
	if (mainWindow && mainContext)
		return 0;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		std::cerr << "Could not initialize SDL: " << SDL_GetError() << '\n';
		return 1;
	}
	sdlInitialized = true;

	if (TTF_Init() != 0)
	{
		std::cerr << "Could not initialize SDL_ttf: " << TTF_GetError() << '\n';
		shutdown();
		return 1;
	}
	ttfInitialized = true;

	if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0 ||
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0 ||
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 0 ||
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0)
	{
		std::cerr << "Could not configure the OpenGL context: " << SDL_GetError() << '\n';
		shutdown();
		return 1;
	}

	mainWindow = SDL_CreateWindow(
		title.c_str(),
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		width,
		height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!mainWindow)
	{
		std::cerr << "Could not create the SDL window: " << SDL_GetError() << '\n';
		shutdown();
		return 1;
	}

	mainContext = SDL_GL_CreateContext(mainWindow);
	if (!mainContext)
	{
		std::cerr << "Could not create the OpenGL context: " << SDL_GetError() << '\n';
		shutdown();
		return 1;
	}

	glewExperimental = GL_TRUE;
	const GLenum glewStatus = glewInit();
	if (glewStatus != GLEW_OK)
	{
		std::cerr << "Could not initialize GLEW: "
			<< reinterpret_cast<const char*>(glewGetErrorString(glewStatus)) << '\n';
		shutdown();
		return 1;
	}

	SDL_GL_SetSwapInterval(1);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	SDL_GL_GetDrawableSize(mainWindow, &bufferWidth, &bufferHeight);
	SDL_GetWindowSize(mainWindow, &width, &height);
	glViewport(0, 0, bufferWidth, bufferHeight);
	shouldClose = false;
	return 0;
}

void Window::shutdown()
{
	shouldClose = true;
	if (mainContext)
	{
		SDL_GL_DeleteContext(mainContext);
		mainContext = nullptr;
	}
	if (mainWindow)
	{
		SDL_DestroyWindow(mainWindow);
		mainWindow = nullptr;
	}
	bufferWidth = 0;
	bufferHeight = 0;
	if (ttfInitialized)
	{
		TTF_Quit();
		ttfInitialized = false;
	}
	if (sdlInitialized)
	{
		SDL_Quit();
		sdlInitialized = false;
	}
}
