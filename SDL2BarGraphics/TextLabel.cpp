#include "TextLabel.h"
#include "AssetPaths.h"

#include <SDL_ttf.h>

#include <iostream>
#include <map>
#include <utility>

#include "WindowsFontCatalog.h"

namespace
{
	std::map<std::string, TTF_Font*>& fontCache()
	{
		static std::map<std::string, TTF_Font*> cache;
		return cache;
	}

	TTF_Font* findFont(const std::string& path, int index, int size, int dpiX, int dpiY)
	{
		const std::string key = path + "#" + std::to_string(index) + "#" + std::to_string(size) + "#" +
			std::to_string(dpiX) + "x" + std::to_string(dpiY);
		std::map<std::string, TTF_Font*>& cache = fontCache();
		std::map<std::string, TTF_Font*>::iterator found = cache.find(key);
		if (found != cache.end())
			return found->second;
		TTF_Font* font = dpiX > 0 && dpiY > 0
			? TTF_OpenFontIndexDPI(path.c_str(), size, static_cast<long>(index), dpiX, dpiY)
			: TTF_OpenFontIndex(path.c_str(), size, static_cast<long>(index));
		if (!font)
		{
			std::cerr << "Could not open font '" << path << "': " << TTF_GetError() << '\n';
			return nullptr;
		}
		cache[key] = font;
		return font;
	}

	const WindowsFontCatalog& installedFonts()
	{
		static const WindowsFontCatalog catalog;
		return catalog;
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
	fontPath_(),
	requestedFontFamily_(),
	requestedFontStyle_(),
	fontSize_(0),
	fontIndex_(0),
	dpiX_(0),
	dpiY_(0),
	fontFallback_(false),
	color_({ 0, 0, 0, 0 }),
	dimensions_(0.0f),
	textureId_(0),
	heightPercent_(0.0275f),
	hasContent_(false),
	textureDirty_(false)
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
	setResolvedText(text, fontFace, applicationAssetPath("Resources/" + fontFace + ".ttf"), 0, fontSize, 0, 0, color, false);
}

void TextLabel::setContent(std::string text)
{
	if (text_ == text)
		return;
	text_ = std::move(text);
	// The view supplies its resolved font and DPI at draw time; defer rasterization until then.
	textureDirty_ = true;
}

void TextLabel::setText(std::string text, const UiFontReference& font, int fontSize, SDL_Color color, int dpi)
{
	std::string identity = font.family + "|" + font.style;
	// Labels are submitted every frame, so reuse the resolved font when nothing changed.
	if (hasContent_ && !textureDirty_ && text_ == text && fontFace_ == identity && fontSize_ == fontSize &&
		dpiX_ == dpi && dpiY_ == dpi && requestedFontFamily_ == font.family && requestedFontStyle_ == font.style)
	{
		color_ = color;
		return;
	}
	std::string path = applicationAssetPath("Resources/Courier New Bold.ttf");
	int index = 0;
	bool fallback = false;
	const bool factoryFace = font.family == "Courier New" && font.style == "Bold";
	const bool keepFallback = fontFallback_ && fontFace_ == identity;
	if (!factoryFace && keepFallback)
		fallback = true;
	else if (!factoryFace)
	{
		WindowsFontFace face;
		if (installedFonts().resolve(font.family, font.style, face))
		{
			path = face.pathUtf8;
			index = face.collectionIndex;
		}
		else fallback = true;
	}
	requestedFontFamily_ = font.family;
	requestedFontStyle_ = font.style;
	setResolvedText(text, identity, path, index, fontSize, dpi, dpi, color, fallback);
}

void TextLabel::setText(std::string text, const ResolvedTextStyle& style, SDL_Color color)
{
	setText(std::move(text), style.font, style.rasterSize, color, style.dpi);
}

void TextLabel::setResolvedText(std::string text, const std::string& identity, const std::string& path,
	int index, int fontSize, int dpiX, int dpiY, SDL_Color color, bool fallback)
{
	const bool textureChanged = !hasContent_ || text_ != text || fontFace_ != identity || fontPath_ != path ||
		fontIndex_ != index || fontSize_ != fontSize || dpiX_ != dpiX || dpiY_ != dpiY;
	text_ = text;
	fontFace_ = identity;
	fontPath_ = path;
	fontSize_ = fontSize;
	fontIndex_ = index;
	dpiX_ = dpiX;
	dpiY_ = dpiY;
	color_ = color;
	fontFallback_ = fallback;
	hasContent_ = true;
	if (textureChanged) createTextLabel();
}

void TextLabel::createTextLabel()
{
	textureDirty_ = true;
	if (textureId_)
	{
		glDeleteTextures(1, &textureId_);
		textureId_ = 0;
	}
	dimensions_ = glm::vec2(0.0f);
	const std::string bundledPath = applicationAssetPath("Resources/Courier New Bold.ttf");
	TTF_Font* font = findFont(fontPath_, fontIndex_, fontSize_, dpiX_, dpiY_);
	if (!font && fontPath_ != bundledPath)
	{
		fontPath_ = bundledPath;
		fontIndex_ = 0;
		fontFallback_ = true;
		font = findFont(fontPath_, fontIndex_, fontSize_, dpiX_, dpiY_);
	}
	if (!font) return;
	const SDL_Color white = { 255, 255, 255, 255 };
	SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text_.empty() ? " " : text_.c_str(), white);
	if (!rendered && fontPath_ != bundledPath)
	{
		fontPath_ = bundledPath;
		fontIndex_ = 0;
		fontFallback_ = true;
		font = findFont(fontPath_, fontIndex_, fontSize_, dpiX_, dpiY_);
		if (font) rendered = TTF_RenderUTF8_Blended(font, text_.empty() ? " " : text_.c_str(), white);
	}
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
	textureDirty_ = false;
}

void TextLabel::clearFontCache()
{
	std::map<std::string, TTF_Font*>& cache = fontCache();
	for (std::map<std::string, TTF_Font*>::iterator font = cache.begin(); font != cache.end(); ++font)
		TTF_CloseFont(font->second);
	cache.clear();
}

glm::vec2 TextLabel::measureText(const std::string& text, const UiFontReference& requestedFont, int fontSize, int dpi)
{
	const std::string bundledPath = applicationAssetPath("Resources/Courier New Bold.ttf");
	std::string path = bundledPath;
	int index = 0;
	if (!(requestedFont.family == "Courier New" && requestedFont.style == "Bold"))
	{
		WindowsFontFace face;
		if (installedFonts().resolve(requestedFont.family, requestedFont.style, face))
		{
			path = face.pathUtf8;
			index = face.collectionIndex;
		}
	}
	TTF_Font* font = findFont(path, index, fontSize, dpi, dpi);
	if (!font && path != bundledPath)
		font = findFont(bundledPath, 0, fontSize, dpi, dpi);
	if (!font) return glm::vec2(0.0f);
	int width = 0;
	int height = 0;
	const char* content = text.empty() ? " " : text.c_str();
	if (TTF_SizeUTF8(font, content, &width, &height) != 0 && path != bundledPath)
	{
		font = findFont(bundledPath, 0, fontSize, dpi, dpi);
		if (!font || TTF_SizeUTF8(font, content, &width, &height) != 0) return glm::vec2(0.0f);
	}
	return glm::vec2(static_cast<float>(width), static_cast<float>(height));
}

glm::vec2 TextLabel::measureText(const std::string& text, const ResolvedTextStyle& style)
{
	return measureText(text, style.font, style.rasterSize, style.dpi);
}
