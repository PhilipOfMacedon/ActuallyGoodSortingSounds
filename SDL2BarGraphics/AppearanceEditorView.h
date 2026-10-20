#pragma once

#include <functional>
#include <string>
#include <vector>

#include "AppearanceEditorLayout.h"
#include "AppearanceEditorModel.h"
#include "UiArchitecture.h"
#include "UiLayer.h"

struct AppearanceEditorPalette
{
	UiColor panel;
	UiColor border;
	UiColor accent;
	UiColor text;
	UiColor mutedText;
	UiColor overlayDim;
};

typedef std::function<std::vector<std::string>()> AppearanceFontFamilyProvider;
typedef std::function<std::vector<std::string>(const std::string&)> AppearanceFontStyleProvider;

enum class AppearanceEditorEffect
{
	None,
	BeginNumericEdit,
	BeginAlphaEdit,
	ChooseColor,
	ImportProfile,
	ExportProfile,
	Cancel,
	Apply
};

struct AppearanceEditorActionResult
{
	bool handled;
	bool requiresSynchronization;
	AppearanceEditorEffect effect;
};

// Owns the appearance editor's retained controls, transactional session, and
// responsive layout. Native dialogs and GPU resource submission stay in the
// platform-facing ControlPanel adapter.
class AppearanceEditorView
{
public:
	AppearanceEditorView(const std::vector<std::string>& presetNames,
		const std::vector<std::string>& paletteNames, const UiAppearancePreferences& initial,
		int initialPaletteIndex);

	void begin(const UiAppearancePreferences& current);
	void restore();
	void accept(const UiAppearancePreferences& committed);
	void synchronizeControls(const AppearanceFontFamilyProvider& fontFamilies,
		const AppearanceFontStyleProvider& fontStyles);
	AppearanceEditorActionResult handleAction(const UiAction& action,
		const AppearanceFontStyleProvider& fontStyles);
	void updateLayout(int logicalWidth, int logicalHeight);
	void setDescription(const std::string& value) { description_ = value; }

	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }
	AppearanceEditorSession& session() { return session_; }
	const AppearanceEditorSession& session() const { return session_; }
	const AppearanceEditorLayoutResult& layoutResult() const { return layout_; }
	const std::string& description() const { return description_; }

	void compose(int screenWidth, int screenHeight, const std::string& status, bool statusIsError,
		const UiTokenValue& selectedValue, const AppearanceEditorPalette& palette,
		UiPaintContext& paintContext) const;

private:
	UIRoot controls_;
	AppearanceEditorSession session_;
	AppearanceEditorLayoutResult layout_;
	std::string description_;
};
