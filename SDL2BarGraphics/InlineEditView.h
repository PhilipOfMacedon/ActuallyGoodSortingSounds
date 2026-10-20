#pragma once

#include <string>

#include "InlineEditModel.h"

class MainControlsView;
class AppearanceEditorView;
struct AppAction;

// Owns the shared inline-edit session across the main controls and appearance
// editor. The host remains responsible for SDL text-input lifecycle and key
// translation.
class InlineEditView
{
public:
	InlineEditView();

	bool active() const { return editor_.active(); }
	bool isAppearanceEdit() const { return editor_.isAppearanceEdit(); }
	InlineEditTarget target() const { return editor_.target(); }
	const std::string& text() const { return editor_.text(); }
	bool editsControl(const std::string& controlId) const;

	bool beginForControl(const std::string& controlId, int itemCount, float durationSeconds,
		MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
	bool appendText(const char* text, MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
	bool backspace(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
	bool clear(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
	void synchronizePresentation(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
	bool commit(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor,
		AppAction& action, bool& appearanceNeedsSynchronization);
	InlineEditTarget cancel(MainControlsView& mainControls, int itemCount, float durationSeconds);

private:
	InlineEditModel editor_;
	void present(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor);
};
