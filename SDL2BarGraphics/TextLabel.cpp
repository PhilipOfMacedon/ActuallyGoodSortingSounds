#include "TextLabel.h"

#include <SDL_ttf.h>

#include <iostream>
#include <map>

namespace
{
	std::map<std::string, TTF_Font*>& fontCache()
	{
		static std::map<std::string, TTF_Font*> cache;
		return cache;
	}

	TTF_Font* findFont(const std::string& fontFace, int size)
	{
		const std::string key = fontFace + "#" + std::to_string(size);
		std::map<std::string, TTF_Font*>& cache = fontCache();
		std::map<std::string, TTF_Font*>::iterator found = cache.find(key);
		if (found != cache.end())
			return found->second;
		const std::string path = "Resources/" + fontFace + ".ttf";
		TTF_Font* font = TTF_OpenFont(path.c_str(), size);
		if (!font)
		{
			std::cerr << "Could not open font '" << path << "': " << TTF_GetError() << '\n';
			return nullptr;
		}
		cache[key] = font;
		return font;
	}

	bool sameColor(const SDL_Color& left, const SDL_Color& right)
	{
		return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
	}
}

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

TextLabel::TextLabel(std::string text, std::string fontFace, int fontSize, SDL_Color color)
	: text_(),
	fontFace_(),
	fontSize_(0),
	color_({ 0, 0, 0, 0 }),
	dimensions_(0.0f),
	textureId_(0),
	heightPercent_(0.0275f),
	hasContent_(false)
{
	setText(text, fontFace, fontSize, color);
}

TextLabel::~TextLabel()
{
	if (textureId_)
		glDeleteTextures(1, &textureId_);
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

void TextLabel::setText(std::string text, std::string fontFace, int fontSize, SDL_Color color)
{
	if (hasContent_ && text_ == text && fontFace_ == fontFace && fontSize_ == fontSize && sameColor(color_, color))
		return;
	text_ = text;
	fontFace_ = fontFace;
	fontSize_ = fontSize;
	color_ = color;
	hasContent_ = true;
	createTextLabel();
}

void TextLabel::createTextLabel()
{
	if (textureId_)
	{
		glDeleteTextures(1, &textureId_);
		textureId_ = 0;
	}
	dimensions_ = glm::vec2(0.0f);
	TTF_Font* font = findFont(fontFace_, fontSize_);
	if (!font)
		return;
	SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text_.empty() ? " " : text_.c_str(), color_);
	if (!rendered)
	{
		std::cerr << "Could not render label text: " << TTF_GetError() << '\n';
		return;
	}
	SDL_Surface* rgba = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(rendered);
	if (!rgba)
	{
		std::cerr << "Could not convert label surface: " << SDL_GetError() << '\n';
		return;
	}
	glGenTextures(1, &textureId_);
	if (!textureId_)
	{
		SDL_FreeSurface(rgba);
		return;
	}
	glBindTexture(GL_TEXTURE_2D, textureId_);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / rgba->format->BytesPerPixel);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	dimensions_ = glm::vec2(static_cast<float>(rgba->w), static_cast<float>(rgba->h));
	SDL_FreeSurface(rgba);
}

void TextLabel::clearFontCache()
{
	std::map<std::string, TTF_Font*>& cache = fontCache();
	for (std::map<std::string, TTF_Font*>::iterator font = cache.begin(); font != cache.end(); ++font)
		TTF_CloseFont(font->second);
	cache.clear();
}
