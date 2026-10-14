#include "Renderer2D.h"

#include <algorithm>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Renderer2D::Renderer2D(Window& window)
	: window_(window),
	shader_(new Shader()),
	vao_(0),
	vbo_(0),
	bufferCapacityBytes_(0),
	logicalWidth_(0),
	logicalHeight_(0),
	drawableWidth_(0),
	drawableHeight_(0),
	projection_(1.0f),
	commands_(),
	vertices_(),
	materials_()
{
	shader_->createFromFiles("Shaders/shader.vert", "Shaders/shader.frag");
	glGenVertexArrays(1, &vao_);
	glGenBuffers(1, &vbo_);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);
	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
	glEnableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	beginFrame();
}

Renderer2D::~Renderer2D()
{
	if (vbo_) glDeleteBuffers(1, &vbo_);
	if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Renderer2D::isReady() const
{
	return shader_ && shader_->getShaderID() != 0 && vao_ != 0 && vbo_ != 0;
}

void Renderer2D::beginFrame()
{
	commands_.clear();
	logicalWidth_ = window_.getLogicalWidth();
	logicalHeight_ = window_.getLogicalHeight();
	drawableWidth_ = window_.getDrawableWidth();
	drawableHeight_ = window_.getDrawableHeight();
	if (logicalWidth_ > 0 && logicalHeight_ > 0)
		projection_ = glm::ortho(0.0f, static_cast<float>(logicalWidth_), static_cast<float>(logicalHeight_), 0.0f, -10.0f, 10.0f);
}

void Renderer2D::submit(const QuadInstance& quad)
{
	if (quad.size.x <= 0.0f || quad.size.y <= 0.0f)
		return;
	commands_.push_back(Command(quad, commands_.size()));
}

void Renderer2D::flush()
{
	if (!isReady() || logicalWidth_ <= 0 || logicalHeight_ <= 0 || drawableWidth_ <= 0 || drawableHeight_ <= 0)
	{
		commands_.clear();
		return;
	}
	std::stable_sort(commands_.begin(), commands_.end(), [](const Command& left, const Command& right)
	{
		return painterLayerBefore(left.quad.layer, right.quad.layer);
	});
	glUseProgram(shader_->getShaderID());
	glUniformMatrix4fv(shader_->getProjectionLocation(), 1, GL_FALSE, glm::value_ptr(projection_));
	glUniformMatrix4fv(shader_->getModelLocation(), 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glUniform1i(shader_->getTextureLocation(), 0);
	glBindVertexArray(vao_);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_);

	std::size_t begin = 0;
	while (begin < commands_.size())
	{
		std::size_t end = begin + 1;
		while (end < commands_.size() && sameBatch(commands_[begin].quad, commands_[end].quad))
			++end;
		drawBatch(begin, end);
		begin = end;
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glUseProgram(0);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	commands_.clear();
}

bool Renderer2D::sameBatch(const QuadInstance& left, const QuadInstance& right) const
{
	return compatibleBatch(
		RenderBatchKey(left.layer, left.render.materialId, left.render.textureId,
			left.clip.enabled, left.clip.x, left.clip.y, left.clip.width, left.clip.height),
		RenderBatchKey(right.layer, right.render.materialId, right.render.textureId,
			right.clip.enabled, right.clip.x, right.clip.y, right.clip.width, right.clip.height));
}

void Renderer2D::drawBatch(std::size_t begin, std::size_t end)
{
	vertices_.clear();
	vertices_.reserve((end - begin) * 6);
	for (std::size_t i = begin; i < end; ++i)
		appendQuad(commands_[i].quad, vertices_);
	const std::size_t required = vertices_.size() * sizeof(Vertex);
	if (required > bufferCapacityBytes_)
	{
		bufferCapacityBytes_ = std::max(required, std::max<std::size_t>(4096, bufferCapacityBytes_ * 2));
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bufferCapacityBytes_), nullptr, GL_DYNAMIC_DRAW);
	}
	glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(required), vertices_.data());
	const QuadInstance& first = commands_[begin].quad;
	const MaterialState material = materials_.stateFor(first.render.materialId);
	if (material.blendMode == BlendMode::Alpha)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);
	const GLuint textureId = material.textureBinding == TextureBinding::CommandTexture
		? static_cast<GLuint>(first.render.textureId)
		: 0;
	glUniform1i(shader_->getUseTextureFlagLocation(), textureId ? GL_TRUE : GL_FALSE);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textureId);
	if (first.clip.enabled)
	{
		const float sx = drawableWidth_ / static_cast<float>(logicalWidth_);
		const float sy = drawableHeight_ / static_cast<float>(logicalHeight_);
		const GLint x = static_cast<GLint>(first.clip.x * sx);
		const GLint y = drawableHeight_ - static_cast<GLint>((first.clip.y + first.clip.height) * sy);
		const GLsizei w = static_cast<GLsizei>(first.clip.width * sx);
		const GLsizei h = static_cast<GLsizei>(first.clip.height * sy);
		glEnable(GL_SCISSOR_TEST);
		glScissor(x, y, std::max<GLsizei>(0, w), std::max<GLsizei>(0, h));
	}
	else
		glDisable(GL_SCISSOR_TEST);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
}

void Renderer2D::appendQuad(const QuadInstance& quad, std::vector<Vertex>& output) const
{
	const glm::vec4& tint = quad.render.tint;
	const Vertex corners[6] =
	{
		{ quad.position.x, quad.position.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 0.0f, 0.0f },
		{ quad.position.x, quad.position.y + quad.size.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 0.0f, 1.0f },
		{ quad.position.x + quad.size.x, quad.position.y + quad.size.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 1.0f, 1.0f },
		{ quad.position.x, quad.position.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 0.0f, 0.0f },
		{ quad.position.x + quad.size.x, quad.position.y + quad.size.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 1.0f, 1.0f },
		{ quad.position.x + quad.size.x, quad.position.y, 0.0f, tint.r, tint.g, tint.b, tint.a, 1.0f, 0.0f }
	};
	output.insert(output.end(), corners, corners + 6);
}
