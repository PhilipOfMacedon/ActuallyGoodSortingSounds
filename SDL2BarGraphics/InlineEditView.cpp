#include "InlineEditView.h"

#include "AppActions.h"
#include "AppearanceEditorView.h"
#include "MainControlsView.h"

namespace
{
	InlineEditTarget targetForControl(const std::string& controlId)
	{
		if (controlId == "bars_value") return InlineEditTarget::ItemCount;
		if (controlId == "time_value") return InlineEditTarget::Duration;
		if (controlId == "appearance_value") return InlineEditTarget::AppearanceNumber;
		if (controlId == "appearance_color_alpha") return InlineEditTarget::AppearanceAlpha;
		return InlineEditTarget::None;
	}

	const char* controlForTarget(InlineEditTarget target)
	{
		switch (target)
		{
		case InlineEditTarget::ItemCount: return "bars_value";
		case InlineEditTarget::Duration: return "time_value";
		case InlineEditTarget::AppearanceNumber: return "appearance_value";
		case InlineEditTarget::AppearanceAlpha: return "appearance_color_alpha";
		case InlineEditTarget::None: return "";
		}
		return "";
	}
}

InlineEditView::InlineEditView()
	: editor_()
{
}

bool InlineEditView::editsControl(const std::string& controlId) const
{
	return active() && controlId == controlForTarget(target());
}

bool InlineEditView::beginForControl(const std::string& controlId, int itemCount, float durationSeconds,
	MainControlsView& mainControls, AppearanceEditorView& appearanceEditor)
{
	if (active()) return false;
	const InlineEditTarget target = targetForControl(controlId);
	if (target == InlineEditTarget::None) return false;
	std::string initialText;
	if (target == InlineEditTarget::AppearanceNumber || target == InlineEditTarget::AppearanceAlpha)
	{
		const AppearanceInlineEditField field = target == InlineEditTarget::AppearanceAlpha
			? AppearanceInlineEditField::Alpha : AppearanceInlineEditField::Number;
		if (!appearanceEditor.beginInlineValueEdit(field, initialText)) return false;
	}
	else if (!mainControls.beginInlineValueEdit(target, itemCount, durationSeconds, initialText)) return false;

	editor_.begin(target, initialText);
	present(mainControls, appearanceEditor);
	return true;
}

bool InlineEditView::appendText(const char* text, MainControlsView& mainControls,
	AppearanceEditorView& appearanceEditor)
{
	if (!editor_.appendText(text)) return false;
	present(mainControls, appearanceEditor);
	return true;
}

bool InlineEditView::backspace(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor)
{
	if (!editor_.backspace()) return false;
	present(mainControls, appearanceEditor);
	return true;
}

bool InlineEditView::clear(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor)
{
	if (!active()) return false;
	editor_.clear();
	present(mainControls, appearanceEditor);
	return true;
}

bool InlineEditView::commit(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor,
	AppAction& action, bool& appearanceNeedsSynchronization)
{
	appearanceNeedsSynchronization = false;
	if (!active()) return false;
	if (isAppearanceEdit())
	{
		double parsedValue = 0.0;
		const bool parsedSuccessfully = editor_.tryParseDouble(parsedValue);
		const AppearanceInlineEditField field = target() == InlineEditTarget::AppearanceAlpha
			? AppearanceInlineEditField::Alpha : AppearanceInlineEditField::Number;
		if (!appearanceEditor.commitInlineValueEdit(field, parsedSuccessfully, parsedValue)) return false;
		editor_.finish();
		appearanceNeedsSynchronization = true;
		return true;
	}

	if (!mainControls.commitInlineValueEdit(target(), editor_, action)) return false;
	editor_.finish();
	return true;
}

InlineEditTarget InlineEditView::cancel(MainControlsView& mainControls, int itemCount, float durationSeconds)
{
	if (!active()) return InlineEditTarget::None;
	const InlineEditTarget cancelledTarget = target();
	editor_.finish();
	if (cancelledTarget == InlineEditTarget::ItemCount || cancelledTarget == InlineEditTarget::Duration)
		mainControls.restoreInlineValueEdit(cancelledTarget, itemCount, durationSeconds);
	return cancelledTarget;
}

void InlineEditView::synchronizePresentation(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor)
{
	if (active()) present(mainControls, appearanceEditor);
}

void InlineEditView::present(MainControlsView& mainControls, AppearanceEditorView& appearanceEditor)
{
	if (isAppearanceEdit())
		appearanceEditor.showInlineValueEdit(target() == InlineEditTarget::AppearanceAlpha
			? AppearanceInlineEditField::Alpha : AppearanceInlineEditField::Number, text());
	else
		mainControls.showInlineValueEdit(target(), text());
}
