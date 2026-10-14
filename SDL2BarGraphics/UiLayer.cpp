#include "UiLayer.h"

#include <algorithm>
#include <cmath>

UiWidget::UiWidget(UiWidgetKind kind, const std::string& id, const std::string& label,
	const AppAction& action, UiAction::ViewCommand viewCommand)
	: kind_(kind), id_(id), label_(label), bounds_(), enabled_(true), visible_(true),
		hovered_(false), pressed_(false), focused_(false), expanded_(false), selectedIndex_(0), options_(), optionEnabled_(),
		action_(action), viewCommand_(viewCommand), parent_(nullptr), sliderMinimum_(0.0f), sliderMaximum_(1.0f),
		sliderStep_(0.01f), sliderValue_(0.0f), popupAbove_(false)
{
}

void UiWidget::setEnabled(bool value)
{
	enabled_ = value;
	if (!value)
	{
		pressed_ = false;
		expanded_ = false;
	}
}

void UiWidget::setVisible(bool value)
{
	visible_ = value;
	if (!value)
	{
		pressed_ = false;
		expanded_ = false;
		hovered_ = false;
		focused_ = false;
	}
}

UIRoot::UIRoot()
	: widgets_(), rootContainer_("root", UiContainerKind::Overlay, nullptr), containers_(), containerLookup_(),
		openCombo_(nullptr), pressedWidget_(nullptr), focusedWidget_(nullptr), inputCaptured_(false),
		hasModalBounds_(false), modalBounds_(), dismissCommand_(UiAction::ViewCommand::None)
{
	containerLookup_["root"] = &rootContainer_;
}

void UiWidget::setOptions(const std::vector<std::string>& values)
{
	options_ = values;
	optionEnabled_.assign(values.size(), true);
	setSelectedIndex(selectedIndex_);
}

void UiWidget::setOptionEnabled(std::size_t index, bool enabled)
{
	if (index < optionEnabled_.size()) optionEnabled_[index] = enabled;
}

bool UiWidget::optionEnabled(std::size_t index) const
{
	return index < optionEnabled_.size() && optionEnabled_[index];
}

void UiWidget::setSelectedIndex(int value)
{
	if (options_.empty())
		selectedIndex_ = 0;
	else
		selectedIndex_ = std::max(0, std::min(static_cast<int>(options_.size()) - 1, value));
	if (kind_ == UiWidgetKind::ComboBox && !options_.empty())
		label_ = options_[selectedIndex_];
}

void UiWidget::setSliderRange(float minimum, float maximum, float step, float value)
{
	sliderMinimum_ = minimum;
	sliderMaximum_ = std::max(minimum, maximum);
	sliderStep_ = std::max(0.000001f, step);
	setSliderValue(value);
}

void UiWidget::setSliderValue(float value)
{
	const float clamped = std::max(sliderMinimum_, std::min(sliderMaximum_, value));
	const float steps = std::floor((clamped - sliderMinimum_) / sliderStep_ + 0.5f);
	sliderValue_ = std::max(sliderMinimum_, std::min(sliderMaximum_, sliderMinimum_ + steps * sliderStep_));
}

UiRect UiWidget::optionBounds(std::size_t index) const
{
	if (index >= options_.size() || index < firstVisibleOption() || index >= firstVisibleOption() + visibleOptionCount())
		return UiRect();
	if (options_.size() <= 8)
		return UiRect(bounds_.x, bounds_.y + bounds_.height * (1.0f + static_cast<float>(index)), bounds_.width, bounds_.height);
	const std::size_t localIndex = index - firstVisibleOption();
	const float popupHeight = bounds_.height * static_cast<float>(visibleOptionCount());
	const float top = popupAbove_ ? bounds_.y - popupHeight : bounds_.y + bounds_.height;
	return UiRect(bounds_.x, top + bounds_.height * static_cast<float>(localIndex), bounds_.width, bounds_.height);
}

std::size_t UiWidget::firstVisibleOption() const
{
	const std::size_t visible = visibleOptionCount();
	if (options_.size() <= visible) return 0;
	const std::size_t half = visible / 2;
	std::size_t first = selectedIndex_ > static_cast<int>(half) ? static_cast<std::size_t>(selectedIndex_) - half : 0;
	const std::size_t maximumFirst = options_.size() - visible;
	return std::min(first, maximumFirst);
}

std::size_t UiWidget::visibleOptionCount() const
{
	return std::min<std::size_t>(options_.size(), 8);
}

UiWidget& UIRoot::addButton(const std::string& id, const std::string& label,
	const AppAction& action, UiAction::ViewCommand viewCommand)
{
	widgets_.push_back(std::unique_ptr<UiWidget>(new UiWidget(UiWidgetKind::Button, id, label, action, viewCommand)));
	return *widgets_.back();
}

UiWidget& UIRoot::addComboBox(const std::string& id, const std::vector<std::string>& options, int selection,
	const AppAction& action)
{
	widgets_.push_back(std::unique_ptr<UiWidget>(new UiWidget(UiWidgetKind::ComboBox, id, std::string(), action,
		UiAction::ViewCommand::None)));
	widgets_.back()->setOptions(options);
	widgets_.back()->setSelectedIndex(selection);
	return *widgets_.back();
}

UiWidget& UIRoot::addSlider(const std::string& id, const std::string& label, float minimum, float maximum,
	float step, float value, const AppAction& action)
{
	widgets_.push_back(std::unique_ptr<UiWidget>(new UiWidget(UiWidgetKind::Slider, id, label, action,
		UiAction::ViewCommand::None)));
	widgets_.back()->setSliderRange(minimum, maximum, step, value);
	return *widgets_.back();
}

UiWidget* UIRoot::find(const std::string& id)
{
	for (std::size_t i = 0; i < widgets_.size(); ++i)
		if (widgets_[i]->id() == id)
			return widgets_[i].get();
	return nullptr;
}

const UiWidget* UIRoot::find(const std::string& id) const
{
	for (std::size_t i = 0; i < widgets_.size(); ++i)
		if (widgets_[i]->id() == id)
			return widgets_[i].get();
	return nullptr;
}

void UIRoot::setWidgetEnabled(const std::string& id, bool enabled)
{
	UiWidget* widget = find(id);
	if (!widget) return;
	widget->setEnabled(enabled);
	if (!enabled && openCombo_ == widget)
		closeCombo();
	if (!enabled && focusedWidget_ == widget)
	{
		focus(nullptr);
		focusNext(false);
	}
}

void UIRoot::setWidgetVisible(const std::string& id, bool visible)
{
	UiWidget* widget = find(id);
	if (!widget) return;
	if (widget->visible() == visible) return;
	widget->setVisible(visible);
	if (!visible && openCombo_ == widget)
		closeCombo();
	if (!visible && focusedWidget_ == widget)
	{
		focus(nullptr);
		focusNext(false);
	}
}

UiContainer& UIRoot::addContainer(const std::string& id, UiContainerKind kind, const std::string& parentId)
{
	std::map<std::string, UiContainer*>::iterator parent = containerLookup_.find(parentId);
	UiContainer* parentContainer = parent == containerLookup_.end() ? &rootContainer_ : parent->second;
	std::map<std::string, UiContainer*>::iterator existing = containerLookup_.find(id);
	if (existing != containerLookup_.end())
		return *existing->second;
	containers_.push_back(std::unique_ptr<UiContainer>(new UiContainer(id, kind, parentContainer)));
	UiContainer* result = containers_.back().get();
	parentContainer->children_.push_back(UiContainer::Child(result));
	containerLookup_[id] = result;
	return *result;
}

void UIRoot::attachWidget(const std::string& widgetId, const std::string& containerId)
{
	UiWidget* widget = find(widgetId);
	UiContainer* container = findContainer(containerId);
	if (!widget || !container)
		return;
	if (widget->parent_)
	{
		std::vector<UiContainer::Child>& siblings = widget->parent_->children_;
		siblings.erase(std::remove_if(siblings.begin(), siblings.end(), [widget](const UiContainer::Child& child)
			{ return child.widget == widget; }), siblings.end());
	}
	widget->parent_ = container;
	container->children_.push_back(UiContainer::Child(widget));
}

void UIRoot::clearContainers()
{
	for (std::size_t i = 0; i < widgets_.size(); ++i)
		widgets_[i]->parent_ = nullptr;
	rootContainer_.children_.clear();
	containers_.clear();
	containerLookup_.clear();
	containerLookup_["root"] = &rootContainer_;
}

UiContainer* UIRoot::findContainer(const std::string& id)
{
	std::map<std::string, UiContainer*>::iterator found = containerLookup_.find(id);
	return found == containerLookup_.end() ? nullptr : found->second;
}

void UIRoot::layoutContainer(const std::string& id, const UiRect& bounds, float gap)
{
	UiContainer* container = findContainer(id);
	if (container)
		layoutNode(*container, bounds, gap);
}

void UIRoot::layoutNode(UiContainer& container, const UiRect& bounds, float gap)
{
	container.bounds_ = bounds;
	const std::size_t count = container.children_.size();
	if (count == 0)
		return;
	const bool horizontal = container.kind_ == UiContainerKind::Row;
	const bool vertical = container.kind_ == UiContainerKind::Column;
	if (!horizontal && !vertical)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			UiContainer::Child& child = container.children_[i];
			if (child.container) layoutNode(*child.container, bounds, gap);
			else if (child.widget) child.widget->setBounds(bounds);
		}
		return;
	}
	const float total = horizontal ? bounds.width : bounds.height;
	const float extent = std::max(1.0f, (total - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
	for (std::size_t i = 0; i < count; ++i)
	{
		const float offset = static_cast<float>(i) * (extent + gap);
		const UiRect childBounds = horizontal
			? UiRect(bounds.x + offset, bounds.y, extent, bounds.height)
			: UiRect(bounds.x, bounds.y + offset, bounds.width, extent);
		UiContainer::Child& child = container.children_[i];
		if (child.container) layoutNode(*child.container, childBounds, gap);
		else if (child.widget) child.widget->setBounds(childBounds);
	}
}

bool UIRoot::handleEvent(const UiInputEvent& event, UiAction& action)
{
	if (event.type == UiInputType::KeyDown)
		return handleKey(event, action);
	if (event.type == UiInputType::PointerMove)
	{
		for (std::size_t i = 0; i < widgets_.size(); ++i)
			widgets_[i]->hovered_ = isWidgetVisible(widgets_[i].get()) && widgets_[i]->bounds_.contains(event.x, event.y);
		if (pressedWidget_ && pressedWidget_->kind_ == UiWidgetKind::Slider && pressedWidget_->enabled_)
		{
			setSliderFromPointer(pressedWidget_, event.x, action);
			return true;
		}
		return inputCaptured_ || openCombo_ != nullptr || hitWidget(event.x, event.y) != nullptr;
	}
	if (event.type == UiInputType::PointerDown)
	{
		if (inputCaptured_ && hasModalBounds_ && !modalBounds_.contains(event.x, event.y))
		{
			closeCombo();
			action = UiAction(AppAction(), dismissCommand_);
			return true;
		}
		if (openCombo_)
		{
			for (std::size_t option = 0; option < openCombo_->options_.size(); ++option)
			{
				if (openCombo_->optionEnabled(option) && openCombo_->optionBounds(option).contains(event.x, event.y))
				{
					const bool changed = openCombo_->selectedIndex_ != static_cast<int>(option);
					openCombo_->setSelectedIndex(static_cast<int>(option));
					UiWidget* combo = openCombo_;
					closeCombo();
					if (changed) emit(combo, action, true);
					return true;
				}
			}
			if (!openCombo_->bounds_.contains(event.x, event.y))
				closeCombo();
		}
		UiWidget* widget = hitWidget(event.x, event.y);
		if (!widget)
			return inputCaptured_;
		focus(widget);
		if (!widget->enabled_)
			return true;
		if (widget->kind_ == UiWidgetKind::ComboBox)
		{
			if (openCombo_ == widget) closeCombo();
			else { closeCombo(); openCombo_ = widget; widget->expanded_ = true; }
			return true;
		}
		widget->pressed_ = true;
		pressedWidget_ = widget;
		if (widget->kind_ == UiWidgetKind::Slider)
			setSliderFromPointer(widget, event.x, action);
		return true;
	}
	if (event.type == UiInputType::PointerUp)
	{
		if (!pressedWidget_)
			return inputCaptured_;
		UiWidget* pressed = pressedWidget_;
		pressedWidget_ = nullptr;
		pressed->pressed_ = false;
		if (pressed->enabled_ && pressed->visible_ && pressed->bounds_.contains(event.x, event.y))
			return emit(pressed, action, false);
		return true;
	}
	return false;
}

void UIRoot::closeCombo()
{
	if (openCombo_)
		openCombo_->expanded_ = false;
	openCombo_ = nullptr;
}

void UIRoot::setModalBounds(const UiRect& bounds, UiAction::ViewCommand dismissCommand)
{
	modalBounds_ = bounds;
	hasModalBounds_ = true;
	dismissCommand_ = dismissCommand;
}

UiWidget* UIRoot::hitWidget(float x, float y) const
{
	for (std::vector<std::unique_ptr<UiWidget> >::const_reverse_iterator i = widgets_.rbegin(); i != widgets_.rend(); ++i)
		if (isWidgetVisible(i->get()) && (*i)->bounds_.contains(x, y))
			return i->get();
	return nullptr;
}

bool UIRoot::isWidgetVisible(const UiWidget* widget) const
{
	if (!widget || !widget->visible_)
		return false;
	for (const UiContainer* parent = widget->parent_; parent; parent = parent->parent_)
		if (!parent->visible_)
			return false;
	return true;
}

void UIRoot::focus(UiWidget* widget)
{
	if (focusedWidget_) focusedWidget_->focused_ = false;
	focusedWidget_ = widget;
	if (focusedWidget_) focusedWidget_->focused_ = true;
}

void UIRoot::focusNext(bool backwards)
{
	if (widgets_.empty()) return;
	int index = -1;
	for (std::size_t i = 0; i < widgets_.size(); ++i)
		if (widgets_[i].get() == focusedWidget_) index = static_cast<int>(i);
	for (std::size_t attempt = 0; attempt < widgets_.size(); ++attempt)
	{
		index = backwards ? (index <= 0 ? static_cast<int>(widgets_.size()) - 1 : index - 1)
			: (index + 1) % static_cast<int>(widgets_.size());
		UiWidget* candidate = widgets_[index].get();
		if (isWidgetVisible(candidate) && candidate->enabled_)
		{
			focus(candidate);
			closeCombo();
			return;
		}
	}
}

bool UIRoot::emit(UiWidget* widget, UiAction& action, bool selection)
{
	if (!widget) return false;
	AppAction appAction = widget->action_;
	if (selection)
		appAction.selection = widget->selectedIndex_;
	if (widget->kind_ == UiWidgetKind::Slider)
		appAction.value = widget->sliderValue_;
	action = UiAction(appAction, widget->viewCommand_);
	return true;
}

bool UIRoot::setSliderFromPointer(UiWidget* widget, float x, UiAction& action)
{
	if (!widget || widget->kind_ != UiWidgetKind::Slider || widget->bounds_.width <= 0.0f)
		return false;
	const float fraction = std::max(0.0f, std::min(1.0f, (x - widget->bounds_.x) / widget->bounds_.width));
	const float value = widget->sliderMinimum_ + fraction * (widget->sliderMaximum_ - widget->sliderMinimum_);
	const float previous = widget->sliderValue_;
	widget->setSliderValue(value);
	if (previous == widget->sliderValue_) return false;
	emit(widget, action, false);
	return true;
}

bool UIRoot::handleKey(const UiInputEvent& event, UiAction& action)
{
	if (event.key == UiKey::Tab)
	{
		focusNext(event.shift);
		return true;
	}
	if (event.key == UiKey::Escape)
	{
		if (openCombo_) { closeCombo(); return true; }
		if (inputCaptured_ && dismissCommand_ != UiAction::ViewCommand::None)
		{
			action = UiAction(AppAction(), dismissCommand_);
			return true;
		}
		return inputCaptured_;
	}
	if (openCombo_ && (event.key == UiKey::Up || event.key == UiKey::Down || event.key == UiKey::Left || event.key == UiKey::Right))
	{
		const int count = static_cast<int>(openCombo_->options_.size());
		if (count == 0) return true;
		const int delta = (event.key == UiKey::Up || event.key == UiKey::Left) ? -1 : 1;
		int selected = openCombo_->selectedIndex_;
		for (int attempt = 0; attempt < count; ++attempt)
		{
			selected = (selected + delta + count) % count;
			if (openCombo_->optionEnabled(static_cast<std::size_t>(selected))) break;
		}
		if (!openCombo_->optionEnabled(static_cast<std::size_t>(selected))) return true;
		if (selected != openCombo_->selectedIndex_)
		{
			openCombo_->setSelectedIndex(selected);
			emit(openCombo_, action, true);
		}
		return true;
	}
	if (!openCombo_ && focusedWidget_ && focusedWidget_->kind_ == UiWidgetKind::ComboBox &&
		(event.key == UiKey::Up || event.key == UiKey::Down || event.key == UiKey::Left || event.key == UiKey::Right))
	{
		closeCombo();
		openCombo_ = focusedWidget_;
		focusedWidget_->expanded_ = true;
		const int count = static_cast<int>(focusedWidget_->options_.size());
		if (count > 0)
		{
			const int delta = (event.key == UiKey::Up || event.key == UiKey::Left) ? -1 : 1;
			int selected = focusedWidget_->selectedIndex_;
			for (int attempt = 0; attempt < count; ++attempt)
			{
				selected = (selected + delta + count) % count;
				if (focusedWidget_->optionEnabled(static_cast<std::size_t>(selected))) break;
			}
			if (!focusedWidget_->optionEnabled(static_cast<std::size_t>(selected))) return true;
			if (selected != focusedWidget_->selectedIndex_)
			{
				focusedWidget_->setSelectedIndex(selected);
				emit(focusedWidget_, action, true);
			}
		}
		return true;
	}
	if (focusedWidget_ && focusedWidget_->kind_ == UiWidgetKind::Slider &&
		(event.key == UiKey::Up || event.key == UiKey::Down || event.key == UiKey::Left || event.key == UiKey::Right))
	{
		const bool increase = event.key == UiKey::Right || event.key == UiKey::Up;
		const float multiplier = event.shift ? 10.0f : 1.0f;
		const float previous = focusedWidget_->sliderValue_;
		focusedWidget_->setSliderValue(previous + (increase ? 1.0f : -1.0f) * focusedWidget_->sliderStep_ * multiplier);
		if (previous != focusedWidget_->sliderValue_) emit(focusedWidget_, action, false);
		return true;
	}
	if ((event.key == UiKey::Enter || event.key == UiKey::Space) && focusedWidget_ && focusedWidget_->visible_ && focusedWidget_->enabled_)
	{
		if (focusedWidget_->kind_ == UiWidgetKind::ComboBox)
		{
			if (openCombo_ == focusedWidget_) closeCombo();
			else { closeCombo(); openCombo_ = focusedWidget_; focusedWidget_->expanded_ = true; }
			return true;
		}
		if (focusedWidget_->kind_ == UiWidgetKind::Slider) return true;
		return emit(focusedWidget_, action, false);
	}
	if (focusedWidget_ && (event.key == UiKey::Up || event.key == UiKey::Down))
	{
		focusNext(event.key == UiKey::Up);
		return true;
	}
	return inputCaptured_;
}
