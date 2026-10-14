#include "FrameCoordinator.h"

FrameCoordinator::FrameCoordinator(Window& window)
	: window_(window), renderer_(window)
{
}

void FrameCoordinator::beginFrame(const glm::vec4& clearColor)
{
	window_.setClearColor(clearColor);
	window_.clearWindow();
	renderer_.beginFrame();
}

void FrameCoordinator::finishFrame()
{
	renderer_.flush();
	window_.swapBuffers();
}
