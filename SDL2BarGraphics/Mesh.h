#pragma once

#include <cstdio>
#include <memory>
#include <SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Texture.h"
#include "Shader.h"

class Mesh
{
public:
	Mesh();
	virtual ~Mesh();
	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;
	void createMesh(GLfloat* vertices, GLfloat* colors, GLuint* indices, GLsizei numOfVertices, GLsizei numOfIndices);
	void assignTexture();
	// Takes ownership of surface and frees it when the texture is destroyed.
	void assignTexture(SDL_Surface* surface);
	void renderMesh(glm::vec3 position, glm::vec3 scale, Shader* shader);
	void clearMesh();
	void createVAO(GLfloat* vertices, GLsizei numOfVertices);
	void createCBO(GLfloat* colors, GLsizei numOfVertices);
	void createIBO(GLuint* indices, GLsizei numOfIndices);
	void bindBuffers();
	void unbindBuffers();
protected:
	GLuint VAO, VBO, IBO, CBO;
	std::unique_ptr<Texture> texture;
	GLsizei vertexCount;
	GLsizei indexCount;
	GLsizei axisCount;
	GLsizei colorChannelCount;
};
