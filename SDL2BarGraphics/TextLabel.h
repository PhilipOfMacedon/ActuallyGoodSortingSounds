#pragma once

#include <cstdio>
#include <string>
#include <SDL.h>

#include "Mesh.h"

class TextLabel : public Mesh
{
public:
	TextLabel(std::string text);
	TextLabel(std::string text, SDL_Color color);
	TextLabel(std::string text, std::string fontFace, int fontSize);
	TextLabel(std::string text, std::string fontFace, int fontSize, SDL_Color color);
	~TextLabel() override = default;

	void setText(std::string text);
	void setText(std::string text, SDL_Color color);
	void setText(std::string text, std::string fontFace, int fontSize);
	void setText(std::string text, std::string fontFace, int fontSize, SDL_Color color);
	glm::vec2 getTextureDimensions() const;
	GLfloat getHeightProportion() { return heightPercent; }

private:
	GLfloat heightPercent;

	void createTextLabel(std::string text, std::string fontFace, int fontSize, SDL_Color fontColor);
	void createBoundaries();
};

