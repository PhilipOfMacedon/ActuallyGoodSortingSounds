#pragma once

#include <SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>

class Texture
{
public:
	Texture(GLuint VAO);
	Texture(SDL_Surface* surface, GLuint VAO);
	~Texture();

	Texture(const Texture&) = delete;
	Texture& operator=(const Texture&) = delete;

	glm::vec2 getDimensions() const { return dimensions; }
	bool isTextured() const { return textured; }
	void bind();
	void unbind();

private:
	void createTexture();
	void createUVVBO();

	GLuint ID;
	GLuint uvVBO;
	GLuint VAO;
	SDL_Surface* data;
	glm::vec2 dimensions;
	bool textured;
};
