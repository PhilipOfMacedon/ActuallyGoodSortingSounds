#pragma once

#include <string>

#include <GL/glew.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <glm/glm.hpp>

class Window
{
public:
	Window();
	Window(GLint width, GLint height, const char* title);
	~Window();

	Window(const Window&) = delete;
	Window& operator=(const Window&) = delete;

	GLfloat getBufferWidth() const { return static_cast<GLfloat>(bufferWidth); }
	GLfloat getBufferHeight() const { return static_cast<GLfloat>(bufferHeight); }
	void delay(Uint32 millis) const { SDL_Delay(millis); }
	bool getShouldClose() const { return shouldClose; }
	void setClearColor(glm::vec4 rgba) { clearColor = rgba; }
	void setTitle(const char* newTitle);
	void swapBuffers() const;
	void setShouldClose(bool shouldClose) { this->shouldClose = shouldClose; }

	void clearWindow();
	int initialize();
	SDL_Window* getWindow() const { return mainWindow; }

private:
	void shutdown();

	std::string title;
	glm::vec4 clearColor;
	SDL_Window* mainWindow;
	SDL_GLContext mainContext;
	GLint width;
	GLint height;
	GLint bufferWidth;
	GLint bufferHeight;
	bool shouldClose;
	bool sdlInitialized;
	bool ttfInitialized;
};
