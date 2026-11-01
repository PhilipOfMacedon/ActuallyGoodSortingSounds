#include "AppearanceDialogAdapter.h"

#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>

#include "Window.h"
#include "DemoModels.h"

namespace
{
	bool writeUtf8File(const std::wstring& path, const std::string& contents, std::string* error)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			if (error) *error = "Could not create the profile file.";
			return false;
		}
		DWORD written = 0;
		const BOOL ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size();
		CloseHandle(file);
		if (!ok && error) *error = "Could not write the full profile file.";
		return ok != FALSE;
	}

	bool readUtf8File(const std::wstring& path, std::string& contents, std::string* error)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			if (error) *error = "Could not open the profile file.";
			return false;
		}
		LARGE_INTEGER size;
		if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 1024 * 1024)
		{
			CloseHandle(file);
			if (error) *error = "The profile file is too large or could not be read.";
			return false;
		}
		contents.assign(static_cast<std::size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		const BOOL ok = contents.empty() || ReadFile(file, &contents[0], static_cast<DWORD>(contents.size()), &read, nullptr);
		CloseHandle(file);
		if (!ok || read != contents.size())
		{
			if (error) *error = "Could not read the full profile file.";
			return false;
		}
		return true;
	}

	HWND dialogOwner(Window& window)
	{
		return window.getWindow() ? GetActiveWindow() : nullptr;
	}
}

bool AppearanceDialogAdapter::chooseColor(Window& window, const UiColor& initial, UiColor& selected)
{
	static COLORREF customColors[16] = {};
	CHOOSECOLORW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = dialogOwner(window);
	dialog.rgbResult = RGB(static_cast<BYTE>(initial.r * 255.0f), static_cast<BYTE>(initial.g * 255.0f),
		static_cast<BYTE>(initial.b * 255.0f));
	dialog.lpCustColors = customColors;
	dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
	if (!ChooseColorW(&dialog)) return false;
	selected = UiColor(static_cast<float>(GetRValue(dialog.rgbResult)) / 255.0f,
		static_cast<float>(GetGValue(dialog.rgbResult)) / 255.0f,
		static_cast<float>(GetBValue(dialog.rgbResult)) / 255.0f, initial.a);
	return true;
}

AppearanceDialogResult AppearanceDialogAdapter::importProfile(Window& window)
{
	wchar_t path[MAX_PATH] = {};
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = dialogOwner(window);
	dialog.lpstrFilter = L"AGSS UI profiles (*.agss-ui.ini)\0*.agss-ui.ini\0INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0";
	dialog.lpstrFile = path;
	dialog.nMaxFile = MAX_PATH;
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog)) return AppearanceDialogResult(AppearanceDialogStatus::Cancelled);

	std::string contents;
	AppearanceDialogResult result(AppearanceDialogStatus::Failed);
	if (!readUtf8File(path, contents, &result.error)) return result;
	if (!parseUiAppearanceProfile(contents, result.profile, &result.error)) return result;
	result.status = AppearanceDialogStatus::Completed;
	return result;
}

AppearanceDialogResult AppearanceDialogAdapter::exportProfile(Window& window, UiPresetId preset,
	const UiThemeCustomization& theme)
{
	wchar_t path[MAX_PATH] = L"appearance.agss-ui.ini";
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = dialogOwner(window);
	dialog.lpstrFilter = L"AGSS UI profiles (*.agss-ui.ini)\0*.agss-ui.ini\0\0";
	dialog.lpstrFile = path;
	dialog.nMaxFile = MAX_PATH;
	dialog.lpstrDefExt = L"agss-ui.ini";
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&dialog)) return AppearanceDialogResult(AppearanceDialogStatus::Cancelled);

	AppearanceDialogResult result(AppearanceDialogStatus::Failed);
	const std::string contents = serializeUiAppearanceProfile(preset, theme);
	if (!writeUtf8File(path, contents, &result.error)) return result;
	result.status = AppearanceDialogStatus::Completed;
	return result;
}

DemoPresetDialogResult AppearanceDialogAdapter::importDemoPresets(Window& window)
{
	wchar_t path[MAX_PATH] = {};
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = dialogOwner(window);
	dialog.lpstrFilter = L"AGSS demo presets (*.agss-demo.ini)\0*.agss-demo.ini\0INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0";
	dialog.lpstrFile = path;
	dialog.nMaxFile = MAX_PATH;
	dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog)) return DemoPresetDialogResult(AppearanceDialogStatus::Cancelled);

	DemoPresetDialogResult result(AppearanceDialogStatus::Failed);
	if (!readUtf8File(path, result.content, &result.error)) return result;
	result.status = AppearanceDialogStatus::Completed;
	return result;
}

DemoPresetDialogResult AppearanceDialogAdapter::exportDemoPreset(Window& window, const DemoPreset& preset)
{
	wchar_t path[MAX_PATH] = L"demo.agss-demo.ini";
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = dialogOwner(window);
	dialog.lpstrFilter = L"AGSS demo presets (*.agss-demo.ini)\0*.agss-demo.ini\0\0";
	dialog.lpstrFile = path;
	dialog.nMaxFile = MAX_PATH;
	dialog.lpstrDefExt = L"agss-demo.ini";
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&dialog)) return DemoPresetDialogResult(AppearanceDialogStatus::Cancelled);

	DemoPreset exportPreset = preset;
	exportPreset.prefab = false;
	if (exportPreset.id.empty() || demoPrefabPreset(exportPreset.id)) exportPreset.id = "export.demo";
	if (exportPreset.name.empty()) exportPreset.name = "Demo export";
	DemoPresetDialogResult result(AppearanceDialogStatus::Failed);
	if (!writeUtf8File(path, DemoPresetStore::encode(std::vector<DemoPreset>(1, exportPreset)), &result.error)) return result;
	result.status = AppearanceDialogStatus::Completed;
	return result;
}
