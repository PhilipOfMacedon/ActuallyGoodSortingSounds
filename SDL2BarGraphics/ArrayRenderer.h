#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Window.h"
#include "Rectangle.h"
#include "Shader.h"
#include "TextLabel.h"

class ArrayRenderer
{
public:
	ArrayRenderer(
		Window& window,
		std::vector<GLfloat>& array,
		std::vector<int>& highlights,
		std::mutex& stateMutex,
		GLfloat projWidth,
		GLfloat projHeight,
		GLfloat heightMult);
	~ArrayRenderer() = default;

	ArrayRenderer(const ArrayRenderer&) = delete;
	ArrayRenderer& operator=(const ArrayRenderer&) = delete;

	bool isReady() const { return shader && shader->getShaderID() != 0; }
	void render();

private:
	bool isHighlighted(int pos, const std::vector<int>& highlights) const;
	void drawBars(
		const std::vector<GLfloat>& array,
		const std::vector<int>& highlights);
	void drawTitleLabel();

	Window& window;
	std::vector<GLfloat>& array;
	std::vector<int>& highlights;
	std::mutex& stateMutex;
	std::unique_ptr<Rectangle> bar;
	std::unique_ptr<Rectangle> highlightedBar;
	std::unique_ptr<TextLabel> titleLabel;
	std::unique_ptr<Shader> shader;
	glm::mat4 projection;
	GLfloat projWidth;
	GLfloat projHeight;
	GLfloat heightMult;
};
