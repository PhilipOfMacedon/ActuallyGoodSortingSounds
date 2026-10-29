#include "SettingsFileStorage.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdlib>

namespace
{
	const wchar_t* SettingsDirectory = L"\\ActuallyGoodSortingSounds";
	const wchar_t* SettingsFile = L"settings.ini";
	const wchar_t* SettingsBackupSuffix = L".pre-v8.bak";
	const std::size_t MaxSettingsFileBytes = 1024 * 1024;

	std::wstring directoryOf(const std::wstring& path)
	{
		const std::wstring::size_type split = path.find_last_of(L"\\/");
		return split == std::wstring::npos ? std::wstring() : path.substr(0, split);
	}
}

namespace settingsfile
{
	std::wstring defaultPath()
	{
		return pathForFile(SettingsFile);
	}

	std::wstring pathForFile(const wchar_t* filename)
	{
		if (!filename || !*filename) return std::wstring();
		DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
		if (size == 0) return std::wstring();
		std::wstring base(size, L'\0');
		const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", &base[0], size);
		if (copied == 0 || copied >= size) return std::wstring();
		base.resize(copied);
		return base + SettingsDirectory + L"\\" + filename;
	}

	bool readAll(const std::wstring& path, std::string& content)
	{
		content.clear();
		if (path.empty()) return false;
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		LARGE_INTEGER fileSize;
		if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0 ||
			fileSize.QuadPart > static_cast<LONGLONG>(MaxSettingsFileBytes))
		{
			CloseHandle(file);
			return false;
		}
		content.assign(static_cast<std::size_t>(fileSize.QuadPart), '\0');
		DWORD read = 0;
		const BOOL readOk = content.empty() || ReadFile(file, &content[0], static_cast<DWORD>(content.size()), &read, nullptr);
		CloseHandle(file);
		if (!readOk || read != content.size())
		{
			content.clear();
			return false;
		}
		return true;
	}

	bool replaceAtomically(const std::wstring& path, const std::string& content,
		int currentVersion, std::string* error, bool preserveMigrationBackup)
	{
		const std::wstring directory = directoryOf(path);
		if (directory.empty() || (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
		{
			if (error) *error = "Could not create the settings directory.";
			return false;
		}

		const std::wstring temporaryPath = path + L".tmp";
		HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			if (error) *error = "Could not open the temporary settings file.";
			return false;
		}
		DWORD written = 0;
		const BOOL wrote = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
		const BOOL flushed = wrote && written == content.size() ? FlushFileBuffers(file) : FALSE;
		CloseHandle(file);
		if (!flushed)
		{
			DeleteFileW(temporaryPath.c_str());
			if (error) *error = "Could not flush the temporary settings file.";
			return false;
		}

		const DWORD existingAttributes = GetFileAttributesW(path.c_str());
		if (preserveMigrationBackup && existingAttributes != INVALID_FILE_ATTRIBUTES)
		{
			const std::wstring backupPath = path + SettingsBackupSuffix;
			if (GetFileAttributesW(backupPath.c_str()) == INVALID_FILE_ATTRIBUTES)
			{
				HANDLE oldFile = CreateFileW(path.c_str(), GENERIC_READ,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
					OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				char header[128] = {};
				DWORD headerBytes = 0;
				if (oldFile != INVALID_HANDLE_VALUE)
				{
					ReadFile(oldFile, header, sizeof(header) - 1, &headerBytes, nullptr);
					CloseHandle(oldFile);
				}
				const std::string headerText(header, headerBytes);
				const std::string::size_type versionAt = headerText.find("version=");
				const int oldVersion = versionAt == std::string::npos
					? currentVersion : std::atoi(headerText.c_str() + versionAt + 8);
				if (oldVersion > 0 && oldVersion < currentVersion &&
					!CopyFileW(path.c_str(), backupPath.c_str(), TRUE))
				{
					DeleteFileW(temporaryPath.c_str());
					if (error) *error = "Could not preserve the previous settings file before its version-8 migration.";
					return false;
				}
			}
		}

		if (!MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileW(temporaryPath.c_str());
			if (error) *error = "Could not atomically replace the settings file.";
			return false;
		}
		if (error) error->clear();
		return true;
	}
}
