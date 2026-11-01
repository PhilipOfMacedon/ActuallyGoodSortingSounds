#pragma once

#include <string>

#include "UiAppearance.h"

class Window;
struct DemoPreset;

enum class AppearanceDialogStatus
{
	Cancelled,
	Completed,
	Failed
};

struct AppearanceDialogResult
{
	AppearanceDialogStatus status;
	UiAppearanceProfile profile;
	std::string error;

	AppearanceDialogResult(AppearanceDialogStatus result = AppearanceDialogStatus::Cancelled)
		: status(result), profile(), error() {}
};

struct DemoPresetDialogResult
{
	AppearanceDialogStatus status;
	std::string content;
	std::string error;
	DemoPresetDialogResult(AppearanceDialogStatus result = AppearanceDialogStatus::Cancelled)
		: status(result), content(), error() {}
};

// Windows file/color dialogs and profile file access belong to the application
// host. The editor receives values and status, not Win32 handles or file APIs.
class AppearanceDialogAdapter
{
public:
	static bool chooseColor(Window& window, const UiColor& initial, UiColor& selected);
	static AppearanceDialogResult importProfile(Window& window);
	static AppearanceDialogResult exportProfile(Window& window, UiPresetId preset,
		const UiThemeCustomization& theme);
	static DemoPresetDialogResult importDemoPresets(Window& window);
	static DemoPresetDialogResult exportDemoPreset(Window& window, const DemoPreset& preset);
};
