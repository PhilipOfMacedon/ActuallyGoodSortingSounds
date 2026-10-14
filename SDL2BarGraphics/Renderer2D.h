#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include <GL/glew.h>
#include <glm/glm.hpp>

#include "RenderingTypes.h"
#include "Shader.h"
#include "Window.h"

class Renderer2D
{
public:
	explicit Renderer2D(Window& window);
	~Renderer2D();

	Renderer2D(const Renderer2D&) = delete;
	Renderer2D& operator=(const Renderer2D&) = delete;

	bool isReady() const;
	void beginFrame();
	void submit(const QuadInstance& quad);
	void registerMaterial(MaterialId id, const MaterialState& state) { materials_.registerMaterial(id, state); }
	void flush();
	int logicalWidth() const { return logicalWidth_; }
	int logicalHeight() const { return logicalHeight_; }
	int drawableWidth() const { return drawableWidth_; }
	int drawableHeight() const { return drawableHeight_; }

private:
	struct Vertex
	{
		GLfloat x, y, z;
		GLfloat r, g, b, a;
		GLfloat u, v;
	};
	struct Command
	{
		QuadInstance quad;
		std::size_t sequence;
		explicit Command(const QuadInstance& value, std::size_t order) : quad(value), sequence(order) {}
	};

	Window& window_;
	std::unique_ptr<Shader> shader_;
	GLuint vao_;
	GLuint vbo_;
	std::size_t bufferCapacityBytes_;
	int logicalWidth_;
	int logicalHeight_;
	int drawableWidth_;
	int drawableHeight_;
	glm::mat4 projection_;
	std::vector<Command> commands_;
	std::vector<Vertex> vertices_;
	MaterialTable materials_;

	void drawBatch(std::size_t begin, std::size_t end);
	void appendQuad(const QuadInstance& quad, std::vector<Vertex>& output) const;
	bool sameBatch(const QuadInstance& left, const QuadInstance& right) const;
};
