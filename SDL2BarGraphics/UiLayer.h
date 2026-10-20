#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AppActions.h"

struct UiRect
{
	float x, y, width, height;
	UiRect(float left = 0.0f, float top = 0.0f, float w = 0.0f, float h = 0.0f)
		: x(left), y(top), width(w), height(h) {}
	bool contains(float px, float py) const
	{
		return px >= x && py >= y && px < x + width && py < y + height;
	}
};

// Reusable clipped viewport state for dynamic UI lists. The owner provides
// content bounds and paints/hit-tests child rows inside viewport(); wheel input
// is consumed only when its pointer is inside that viewport.
class UiScrollViewport
{
public:
	UiScrollViewport();
	void configure(const UiRect& viewport, float contentExtent, float rowExtent, float requestedOffset = 0.0f);
	const UiRect& viewport() const { return viewport_; }
	float offset() const { return offset_; }
	float maximumOffset() const;
	float contentExtent() const { return contentExtent_; }
	float rowExtent() const { return rowExtent_; }
	void setOffset(float value);
	bool scrollWheel(float x, float y, int wheelDelta, float rowsPerWheel = 3.0f);
	void ensureItemVisible(std::size_t index, float itemExtent = 0.0f);
	std::size_t firstVisibleItem(float itemExtent = 0.0f) const;
	std::size_t visibleItemCount(std::size_t itemCount, float itemExtent = 0.0f) const;
	int itemAt(float x, float y, std::size_t itemCount, float itemExtent = 0.0f) const;
	UiRect itemBounds(std::size_t index, float itemExtent = 0.0f) const;
	bool itemIntersectsViewport(std::size_t index, float itemExtent = 0.0f) const;

private:
	UiRect viewport_;
	float contentExtent_;
	float rowExtent_;
	float offset_;
};

enum class UiWidgetKind
{
	Button,
	ComboBox,
	Slider
};

enum class UiWidgetPresentation { Standard, RadioGroup, DosScrollbar, EditableLabel };
enum class UiControlRole { Standard, PanelToggle, Settings, AlgorithmChoice, BuildTrace, Playback, Reset, EditableValue, Choice };

enum class UiContainerKind
{
	Panel,
	Row,
	Column,
	Overlay
};

class UiContainer;

enum class UiInputType
{
	PointerMove,
	PointerDown,
	PointerUp,
	KeyDown,
	TextInput,
	Wheel,
	FocusLost
};

enum class UiKey
{
	None,
	Tab,
	Enter,
	Space,
	Backspace,
	Escape,
	Up,
	Down,
	Left,
	Right
};

struct UiInputEvent
{
	UiInputType type;
	float x;
	float y;
	UiKey key;
	bool shift;
	bool control;
	bool alt;
	int wheelDelta;
	std::string text;
	UiInputEvent(UiInputType eventType, float px = 0.0f, float py = 0.0f, UiKey eventKey = UiKey::None, bool shifted = false)
		: type(eventType), x(px), y(py), key(eventKey), shift(shifted), control(false), alt(false), wheelDelta(0), text() {}
	UiInputEvent(UiInputType eventType, const std::string& value)
		: type(eventType), x(0.0f), y(0.0f), key(UiKey::None), shift(false), control(false), alt(false), wheelDelta(0), text(value) {}
	UiInputEvent(UiInputType eventType, float px, float py, int wheel)
		: type(eventType), x(px), y(py), key(UiKey::None), shift(false), control(false), alt(false), wheelDelta(wheel), text() {}
};

enum class UiEventKind { None, Activated, SelectionChanged, ValueChanged, TextEntered, Wheel, FocusLost };

struct UiEvent
{
	std::string controlId;
	UiEventKind kind;
	float value;
	int selection;
	std::string text;
	bool shift;
	bool control;
	bool alt;
	UiEvent() : controlId(), kind(UiEventKind::None), value(0.0f), selection(-1), text(), shift(false), control(false), alt(false) {}
};

struct UiAction
{
	enum class ViewCommand { None, OpenSettings, CloseSettings, OpenAlgorithmMenu, OpenAppearanceEditor, CloseAppearanceEditor,
		ShowAppearanceSettings, ShowAlgorithmSettings, ShowAudioSettings, ShowGeneralAudioSettings,
	ShowAudioBasic, ShowAudioAdvanced };
	AppAction appAction;
	ViewCommand viewCommand;
	UiEvent event;
	UiAction(const AppAction& action = AppAction(), ViewCommand command = ViewCommand::None)
		: appAction(action), viewCommand(command), event() {}
};

class UiWidget
{
public:
	UiWidget(UiWidgetKind kind, const std::string& id, const std::string& label,
		const AppAction& action, UiAction::ViewCommand viewCommand);
	UiWidgetKind kind() const { return kind_; }
	UiWidgetPresentation presentation() const { return presentation_; }
	UiControlRole role() const { return role_; }
	const std::string& id() const { return id_; }
	const std::string& label() const { return label_; }
	const UiRect& bounds() const { return bounds_; }
	const UiRect& popupBounds() const { return popupBounds_; }
	bool enabled() const { return enabled_; }
	bool visible() const { return visible_; }
	bool hovered() const { return hovered_; }
	bool pressed() const { return pressed_; }
	bool focused() const { return focused_; }
	bool expanded() const { return expanded_; }
	int selectedIndex() const { return selectedIndex_; }
	const std::vector<std::string>& options() const { return options_; }
	const AppAction& action() const { return action_; }
	UiAction::ViewCommand viewCommand() const { return viewCommand_; }
	UiContainer* parentContainer() const { return parent_; }
	float optionHeight() const { return bounds_.height; }
	float value() const { return sliderValue_; }
	float minimum() const { return sliderMinimum_; }
	float maximum() const { return sliderMaximum_; }
	float step() const { return sliderStep_; }
	float scrollbarArrowSize() const { return scrollbarArrowSize_; }
	const std::vector<UiRect>& interactiveSubregions() const { return interactiveSubregions_; }

	void setBounds(const UiRect& value);
	void setPopupBounds(const UiRect& value);
	void setEnabled(bool value);
	void setVisible(bool value);
	void setLabel(const std::string& value) { label_ = value; }
	void setPresentation(UiWidgetPresentation value) { presentation_ = value; }
	void setRole(UiControlRole value) { role_ = value; }
	void setScrollbarArrowSize(float value) { scrollbarArrowSize_ = value < 0.0f ? 0.0f : value; }
	void setInteractiveSubregions(const std::vector<UiRect>& values) { interactiveSubregions_ = values; }
	void setOptions(const std::vector<std::string>& values);
	void setOptionEnabled(std::size_t index, bool enabled);
	bool optionEnabled(std::size_t index) const;
	void setSelectedIndex(int value);
	void setPopupAbove(bool value);
	std::size_t firstVisibleOption() const;
	std::size_t visibleOptionCount() const;
	void setSliderRange(float minimum, float maximum, float step, float value);
	void setSliderValue(float value);
	void setActionIndex(int value) { action_.index = value; }
	void setAction(const AppAction& action) { action_ = action; }
	UiRect optionBounds(std::size_t index) const;

private:
	friend class UIRoot;
	UiWidgetKind kind_;
	UiWidgetPresentation presentation_;
	UiControlRole role_;
	std::string id_;
	std::string label_;
	UiRect bounds_;
	UiRect popupBounds_;
	bool enabled_;
	bool visible_;
	bool hovered_;
	bool pressed_;
	bool focused_;
	bool expanded_;
	int selectedIndex_;
	std::vector<std::string> options_;
	std::vector<bool> optionEnabled_;
	AppAction action_;
	UiAction::ViewCommand viewCommand_;
	UiContainer* parent_;
	float sliderMinimum_;
	float sliderMaximum_;
	float sliderStep_;
	float sliderValue_;
	bool popupAbove_;
	float scrollbarArrowSize_;
	std::vector<UiRect> interactiveSubregions_;
	UiScrollViewport optionViewport_;
	float optionRowExtent_;
	void configureOptionViewport(float requestedOffset = -1.0f);
};

class UiContainer
{
public:
	UiContainerKind kind() const { return kind_; }
	const std::string& id() const { return id_; }
	const UiRect& bounds() const { return bounds_; }
	bool visible() const { return visible_; }
	void setVisible(bool value) { visible_ = value; }

private:
	friend class UIRoot;
	struct Child
	{
		UiContainer* container;
		UiWidget* widget;
		Child(UiContainer* value) : container(value), widget(nullptr) {}
		Child(UiWidget* value) : container(nullptr), widget(value) {}
	};
	std::string id_;
	UiContainerKind kind_;
	UiRect bounds_;
	UiContainer* parent_;
	bool visible_;
	std::vector<Child> children_;
	UiContainer(const std::string& id, UiContainerKind kind, UiContainer* parent)
		: id_(id), kind_(kind), bounds_(), parent_(parent), visible_(true), children_() {}
};

class UIRoot
{
public:
	UIRoot();
	UiWidget& addButton(const std::string& id, const std::string& label,
		const AppAction& action = AppAction(), UiAction::ViewCommand viewCommand = UiAction::ViewCommand::None);
	UiWidget& addComboBox(const std::string& id, const std::vector<std::string>& options, int selection = 0,
		const AppAction& action = AppAction());
	UiWidget& addSlider(const std::string& id, const std::string& label, float minimum, float maximum,
		float step, float value, const AppAction& action);
	UiWidget* find(const std::string& id);
	const UiWidget* find(const std::string& id) const;
	void setWidgetEnabled(const std::string& id, bool enabled);
	void setWidgetVisible(const std::string& id, bool visible);
	void setFocusOrder(const std::vector<std::string>& controlIds) { focusOrder_ = controlIds; }
	UiContainer& addContainer(const std::string& id, UiContainerKind kind, const std::string& parentId = "root");
	void attachWidget(const std::string& widgetId, const std::string& containerId);
	void clearContainers();
	void layoutContainer(const std::string& id, const UiRect& bounds, float gap = 0.0f);
	UiContainer* findContainer(const std::string& id);
	const std::vector<std::unique_ptr<UiWidget> >& widgets() const { return widgets_; }
	bool handleEvent(const UiInputEvent& event, UiAction& action);
	void closeCombo();
	bool hasOpenCombo() const { return openCombo_ != nullptr; }
	void setInputCaptured(bool value) { inputCaptured_ = value; if (!value) hasModalBounds_ = false; }
	void setModalBounds(const UiRect& bounds, UiAction::ViewCommand dismissCommand);
	bool inputCaptured() const { return inputCaptured_; }

private:
	std::vector<std::unique_ptr<UiWidget> > widgets_;
	std::vector<std::string> focusOrder_;
	UiContainer rootContainer_;
	std::vector<std::unique_ptr<UiContainer> > containers_;
	std::map<std::string, UiContainer*> containerLookup_;
	UiWidget* openCombo_;
	UiWidget* pressedWidget_;
	UiWidget* focusedWidget_;
	bool inputCaptured_;
	bool hasModalBounds_;
	UiRect modalBounds_;
	UiAction::ViewCommand dismissCommand_;
	bool pointerShift_;
	bool pointerControl_;
	bool pointerAlt_;

	UiWidget* hitWidget(float x, float y) const;
	bool isWidgetVisible(const UiWidget* widget) const;
	void layoutNode(UiContainer& container, const UiRect& bounds, float gap);
	void focus(UiWidget* widget);
	void focusNext(bool backwards);
	bool emit(UiWidget* widget, UiAction& action, bool selection, const UiInputEvent* event = nullptr);
	bool handleKey(const UiInputEvent& event, UiAction& action);
	bool setSliderFromPointer(UiWidget* widget, float x, UiAction& action);
};
