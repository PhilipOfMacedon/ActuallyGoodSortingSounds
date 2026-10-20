#pragma once

#include <string>

namespace settingsfile
{
	std::wstring defaultPath();
	bool readAll(const std::wstring& path, std::string& content);
	bool replaceAtomically(const std::wstring& path, const std::string& content,
		int currentVersion, std::string* error);
}
