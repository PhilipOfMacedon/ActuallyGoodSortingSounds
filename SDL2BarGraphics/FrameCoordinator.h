#pragma once

#include <glm/glm.hpp>

#include "Renderer2D.h"
#include "Window.h"

// Owns the shared 2D queue and brackets one on-screen painter-ordered frame.
class FrameCoordinator
{
public:
	explicit FrameCoordinator(Window& window);
	Renderer2D& renderer() { return renderer_; }
	const Renderer2D& renderer() const { return renderer_; }
	void beginFrame(const glm::vec4& clearColor);
	void finishFrame();

private:
	Window& window_;
	Renderer2D renderer_;
};
