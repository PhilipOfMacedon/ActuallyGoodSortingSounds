#pragma once

#include <string>

namespace settingsfile
{
	std::wstring defaultPath();
	std::wstring pathForFile(const wchar_t* filename);
	bool readAll(const std::wstring& path, std::string& content);
	bool replaceAtomically(const std::wstring& path, const std::string& content,
		int currentVersion, std::string* error, bool preserveMigrationBackup = true);
}
