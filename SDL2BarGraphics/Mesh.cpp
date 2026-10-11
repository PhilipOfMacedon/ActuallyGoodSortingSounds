#include "Mesh.h"

Mesh::Mesh()
	: VAO(0),
	VBO(0),
	IBO(0),
	CBO(0),
	texture(nullptr),
	vertexCount(0),
	indexCount(0),
	axisCount(3),
	colorChannelCount(4)
{
}

Mesh::~Mesh()
{
	clearMesh();
}

void Mesh::createMesh(
	GLfloat* vertices,
	GLfloat* colors,
	GLuint* indices,
	GLsizei numOfVertices,
	GLsizei numOfIndices)
{
	vertexCount = numOfVertices;
	indexCount = numOfIndices;

	createVAO(vertices, numOfVertices);
	createCBO(colors, numOfVertices);
	createIBO(indices, numOfIndices);
}

void Mesh::assignTexture()
{
	texture.reset(new Texture(VAO));
}

void Mesh::assignTexture(SDL_Surface* surface)
{
	texture.reset(new Texture(surface, VAO));
}

void Mesh::createVAO(GLfloat* vertices, GLsizei numOfVertices)
{
	const GLsizei elementCount = numOfVertices * axisCount;

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(
		GL_ARRAY_BUFFER,
		sizeof(GLfloat) * elementCount,
		vertices,
		GL_STATIC_DRAW);
	glVertexAttribPointer(0, axisCount, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(0);
}

void Mesh::createCBO(GLfloat* colors, GLsizei numOfVertices)
{
	const GLsizei elementCount = numOfVertices * colorChannelCount;
	glGenBuffers(1, &CBO);
	glBindBuffer(GL_ARRAY_BUFFER, CBO);
	glBufferData(
		GL_ARRAY_BUFFER,
		sizeof(GLfloat) * elementCount,
		colors,
		GL_STATIC_DRAW);
	glVertexAttribPointer(1, colorChannelCount, GL_FLOAT, GL_FALSE, 0, nullptr);
	glEnableVertexAttribArray(1);
}

void Mesh::createIBO(GLuint* indices, GLsizei numOfIndices)
{
	glGenBuffers(1, &IBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
	glBufferData(
		GL_ELEMENT_ARRAY_BUFFER,
		sizeof(GLuint) * numOfIndices,
		indices,
		GL_STATIC_DRAW);
}

void Mesh::renderMesh(glm::vec3 position, glm::vec3 scale, Shader* shader)
{
	if (!VAO || !IBO || !CBO || !vertexCount || !indexCount || !texture || !shader)
		return;

	glm::mat4 model(1.0f);
	model = glm::translate(model, position);
	model = glm::scale(model, scale);
	glUniformMatrix4fv(shader->getModelLocation(), 1, GL_FALSE, glm::value_ptr(model));

	bindBuffers();
	texture->bind();
	glUniform1i(shader->getTextureLocation(), 0);
	glUniform1i(
		shader->getUseTextureFlagLocation(),
		texture->isTextured() ? GL_TRUE : GL_FALSE);
	glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
	texture->unbind();
	unbindBuffers();
}

void Mesh::clearMesh()
{
	texture.reset();

	if (VAO)
	{
		glDeleteVertexArrays(1, &VAO);
		VAO = 0;
	}
	if (VBO)
	{
		glDeleteBuffers(1, &VBO);
		VBO = 0;
	}
	if (IBO)
	{
		glDeleteBuffers(1, &IBO);
		IBO = 0;
	}
	if (CBO)
	{
		glDeleteBuffers(1, &CBO);
		CBO = 0;
	}

	vertexCount = 0;
	indexCount = 0;
}

void Mesh::bindBuffers()
{
	glBindVertexArray(VAO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
}

void Mesh::unbindBuffers()
{
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
