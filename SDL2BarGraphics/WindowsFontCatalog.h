#pragma once

#include <string>
#include <vector>

struct WindowsFontFace
{
	std::string family;
	std::string style;
	std::wstring path;
	std::string pathUtf8;
	int collectionIndex;
	WindowsFontFace() : family(), style(), path(), pathUtf8(), collectionIndex(0) {}
};

// DirectWrite is used only to find installed local faces. SDL_ttf remains the
// rasterizer, which keeps text textures on the same rendering path as today.
class WindowsFontCatalog
{
public:
	WindowsFontCatalog();
	const std::vector<WindowsFontFace>& faces() const { return faces_; }
	bool resolve(const std::string& family, const std::string& style, WindowsFontFace& face,
		std::string* error = nullptr) const;

private:
	std::vector<WindowsFontFace> faces_;
};
