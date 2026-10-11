#include "Texture.h"

#include <iostream>

Texture::Texture(GLuint VAO)
	: ID(0),
	uvVBO(0),
	VAO(VAO),
	data(nullptr),
	dimensions(0.0f, 0.0f),
	textured(false)
{
	createUVVBO();
}

Texture::Texture(SDL_Surface* surface, GLuint VAO)
	: ID(0),
	uvVBO(0),
	VAO(VAO),
	data(surface),
	dimensions(surface ? glm::vec2(static_cast<float>(surface->w), static_cast<float>(surface->h)) : glm::vec2(0.0f)),
	textured(false)
{
	if (data && data->format && data->format->format != SDL_PIXELFORMAT_RGBA32)
	{
		SDL_Surface* rgbaSurface = SDL_ConvertSurfaceFormat(data, SDL_PIXELFORMAT_RGBA32, 0);
		if (rgbaSurface)
		{
			SDL_FreeSurface(data);
			data = rgbaSurface;
			dimensions = glm::vec2(
				static_cast<float>(data->w),
				static_cast<float>(data->h));
		}
		else
		{
			std::cerr << "Could not convert texture surface: " << SDL_GetError() << '\n';
		}
	}
	createUVVBO();
	createTexture();
}

Texture::~Texture()
{
	if (ID)
		glDeleteTextures(1, &ID);
	if (uvVBO)
		glDeleteBuffers(1, &uvVBO);
	if (data)
		SDL_FreeSurface(data);

	ID = 0;
	uvVBO = 0;
	data = nullptr;
}

void Texture::createTexture()
{
	if (!data || !data->format ||
		data->format->format != SDL_PIXELFORMAT_RGBA32 ||
		data->format->BytesPerPixel != 4)
	{
		std::cerr << "Texture source must be a valid RGBA32 SDL surface.\n";
		return;
	}

	glGenTextures(1, &ID);
	if (!ID)
		return;

	glBindTexture(GL_TEXTURE_2D, ID);
	const bool mustLock = SDL_MUSTLOCK(data) != 0;
	if (mustLock && SDL_LockSurface(data) != 0)
	{
		std::cerr << "Could not lock texture surface: " << SDL_GetError() << '\n';
		glBindTexture(GL_TEXTURE_2D, 0);
		glDeleteTextures(1, &ID);
		ID = 0;
		return;
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, data->pitch / data->format->BytesPerPixel);
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA,
		data->w,
		data->h,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		data->pixels);
	if (mustLock)
		SDL_UnlockSurface(data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	textured = true;
}

void Texture::createUVVBO()
{
	if (!VAO)
		return;

	const GLfloat uvCoords[] =
	{
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f
	};

	glBindVertexArray(VAO);
	glGenBuffers(1, &uvVBO);
	glBindBuffer(GL_ARRAY_BUFFER, uvVBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(uvCoords), uvCoords, GL_STATIC_DRAW);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
	glEnableVertexAttribArray(2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Texture::bind()
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, ID);
}

void Texture::unbind()
{
	glBindTexture(GL_TEXTURE_2D, 0);
}
