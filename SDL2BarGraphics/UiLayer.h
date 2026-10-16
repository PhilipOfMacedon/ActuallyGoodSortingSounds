#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "AppController.h"

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

enum class UiWidgetKind
{
	Button,
	ComboBox,
	Slider
};

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
	KeyDown
};

enum class UiKey
{
	None,
	Tab,
	Enter,
	Space,
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
	UiInputEvent(UiInputType eventType, float px = 0.0f, float py = 0.0f, UiKey eventKey = UiKey::None, bool shifted = false)
		: type(eventType), x(px), y(py), key(eventKey), shift(shifted) {}
};

struct UiAction
{
	enum class ViewCommand { None, OpenSettings, CloseSettings, OpenAlgorithmMenu,
		ShowAppearanceSettings, ShowAlgorithmSettings, ShowAudioSettings, ShowGeneralAudioSettings,
		ShowAudioBasic, ShowAudioAdvanced };
	AppAction appAction;
	ViewCommand viewCommand;
	UiAction(const AppAction& action = AppAction(), ViewCommand command = ViewCommand::None)
		: appAction(action), viewCommand(command) {}
};

class UiWidget
{
public:
	UiWidget(UiWidgetKind kind, const std::string& id, const std::string& label,
		const AppAction& action, UiAction::ViewCommand viewCommand);
	UiWidgetKind kind() const { return kind_; }
	const std::string& id() const { return id_; }
	const std::string& label() const { return label_; }
	const UiRect& bounds() const { return bounds_; }
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

	void setBounds(const UiRect& value) { bounds_ = value; }
	void setEnabled(bool value);
	void setVisible(bool value);
	void setLabel(const std::string& value) { label_ = value; }
	void setOptions(const std::vector<std::string>& values);
	void setOptionEnabled(std::size_t index, bool enabled);
	bool optionEnabled(std::size_t index) const;
	void setSelectedIndex(int value);
	void setPopupAbove(bool value) { popupAbove_ = value; }
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
	std::string id_;
	std::string label_;
	UiRect bounds_;
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

	UiWidget* hitWidget(float x, float y) const;
	bool isWidgetVisible(const UiWidget* widget) const;
	void layoutNode(UiContainer& container, const UiRect& bounds, float gap);
	void focus(UiWidget* widget);
	void focusNext(bool backwards);
	bool emit(UiWidget* widget, UiAction& action, bool selection);
	bool handleKey(const UiInputEvent& event, UiAction& action);
	bool setSliderFromPointer(UiWidget* widget, float x, UiAction& action);
};
