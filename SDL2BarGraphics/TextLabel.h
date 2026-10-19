#pragma once

#include <string>

#include <GL/glew.h>
#include <SDL.h>
#include <glm/glm.hpp>

#include "UiAppearance.h"

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
	void setText(std::string text, const UiFontReference& font, int fontSize, SDL_Color color, int dpi = 96);
	void setText(std::string text, const ResolvedTextStyle& style, SDL_Color color);
	void setContent(std::string text);
	glm::vec2 getTextureDimensions() const { return dimensions_; }
	GLuint getTextureId() const { return textureId_; }
	const std::string& text() const { return text_; }
	bool usingFallbackFont() const { return fontFallback_; }
	const std::string& requestedFontFamily() const { return requestedFontFamily_; }
	GLfloat getHeightProportion() const { return heightPercent_; }
	static glm::vec2 measureText(const std::string& text, const UiFontReference& font, int fontSize, int dpi = 96);
	static glm::vec2 measureText(const std::string& text, const ResolvedTextStyle& style);
	static void clearFontCache();

private:
	std::string text_;
	std::string fontFace_;
	std::string fontPath_;
	std::string requestedFontFamily_;
	std::string requestedFontStyle_;
	int fontSize_;
	int fontIndex_;
	int dpiX_;
	int dpiY_;
	bool fontFallback_;
	SDL_Color color_;
	glm::vec2 dimensions_;
	GLuint textureId_;
	GLfloat heightPercent_;
	bool hasContent_;

	void createTextLabel();
	void setResolvedText(std::string text, const std::string& identity, const std::string& path,
		int index, int fontSize, int dpiX, int dpiY, SDL_Color color, bool fallback);
};
