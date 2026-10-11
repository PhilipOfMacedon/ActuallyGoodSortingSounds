#include "TextLabel.h"

#include <SDL_ttf.h>

#include <iostream>

TextLabel::TextLabel(std::string text)
	: TextLabel(text, "Courier New Bold", 16, { 255, 255, 255, 255 })
{
}

TextLabel::TextLabel(std::string text, SDL_Color color)
	: TextLabel(text, "Courier New Bold", 16, color)
{
}

TextLabel::TextLabel(std::string text, std::string fontFace, int fontSize)
	: TextLabel(text, fontFace, fontSize, { 255, 255, 255, 255 })
{
}

TextLabel::TextLabel(
	std::string text,
	std::string fontFace,
	int fontSize,
	SDL_Color color)
	: Mesh(),
	heightPercent(0.0275f)
{
	createBoundaries();
	setText(text, fontFace, fontSize, color);
}

void TextLabel::setText(std::string text)
{
	setText(text, "Courier New Bold", 16, { 255, 255, 255, 255 });
}

void TextLabel::setText(std::string text, SDL_Color color)
{
	setText(text, "Courier New Bold", 16, color);
}

void TextLabel::setText(std::string text, std::string fontFace, int fontSize)
{
	setText(text, fontFace, fontSize, { 255, 255, 255, 255 });
}

void TextLabel::setText(
	std::string text,
	std::string fontFace,
	int fontSize,
	SDL_Color fontColor)
{
	createTextLabel(text, fontFace, fontSize, fontColor);
}

glm::vec2 TextLabel::getTextureDimensions() const
{
	return texture && texture->isTextured()
		? texture->getDimensions()
		: glm::vec2(0.0f);
}

void TextLabel::createTextLabel(
	std::string text,
	std::string fontFace,
	int fontSize,
	SDL_Color fontColor)
{
	texture.reset();
	const std::string fontPath = "Resources/" + fontFace + ".ttf";
	TTF_Font* font = TTF_OpenFont(fontPath.c_str(), fontSize);
	if (!font)
	{
		std::cerr << "Could not open font '" << fontPath << "': " << TTF_GetError() << '\n';
		return;
	}

	SDL_Surface* textSurface = TTF_RenderText_Blended(font, text.c_str(), fontColor);
	TTF_CloseFont(font);
	if (!textSurface)
	{
		std::cerr << "Could not render label text: " << TTF_GetError() << '\n';
		return;
	}

	SDL_Surface* rgbaSurface = SDL_ConvertSurfaceFormat(textSurface, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(textSurface);
	if (!rgbaSurface)
	{
		std::cerr << "Could not convert label surface: " << SDL_GetError() << '\n';
		return;
	}

	Mesh::assignTexture(rgbaSurface);
}

void TextLabel::createBoundaries()
{
	GLfloat vertices[] =
	{
		0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f,
		1.0f, -1.0f, 0.0f,
		1.0f, 0.0f, 0.0f
	};

	GLfloat colors[] =
	{
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f,
		1.0f, 1.0f, 1.0f, 1.0f
	};

	GLuint indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	Mesh::createMesh(vertices, colors, indices, 4, 6);
}
