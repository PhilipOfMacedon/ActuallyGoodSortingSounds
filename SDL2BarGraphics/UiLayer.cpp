#include "UiLayer.h"

#include <algorithm>
#include <cmath>

UiScrollViewport::UiScrollViewport()
	: viewport_(), contentExtent_(0.0f), rowExtent_(1.0f), offset_(0.0f)
{
}

void UiScrollViewport::configure(const UiRect& viewport, float contentExtent, float rowExtent, float requestedOffset)
{
	viewport_ = viewport;
	contentExtent_ = std::max(0.0f, contentExtent);
	rowExtent_ = std::max(1.0f, rowExtent);
	setOffset(requestedOffset);
}

float UiScrollViewport::maximumOffset() const
{
	return std::max(0.0f, contentExtent_ - std::max(0.0f, viewport_.height));
}

void UiScrollViewport::setOffset(float value)
{
	offset_ = std::max(0.0f, std::min(maximumOffset(), value));
}

bool UiScrollViewport::scrollWheel(float x, float y, int wheelDelta, float rowsPerWheel)
{
	if (!viewport_.contains(x, y)) return false;
	if (wheelDelta != 0)
		setOffset(offset_ - static_cast<float>(wheelDelta) * rowExtent_ * std::max(0.0f, rowsPerWheel));
	return true;
}

void UiScrollViewport::ensureItemVisible(std::size_t index, float itemExtent)
{
	const float extent = itemExtent > 0.0f ? itemExtent : rowExtent_;
	const float top = static_cast<float>(index) * extent;
	const float bottom = top + extent;
	if (top < offset_) setOffset(top);
	else if (bottom > offset_ + viewport_.height) setOffset(bottom - viewport_.height);
}

std::size_t UiScrollViewport::firstVisibleItem(float itemExtent) const
{
	const float extent = itemExtent > 0.0f ? itemExtent : rowExtent_;
	return static_cast<std::size_t>(std::floor(offset_ / extent));
}

std::size_t UiScrollViewport::visibleItemCount(std::size_t itemCount, float itemExtent) const
{
	const float extent = itemExtent > 0.0f ? itemExtent : rowExtent_;
	if (itemCount == 0 || viewport_.height <= 0.0f) return 0;
	const std::size_t first = std::min(itemCount, firstVisibleItem(extent));
	const std::size_t count = static_cast<std::size_t>(std::ceil(viewport_.height / extent)) + 1;
	return std::min(count, itemCount - first);
}

int UiScrollViewport::itemAt(float x, float y, std::size_t itemCount, float itemExtent) const
{
	if (!viewport_.contains(x, y)) return -1;
	const float extent = itemExtent > 0.0f ? itemExtent : rowExtent_;
	const std::size_t index = static_cast<std::size_t>(std::floor((y - viewport_.y + offset_) / extent));
	return index < itemCount ? static_cast<int>(index) : -1;
}

UiRect UiScrollViewport::itemBounds(std::size_t index, float itemExtent) const
{
	const float extent = itemExtent > 0.0f ? itemExtent : rowExtent_;
	return UiRect(viewport_.x, viewport_.y + static_cast<float>(index) * extent - offset_, viewport_.width, extent);
}

bool UiScrollViewport::itemIntersectsViewport(std::size_t index, float itemExtent) const
{
	const UiRect bounds = itemBounds(index, itemExtent);
	return bounds.y < viewport_.y + viewport_.height && bounds.y + bounds.height > viewport_.y;
}

UiWidget::UiWidget(UiWidgetKind kind, const std::string& id, const std::string& label,
	const AppAction& action, UiAction::ViewCommand viewCommand)
	: kind_(kind), presentation_(UiWidgetPresentation::Standard), role_(UiControlRole::Standard), id_(id), label_(label), bounds_(), popupBounds_(), enabled_(true), visible_(true),
		hovered_(false), pressed_(false), focused_(false), expanded_(false), selectedIndex_(0), options_(), optionEnabled_(),
		action_(action), viewCommand_(viewCommand), parent_(nullptr), sliderMinimum_(0.0f), sliderMaximum_(1.0f),
		sliderStep_(0.01f), sliderValue_(0.0f), popupAbove_(false), scrollbarArrowSize_(14.0f), interactiveSubregions_(),
		optionViewport_(), optionRowExtent_(1.0f)
{
}

void UiWidget::setBounds(const UiRect& value)
{
	const bool firstLayout = bounds_.height <= 0.0f && value.height > 0.0f;
	const bool changed = bounds_.x != value.x || bounds_.y != value.y ||
		bounds_.width != value.width || bounds_.height != value.height;
	bounds_ = value;
	popupBounds_ = UiRect();
	configureOptionViewport();
	if (!options_.empty() && (firstLayout || (changed && expanded_)))
		optionViewport_.ensureItemVisible(static_cast<std::size_t>(selectedIndex_), optionRowExtent_);
}

void UiWidget::setPopupBounds(const UiRect& value)
{
	popupBounds_ = value;
	configureOptionViewport();
}

void UiWidget::setPopupAbove(bool value)
{
	popupAbove_ = value;
	if (popupBounds_.width <= 0.0f || popupBounds_.height <= 0.0f)
		configureOptionViewport();
}

void UiWidget::configureOptionViewport(float requestedOffset)
{
	const std::size_t rows = std::min<std::size_t>(options_.size(), 8);
	const float fallbackRow = std::max(1.0f, bounds_.height);
	const float viewportHeight = popupBounds_.height > 0.0f
		? popupBounds_.height : fallbackRow * static_cast<float>(rows);
	optionRowExtent_ = rows > 0 && popupBounds_.height > 0.0f
		? std::max(1.0f, popupBounds_.height / static_cast<float>(rows)) : fallbackRow;
	const UiRect viewport = popupBounds_.width > 0.0f && popupBounds_.height > 0.0f
		? popupBounds_
		: UiRect(bounds_.x, popupAbove_ ? bounds_.y - viewportHeight : bounds_.y + bounds_.height,
			bounds_.width, viewportHeight);
	const float offset = requestedOffset >= 0.0f ? requestedOffset : optionViewport_.offset();
	optionViewport_.configure(viewport, optionRowExtent_ * static_cast<float>(options_.size()), optionRowExtent_, offset);
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
	: widgets_(), focusOrder_(), rootContainer_("root", UiContainerKind::Overlay, nullptr), containers_(), containerLookup_(),
		openCombo_(nullptr), pressedWidget_(nullptr), focusedWidget_(nullptr), inputCaptured_(false),
		hasModalBounds_(false), modalBounds_(), dismissCommand_(UiAction::ViewCommand::None),
		pointerShift_(false), pointerControl_(false), pointerAlt_(false)
{
	containerLookup_["root"] = &rootContainer_;
}

void UiWidget::setOptions(const std::vector<std::string>& values)
{
	options_ = values;
	optionEnabled_.assign(values.size(), true);
	configureOptionViewport(0.0f);
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
	{
		label_ = options_[selectedIndex_];
		optionViewport_.ensureItemVisible(static_cast<std::size_t>(selectedIndex_), optionRowExtent_);
	}
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
	return optionViewport_.itemBounds(index, optionRowExtent_);
}

std::size_t UiWidget::firstVisibleOption() const
{
	return std::min(options_.size(), optionViewport_.firstVisibleItem(optionRowExtent_));
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
	if (event.type == UiInputType::PointerDown)
	{
		pointerShift_ = event.shift;
		pointerControl_ = event.control;
		pointerAlt_ = event.alt;
	}
	if (event.type == UiInputType::FocusLost)
	{
		closeCombo();
		if (pressedWidget_) pressedWidget_->pressed_ = false;
		pressedWidget_ = nullptr;
		pointerShift_ = pointerControl_ = pointerAlt_ = false;
		focus(nullptr);
		action.event.kind = UiEventKind::FocusLost;
		return true;
	}
	if (event.type == UiInputType::TextInput)
	{
		if (!focusedWidget_ || !isWidgetVisible(focusedWidget_)) return inputCaptured_;
		action.event.controlId = focusedWidget_->id_;
		action.event.kind = UiEventKind::TextEntered;
		action.event.text = event.text;
		return true;
	}
	if (event.type == UiInputType::Wheel)
	{
		if (openCombo_ && openCombo_->optionViewport_.scrollWheel(event.x, event.y, event.wheelDelta))
		{
			action.event.controlId = openCombo_->id_;
			action.event.kind = UiEventKind::Wheel;
			action.event.value = static_cast<float>(event.wheelDelta);
			return true;
		}
		UiWidget* target = hitWidget(event.x, event.y);
		if (!target) return inputCaptured_;
		action.event.controlId = target->id_;
		action.event.kind = UiEventKind::Wheel;
		action.event.value = static_cast<float>(event.wheelDelta);
		return true;
	}
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
					if (changed) emit(combo, action, true, &event);
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
			if (widget->presentation_ == UiWidgetPresentation::RadioGroup)
			{
				if (widget->bounds_.width > 0.0f && !widget->options_.empty())
				{
					int selected = -1;
					for (std::size_t i = 0; i < widget->interactiveSubregions_.size(); ++i)
						if (widget->interactiveSubregions_[i].contains(event.x, event.y)) selected = static_cast<int>(i);
					if (selected < 0)
					{
						const float relative = (event.x - widget->bounds_.x) / widget->bounds_.width;
						selected = std::max(0, std::min(static_cast<int>(widget->options_.size()) - 1,
							static_cast<int>(relative * widget->options_.size())));
					}
					if (widget->optionEnabled(static_cast<std::size_t>(selected)) && selected != widget->selectedIndex_)
					{
						widget->setSelectedIndex(selected);
						emit(widget, action, true, &event);
					}
				}
				return true;
			}
			if (openCombo_ == widget) closeCombo();
			else { closeCombo(); openCombo_ = widget; widget->expanded_ = true; }
			return true;
		}
		widget->pressed_ = true;
		pressedWidget_ = widget;
		if (widget->kind_ == UiWidgetKind::Slider)
		{
			if (widget->presentation_ == UiWidgetPresentation::DosScrollbar)
			{
				const std::vector<UiRect>& regions = widget->interactiveSubregions_;
				if (regions.size() >= 3 && (regions[0].contains(event.x, event.y) || regions[2].contains(event.x, event.y)))
				{
					const bool increase = regions[2].contains(event.x, event.y);
					const float previous = widget->sliderValue_;
					widget->setSliderValue(previous + (increase ? widget->sliderStep_ : -widget->sliderStep_));
					if (previous != widget->sliderValue_) emit(widget, action, false, &event);
					widget->pressed_ = false;
					pressedWidget_ = nullptr;
					return true;
				}
				if (regions.size() >= 3 && !regions[1].contains(event.x, event.y))
				{
					widget->pressed_ = false;
					pressedWidget_ = nullptr;
					return true;
				}
			}
			setSliderFromPointer(widget, event.x, action);
		}
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
		{
			const bool emitted = emit(pressed, action, false);
			pointerShift_ = pointerControl_ = pointerAlt_ = false;
			return emitted;
		}
		pointerShift_ = pointerControl_ = pointerAlt_ = false;
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
	std::vector<UiWidget*> order;
	if (!focusOrder_.empty())
	{
		for (std::size_t i = 0; i < focusOrder_.size(); ++i)
		{
			UiWidget* widget = find(focusOrder_[i]);
			if (widget && std::find(order.begin(), order.end(), widget) == order.end()) order.push_back(widget);
		}
		for (std::size_t i = 0; i < widgets_.size(); ++i)
			if (std::find(order.begin(), order.end(), widgets_[i].get()) == order.end()) order.push_back(widgets_[i].get());
	}
	else
		for (std::size_t i = 0; i < widgets_.size(); ++i) order.push_back(widgets_[i].get());
	if (order.empty()) return;
	int index = -1;
	for (std::size_t i = 0; i < order.size(); ++i)
		if (order[i] == focusedWidget_) index = static_cast<int>(i);
	for (std::size_t attempt = 0; attempt < order.size(); ++attempt)
	{
		index = backwards ? (index <= 0 ? static_cast<int>(order.size()) - 1 : index - 1)
			: (index + 1) % static_cast<int>(order.size());
		UiWidget* candidate = order[static_cast<std::size_t>(index)];
		if (isWidgetVisible(candidate) && candidate->enabled_)
		{
			focus(candidate);
			closeCombo();
			return;
		}
	}
}

bool UIRoot::emit(UiWidget* widget, UiAction& action, bool selection, const UiInputEvent* event)
{
	if (!widget) return false;
	AppAction appAction = widget->action_;
	if (selection)
		appAction.selection = widget->selectedIndex_;
	if (widget->kind_ == UiWidgetKind::Slider)
		appAction.value = widget->sliderValue_;
	action = UiAction(appAction, widget->viewCommand_);
	action.event.controlId = widget->id_;
	action.event.kind = selection ? UiEventKind::SelectionChanged :
		widget->kind_ == UiWidgetKind::Slider ? UiEventKind::ValueChanged : UiEventKind::Activated;
	action.event.selection = widget->selectedIndex_;
	action.event.value = widget->kind_ == UiWidgetKind::Slider ? widget->sliderValue_ : static_cast<float>(widget->selectedIndex_);
	action.event.shift = event ? event->shift : pointerShift_;
	action.event.control = event ? event->control : pointerControl_;
	action.event.alt = event ? event->alt : pointerAlt_;
	return true;
}

bool UIRoot::setSliderFromPointer(UiWidget* widget, float x, UiAction& action)
{
	if (!widget || widget->kind_ != UiWidgetKind::Slider || widget->bounds_.width <= 0.0f)
		return false;
	float start = widget->bounds_.x;
	float width = widget->bounds_.width;
	if (widget->presentation_ == UiWidgetPresentation::DosScrollbar)
	{
		if (widget->interactiveSubregions_.size() >= 3)
		{
			start = widget->interactiveSubregions_[1].x;
			width = std::max(1.0f, widget->interactiveSubregions_[1].width);
		}
		else
		{
			const float arrow = std::min(widget->scrollbarArrowSize_, widget->bounds_.width * 0.25f);
			start += arrow;
			width = std::max(1.0f, width - 2.0f * arrow);
		}
	}
	const float fraction = std::max(0.0f, std::min(1.0f, (x - start) / width));
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
			emit(openCombo_, action, true, &event);
		}
		return true;
	}
	if (!openCombo_ && focusedWidget_ && focusedWidget_->kind_ == UiWidgetKind::ComboBox &&
		(event.key == UiKey::Up || event.key == UiKey::Down || event.key == UiKey::Left || event.key == UiKey::Right))
	{
		if (focusedWidget_->presentation_ == UiWidgetPresentation::RadioGroup)
		{
			const int count = static_cast<int>(focusedWidget_->options_.size());
			if (count <= 0) return true;
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
				emit(focusedWidget_, action, true, &event);
			}
			return true;
		}
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
				emit(focusedWidget_, action, true, &event);
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
		if (previous != focusedWidget_->sliderValue_) emit(focusedWidget_, action, false, &event);
		return true;
	}
	// Modifier chords belong to the application command layer. In particular,
	// Alt+Enter must not silently activate whichever control happens to have focus.
	if ((event.key == UiKey::Enter || event.key == UiKey::Space) && (event.alt || event.control))
		return false;
	if ((event.key == UiKey::Enter || event.key == UiKey::Space) && focusedWidget_ && focusedWidget_->visible_ && focusedWidget_->enabled_)
	{
		if (focusedWidget_->kind_ == UiWidgetKind::ComboBox)
		{
			if (openCombo_ == focusedWidget_) closeCombo();
			else { closeCombo(); openCombo_ = focusedWidget_; focusedWidget_->expanded_ = true; }
			return true;
		}
		if (focusedWidget_->kind_ == UiWidgetKind::Slider) return true;
		return emit(focusedWidget_, action, false, &event);
	}
	if (focusedWidget_ && (event.key == UiKey::Up || event.key == UiKey::Down))
	{
		focusNext(event.key == UiKey::Up);
		return true;
	}
	return inputCaptured_;
}
