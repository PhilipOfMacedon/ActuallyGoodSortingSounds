#pragma once

#include <string>

#include <GL/glew.h>
#include <SDL.h>
#include <glm/glm.hpp>

// A cached rasterized label. Geometry and drawing are owned by Renderer2D;
// this class owns only the text texture and regenerates it when content changes.
class TextLabel
{
public:
	TextLabel(std::string text);
	TextLabel(std::string text, SDL_Color color);
	TextLabel(std::string text, std::string fontFace, int fontSize);
	TextLabel(std::string text, std::string fontFace, int fontSize, SDL_Color color);
	~TextLabel();

	TextLabel(const TextLabel&) = delete;
	TextLabel& operator=(const TextLabel&) = delete;

	void setText(std::string text);
	void setText(std::string text, SDL_Color color);
	void setText(std::string text, std::string fontFace, int fontSize);
	void setText(std::string text, std::string fontFace, int fontSize, SDL_Color color);
	glm::vec2 getTextureDimensions() const { return dimensions_; }
	GLuint getTextureId() const { return textureId_; }
	GLfloat getHeightProportion() const { return heightPercent_; }
	static void clearFontCache();

private:
	std::string text_;
	std::string fontFace_;
	int fontSize_;
	SDL_Color color_;
	glm::vec2 dimensions_;
	GLuint textureId_;
	GLfloat heightPercent_;
	bool hasContent_;

	void createTextLabel();
};
