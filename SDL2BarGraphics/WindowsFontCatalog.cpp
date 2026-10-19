#include "WindowsFontCatalog.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>
#include <memory>

namespace
{
	template <typename T> class ComPtr
	{
	public:
		ComPtr() : value_(nullptr) {}
		~ComPtr() { if (value_) value_->Release(); }
		T** put() { if (value_) { value_->Release(); value_ = nullptr; } return &value_; }
		T* get() const { return value_; }
		T* operator->() const { return value_; }
		operator bool() const { return value_ != nullptr; }
	private:
		T* value_;
		ComPtr(const ComPtr&);
		ComPtr& operator=(const ComPtr&);
	};

	std::string utf8(const std::wstring& value)
	{
		if (value.empty()) return std::string();
		const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
		if (count <= 0) return std::string();
		std::string result(static_cast<std::size_t>(count), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], count, nullptr, nullptr);
		return result;
	}

	std::wstring localizedString(IDWriteLocalizedStrings* strings)
	{
		if (!strings || strings->GetCount() == 0) return std::wstring();
		UINT32 index = 0;
		BOOL exists = FALSE;
		if (SUCCEEDED(strings->FindLocaleName(L"en-us", &index, &exists)) && !exists) index = 0;
		UINT32 length = 0;
		if (FAILED(strings->GetStringLength(index, &length))) return std::wstring();
		std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
		if (FAILED(strings->GetString(index, &result[0], length + 1))) return std::wstring();
		result.resize(length);
		return result;
	}

	std::string styleName(IDWriteFont* font)
	{
		if (!font) return "Regular";
		std::string result;
		switch (font->GetWeight())
		{
		case DWRITE_FONT_WEIGHT_THIN: result = "Thin"; break;
		case DWRITE_FONT_WEIGHT_EXTRA_LIGHT: result = "Extra Light"; break;
		case DWRITE_FONT_WEIGHT_LIGHT: result = "Light"; break;
		case DWRITE_FONT_WEIGHT_SEMI_LIGHT: result = "Semi Light"; break;
		case DWRITE_FONT_WEIGHT_NORMAL: result = "Regular"; break;
		case DWRITE_FONT_WEIGHT_MEDIUM: result = "Medium"; break;
		case DWRITE_FONT_WEIGHT_DEMI_BOLD: result = "Demi Bold"; break;
		case DWRITE_FONT_WEIGHT_BOLD: result = "Bold"; break;
		case DWRITE_FONT_WEIGHT_EXTRA_BOLD: result = "Extra Bold"; break;
		case DWRITE_FONT_WEIGHT_BLACK: result = "Black"; break;
		case DWRITE_FONT_WEIGHT_EXTRA_BLACK: result = "Extra Black"; break;
		default: result = "Regular"; break;
		}
		if (font->GetStyle() == DWRITE_FONT_STYLE_ITALIC) result += " Italic";
		else if (font->GetStyle() == DWRITE_FONT_STYLE_OBLIQUE) result += " Oblique";
		return result;
	}

	bool localFile(IDWriteFont* font, WindowsFontFace& result)
	{
		ComPtr<IDWriteFontFace> face;
		if (FAILED(font->CreateFontFace(face.put())) || !face) return false;
		UINT32 count = 0;
		if (FAILED(face->GetFiles(&count, nullptr)) || count == 0) return false;
		std::vector<IDWriteFontFile*> files(count, nullptr);
		if (FAILED(face->GetFiles(&count, &files[0])))
		{
			for (std::size_t i = 0; i < files.size(); ++i) if (files[i]) files[i]->Release();
			return false;
		}
		bool found = false;
		UINT32 released = 0;
		for (UINT32 i = 0; i < count && !found; ++i)
		{
			ComPtr<IDWriteFontFileLoader> loader;
			if (SUCCEEDED(files[i]->GetLoader(loader.put())) && loader)
			{
				ComPtr<IDWriteLocalFontFileLoader> localLoader;
				if (SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(localLoader.put()))) && localLoader)
				{
					const void* key = nullptr;
					UINT32 keySize = 0;
					if (SUCCEEDED(files[i]->GetReferenceKey(&key, &keySize)))
					{
						UINT32 pathLength = 0;
						if (SUCCEEDED(localLoader->GetFilePathLengthFromKey(key, keySize, &pathLength)))
						{
							result.path.assign(static_cast<std::size_t>(pathLength) + 1, L'\0');
							if (SUCCEEDED(localLoader->GetFilePathFromKey(key, keySize, &result.path[0], pathLength + 1)))
							{
								result.path.resize(pathLength);
								result.pathUtf8 = utf8(result.path);
								result.collectionIndex = static_cast<int>(face->GetIndex());
								found = true;
							}
						}
					}
				}
			}
			if (files[i]) files[i]->Release();
			released = i + 1;
		}
		for (UINT32 i = released; i < count; ++i) if (files[i]) files[i]->Release();
		return found;
	}

	bool equalInsensitive(const std::string& left, const std::string& right)
	{
		if (left.size() != right.size()) return false;
		for (std::size_t i = 0; i < left.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) return false;
		return true;
	}
}

WindowsFontCatalog::WindowsFontCatalog() : faces_()
{
	ComPtr<IDWriteFactory> factory;
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(factory.put()))) || !factory) return;
	ComPtr<IDWriteFontCollection> collection;
	if (FAILED(factory->GetSystemFontCollection(collection.put(), FALSE)) || !collection) return;
	for (UINT32 familyIndex = 0; familyIndex < collection->GetFontFamilyCount(); ++familyIndex)
	{
		ComPtr<IDWriteFontFamily> family;
		if (FAILED(collection->GetFontFamily(familyIndex, family.put())) || !family) continue;
		ComPtr<IDWriteLocalizedStrings> familyNames;
		if (FAILED(family->GetFamilyNames(familyNames.put())) || !familyNames) continue;
		const std::string familyName = utf8(localizedString(familyNames.get()));
		if (familyName.empty()) continue;
		for (UINT32 fontIndex = 0; fontIndex < family->GetFontCount(); ++fontIndex)
		{
			ComPtr<IDWriteFont> font;
			if (FAILED(family->GetFont(fontIndex, font.put())) || !font) continue;
			WindowsFontFace face;
			face.family = familyName;
			face.style = styleName(font.get());
			if (localFile(font.get(), face)) faces_.push_back(face);
		}
	}
	std::sort(faces_.begin(), faces_.end(), [](const WindowsFontFace& a, const WindowsFontFace& b)
		{ return a.family == b.family ? a.style < b.style : a.family < b.family; });
}

bool WindowsFontCatalog::resolve(const std::string& family, const std::string& style, WindowsFontFace& face,
	std::string* error) const
{
	for (std::size_t i = 0; i < faces_.size(); ++i)
		if (equalInsensitive(faces_[i].family, family) && equalInsensitive(faces_[i].style, style))
		{
			face = faces_[i];
			if (error) error->clear();
			return true;
		}
	if (error) *error = "The requested installed font face could not be resolved.";
	return false;
}
