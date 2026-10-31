#include "UiLayer.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

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

UiScrollbar::UiScrollbar() : viewport_(nullptr), bounds_(), dragging_(false), dragOffset_(0.0f) {}

void UiScrollbar::bind(UiScrollViewport* viewport)
{
	if (viewport_ == viewport) return;
	viewport_ = viewport;
	dragging_ = false;
}

void UiScrollbar::setBounds(const UiRect& bounds)
{
	bounds_ = bounds;
	if (bounds_.height <= 0.0f) dragging_ = false;
}

bool UiScrollbar::visible() const
{
	return viewport_ && viewport_->maximumOffset() > 0.0f && bounds_.width > 0.0f && bounds_.height > 0.0f;
}

float UiScrollbar::thumbLength() const
{
	if (!visible()) return 0.0f;
	const float extent = viewport_->contentExtent();
	const float viewportHeight = viewport_->viewport().height;
	if (extent <= 0.0f) return bounds_.height;
	const float proportional = bounds_.height * viewportHeight / extent;
	return std::min(bounds_.height, std::max(18.0f, proportional));
}

float UiScrollbar::travel() const { return std::max(0.0f, bounds_.height - thumbLength()); }

UiRect UiScrollbar::thumbBounds() const
{
	if (!visible()) return UiRect();
	const float trackTravel = travel();
	const float ratio = viewport_->maximumOffset() > 0.0f ? viewport_->offset() / viewport_->maximumOffset() : 0.0f;
	return UiRect(bounds_.x, bounds_.y + trackTravel * ratio, bounds_.width, thumbLength());
}

bool UiScrollbar::pointerDown(float x, float y)
{
	if (!visible() || !bounds_.contains(x, y)) return false;
	const UiRect thumb = thumbBounds();
	if (thumb.contains(x, y))
	{
		dragging_ = true;
		dragOffset_ = y - thumb.y;
	}
	else
	{
		page(x, y);
		// Track clicks page toward the pointer and keep the pointer captured so
		// scrollbar users can continue directly into a drag without a second
	// click. Centering the thumb under the pointer avoids a jump on movement.
		dragging_ = true;
		dragOffset_ = thumbLength() * 0.5f;
	}
	return true;
}

bool UiScrollbar::pointerMove(float, float y)
{
	if (!dragging_ || !viewport_) return false;
	const float trackTravel = travel();
	if (trackTravel <= 0.0f) { viewport_->setOffset(0.0f); return true; }
	const float top = std::max(bounds_.y, std::min(bounds_.y + trackTravel, y - dragOffset_));
	viewport_->setOffset((top - bounds_.y) / trackTravel * viewport_->maximumOffset());
	return true;
}

bool UiScrollbar::pointerUp(float x, float y)
{
	if (!dragging_) return false;
	pointerMove(x, y);
	dragging_ = false;
	return true;
}

bool UiScrollbar::page(float, float y)
{
	if (!visible() || !bounds_.contains(bounds_.x, y)) return false;
	const float amount = std::max(viewport_->viewport().height * 0.85f, viewport_->rowExtent());
	viewport_->setOffset(viewport_->offset() + (y < thumbBounds().y ? -amount : amount));
	return true;
}

bool UiScrollbar::step(int direction)
{
	if (!visible() || direction == 0) return false;
	const float amount = std::max(1.0f, viewport_->rowExtent());
	viewport_->setOffset(viewport_->offset() + (direction < 0 ? -amount : amount));
	return true;
}

UiFormLayout::UiFormLayout(const UiRect& bounds, float padding, float rowGap, float labelFontSize, float lineHeight)
	: bounds_(bounds), padding_(std::max(0.0f, padding)), rowGap_(std::max(0.0f, rowGap)),
	labelFontSize_(std::max(1.0f, labelFontSize)), lineHeight_(std::max(1.0f, lineHeight)),
	cursorY_(bounds.y + std::max(0.0f, padding))
{
}

float UiFormLayout::measureTextHeight(const std::string& text, float fontSize) const
{
	const float size = std::max(1.0f, fontSize);
	const float usableWidth = std::max(1.0f, bounds_.width - 2.0f * padding_);
	const float glyphWidth = size * 0.62f;
	const std::size_t charsPerLine = std::max<std::size_t>(1,
		static_cast<std::size_t>(std::floor(usableWidth / glyphWidth)));
	std::size_t lines = 0;
	std::size_t currentLine = 0;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\n') { ++lines; currentLine = 0; continue; }
		if (currentLine >= charsPerLine) { ++lines; currentLine = 0; }
		++currentLine;
	}
	if (currentLine > 0 || text.empty()) ++lines;
	return static_cast<float>(lines) * size * lineHeight_;
}

UiRect UiFormLayout::addTextBlock(const std::string& text, float fontSize, float minimumHeight)
{
	const float height = std::max(std::max(0.0f, minimumHeight), measureTextHeight(text, fontSize));
	UiRect result(bounds_.x + padding_, cursorY_, std::max(0.0f, bounds_.width - padding_ * 2.0f), height);
	cursorY_ += height + rowGap_;
	return result;
}

UiLabeledControlBounds UiFormLayout::addLabeledControl(const std::string& text, float controlHeight,
	float fontSize, float labelGap)
{
	UiLabeledControlBounds result;
	const float labelHeight = std::max(labelFontSize_ * lineHeight_, measureTextHeight(text, fontSize));
	const float x = bounds_.x + padding_;
	const float width = std::max(0.0f, bounds_.width - padding_ * 2.0f);
	result.label = UiRect(x, cursorY_, width, labelHeight);
	result.control = UiRect(x, cursorY_ + labelHeight + std::max(0.0f, labelGap), width, std::max(0.0f, controlHeight));
	cursorY_ = result.control.y + result.control.height + rowGap_;
	return result;
}

UiRect UiFormLayout::remainingBounds() const
{
	return UiRect(bounds_.x + padding_, cursorY_, std::max(0.0f, bounds_.width - padding_ * 2.0f),
		std::max(0.0f, bounds_.y + bounds_.height - padding_ - cursorY_));
}

UiWidget::UiWidget(UiWidgetKind kind, const std::string& id, const std::string& label,
	const AppAction& action, UiAction::ViewCommand viewCommand)
	: kind_(kind), presentation_(UiWidgetPresentation::Standard), role_(UiControlRole::Standard), id_(id), label_(label), bounds_(), popupBounds_(), enabled_(true), visible_(true),
		hovered_(false), pressed_(false), focused_(false), expanded_(false), checked_(false), selectedIndex_(0), options_(), optionEnabled_(),
		action_(action), viewCommand_(viewCommand), parent_(nullptr), sliderMinimum_(0.0f), sliderMaximum_(1.0f),
		sliderStep_(0.01f), sliderValue_(0.0f), popupAbove_(false), scrollbarArrowSize_(14.0f), interactiveSubregions_(),
		optionViewport_(), optionScrollbar_(), optionRowExtent_(1.0f)
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
	optionRowExtent_ = fallbackRow;
	const UiRect popup = popupBounds_.width > 0.0f && popupBounds_.height > 0.0f
		? popupBounds_
		: UiRect(bounds_.x, popupAbove_ ? bounds_.y - viewportHeight : bounds_.y + bounds_.height,
			bounds_.width, viewportHeight);
	const bool overflow = optionRowExtent_ * static_cast<float>(options_.size()) > popup.height + 0.5f;
	const float scrollWidth = overflow ? std::min(12.0f, popup.width * 0.18f) : 0.0f;
	const UiRect viewport(popup.x, popup.y, std::max(0.0f, popup.width - scrollWidth), popup.height);
	const float offset = requestedOffset >= 0.0f ? requestedOffset : optionViewport_.offset();
	optionViewport_.configure(viewport, optionRowExtent_ * static_cast<float>(options_.size()), optionRowExtent_, offset);
	optionScrollbar_.bind(&optionViewport_);
	optionScrollbar_.setBounds(overflow ? UiRect(popup.x + popup.width - scrollWidth, popup.y, scrollWidth, popup.height) : UiRect());
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

UiTabStrip::UiTabStrip(const std::string& id, const std::vector<std::string>& tabs, int selection, const AppAction& action)
	: id_(id), tabs_(tabs), selectedIndex_(0), bounds_(), enabled_(true), visible_(true), focused_(false), action_(action)
{
	setSelectedIndex(selection);
}

void UiTabStrip::setTabs(const std::vector<std::string>& tabs)
{
	tabs_ = tabs;
	setSelectedIndex(selectedIndex_);
}

void UiTabStrip::setSelectedIndex(int index)
{
	selectedIndex_ = tabs_.empty() ? 0 : std::max(0, std::min(static_cast<int>(tabs_.size()) - 1, index));
}

UiRect UiTabStrip::tabBounds(std::size_t index) const
{
	if (index >= tabs_.size() || tabs_.empty()) return UiRect();
	const float width = bounds_.width / static_cast<float>(tabs_.size());
	return UiRect(bounds_.x + width * static_cast<float>(index), bounds_.y, width, bounds_.height);
}

UiSelectionList::UiSelectionList(const std::string& id, const AppAction& action)
	: id_(id), items_(), selectedIndex_(-1), bounds_(), rowHeight_(36.0f), enabled_(true), visible_(true), focused_(false),
	action_(action), viewport_()
{
}

void UiSelectionList::configure(float requestedOffset)
{
	const float offset = requestedOffset >= 0.0f ? requestedOffset : viewport_.offset();
	viewport_.configure(bounds_, rowHeight_ * static_cast<float>(items_.size()), rowHeight_, offset);
}

void UiSelectionList::setItems(const std::vector<std::string>& items)
{
	items_ = items;
	if (selectedIndex_ >= static_cast<int>(items_.size())) selectedIndex_ = items_.empty() ? -1 : static_cast<int>(items_.size()) - 1;
	configure();
}

void UiSelectionList::setSelectedIndex(int index, bool reveal)
{
	selectedIndex_ = items_.empty() ? -1 : std::max(0, std::min(static_cast<int>(items_.size()) - 1, index));
	if (reveal && selectedIndex_ >= 0) viewport_.ensureItemVisible(static_cast<std::size_t>(selectedIndex_), rowHeight_);
}

void UiSelectionList::setBounds(const UiRect& bounds)
{
	const bool changed = bounds_.x != bounds.x || bounds_.y != bounds.y || bounds_.width != bounds.width || bounds_.height != bounds.height;
	bounds_ = bounds;
	configure(changed ? viewport_.offset() : -1.0f);
}

void UiSelectionList::setRowHeight(float value)
{
	rowHeight_ = std::max(12.0f, value);
	configure();
}

void UiSelectionList::scroll(int wheelDelta)
{
	viewport_.scrollWheel(bounds_.x, bounds_.y, wheelDelta);
}

UiEditableField::UiEditableField(const std::string& id, const std::string& label, UiEditableFieldKind kind,
	const std::string& value, double minimum, double maximum, std::size_t maximumLength, const AppAction& action)
	: id_(id), label_(label), value_(value), draft_(value), bounds_(), kind_(kind), minimum_(minimum), maximum_(maximum),
	maximumLength_(maximumLength), enabled_(true), visible_(true), focused_(false), editing_(false), invalid_(false), action_(action)
{
}

void UiEditableField::setValue(const std::string& value)
{
	value_ = value;
	if (!editing_) draft_ = value;
	invalid_ = false;
}

void UiEditableField::setEnabled(bool value)
{
	enabled_ = value;
	if (!value) { cancel(); focused_ = false; }
}

void UiEditableField::setVisible(bool value)
{
	visible_ = value;
	if (!value) { cancel(); focused_ = false; }
}

bool UiEditableField::append(const std::string& text)
{
	if (!editing_ || !enabled_) return false;
	bool changed = false;
	for (std::size_t i = 0; i < text.size() && draft_.size() < maximumLength_; ++i)
	{
		const char value = text[i];
		const bool allowed = kind_ == UiEditableFieldKind::BoundedText
			? value >= 32 && value != 127
			: (value >= '0' && value <= '9') || (kind_ == UiEditableFieldKind::FiniteDecimal && (value == '.' || value == '-'));
		if (allowed) { draft_.push_back(value); changed = true; }
	}
	invalid_ = !valid(draft_);
	return changed;
}

void UiEditableField::erase()
{
	if (!editing_ || draft_.empty()) return;
	draft_.erase(draft_.size() - 1);
	invalid_ = !draft_.empty() && !valid(draft_);
}

void UiEditableField::cancel()
{
	draft_ = value_;
	editing_ = false;
	invalid_ = false;
}

bool UiEditableField::valid(const std::string& value) const
{
	if (kind_ == UiEditableFieldKind::BoundedText) return !value.empty() && value.size() <= maximumLength_;
	if (value.empty()) return false;
	if (kind_ == UiEditableFieldKind::UnsignedInteger)
		for (std::size_t i = 0; i < value.size(); ++i) if (value[i] < '0' || value[i] > '9') return false;
	errno = 0;
	char* end = nullptr;
	const double parsed = std::strtod(value.c_str(), &end);
	return errno != ERANGE && end && end != value.c_str() && *end == '\0' && std::isfinite(parsed) &&
		parsed >= minimum_ && parsed <= maximum_;
}

bool UiEditableField::commit(UiAction& action)
{
	if (!editing_) return false;
	if (!valid(draft_)) { invalid_ = true; return false; }
	const bool changed = value_ != draft_;
	value_ = draft_;
	editing_ = false;
	invalid_ = false;
	if (!changed) return true;
	AppAction appAction = action_;
	appAction.text = value_;
	action = UiAction(appAction);
	action.event.controlId = id_;
	action.event.kind = UiEventKind::TextEntered;
	action.event.text = value_;
	return true;
}

UIRoot::UIRoot()
	: widgets_(), tabStrips_(), selectionLists_(), editableFields_(), focusOrder_(), rootContainer_("root", UiContainerKind::Overlay, nullptr), containers_(), containerLookup_(),
		openCombo_(nullptr), pressedWidget_(nullptr), focusedWidget_(nullptr), focusedCustomId_(), inputCaptured_(false),
		hasModalBounds_(false), modalBounds_(), dismissCommand_(UiAction::ViewCommand::None),
		pointerShift_(false), pointerControl_(false), pointerAlt_(false)
{
	containerLookup_["root"] = &rootContainer_;
}

void UiWidget::setOptions(const std::vector<std::string>& values)
{
	if (options_ == values) return;
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
	const int previousIndex = selectedIndex_;
	if (options_.empty())
		selectedIndex_ = 0;
	else
		selectedIndex_ = std::max(0, std::min(static_cast<int>(options_.size()) - 1, value));
	if (kind_ == UiWidgetKind::ComboBox && !options_.empty())
	{
		label_ = options_[selectedIndex_];
		if (previousIndex != selectedIndex_)
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
	const std::size_t fittingRows = popupBounds_.height > 0.0f
		? static_cast<std::size_t>(std::ceil(popupBounds_.height / std::max(1.0f, optionRowExtent_))) : 8;
	return std::min<std::size_t>(options_.size(), std::min<std::size_t>(8, fittingRows));
}

void UIRoot::layoutComboPopups(const UiRect& viewport,
	const std::function<float(const std::string&)>& measureOptionWidth, float padding)
{
	for (std::size_t i = 0; i < widgets_.size(); ++i)
	{
		UiWidget& widget = *widgets_[i];
		if (widget.kind() != UiWidgetKind::ComboBox || !widget.visible() || widget.options().empty() ||
			widget.bounds().height <= 0.0f || widget.bounds().width <= 0.0f) continue;
		float width = widget.bounds().width;
		for (std::size_t option = 0; option < widget.options().size(); ++option)
			width = std::max(width, measureOptionWidth(widget.options()[option]) + padding * 2.0f);
		width = std::min(width, viewport.width);
		const float height = std::min(widget.bounds().height *
			static_cast<float>(std::min<std::size_t>(8, widget.options().size())), viewport.height);
		const float x = std::max(viewport.x, std::min(widget.bounds().x, viewport.x + viewport.width - width));
		const float below = viewport.y + viewport.height - widget.bounds().y - widget.bounds().height;
		const float above = widget.bounds().y - viewport.y;
		const bool useAbove = below < height && above > below;
		const float fittingHeight = std::min(height, std::max(0.0f, useAbove ? above : below));
		const float y = useAbove ? widget.bounds().y - fittingHeight : widget.bounds().y + widget.bounds().height;
		widget.setPopupAbove(useAbove);
		widget.setPopupBounds(UiRect(x, y, width, fittingHeight));
	}
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
	widgets_.back()->setRole(UiControlRole::Choice);
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

UiTabStrip& UIRoot::addTabStrip(const std::string& id, const std::vector<std::string>& tabs, int selection, const AppAction& action)
{
	tabStrips_.push_back(std::unique_ptr<UiTabStrip>(new UiTabStrip(id, tabs, selection, action)));
	return *tabStrips_.back();
}

UiSelectionList& UIRoot::addSelectionList(const std::string& id, const AppAction& action)
{
	selectionLists_.push_back(std::unique_ptr<UiSelectionList>(new UiSelectionList(id, action)));
	return *selectionLists_.back();
}

UiEditableField& UIRoot::addEditableField(const std::string& id, const std::string& label, UiEditableFieldKind kind,
	const std::string& value, double minimum, double maximum, std::size_t maximumLength, const AppAction& action)
{
	editableFields_.push_back(std::unique_ptr<UiEditableField>(new UiEditableField(id, label, kind, value,
		minimum, maximum, maximumLength, action)));
	return *editableFields_.back();
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

UiTabStrip* UIRoot::findTabStrip(const std::string& id)
{
	for (std::size_t i = 0; i < tabStrips_.size(); ++i) if (tabStrips_[i]->id() == id) return tabStrips_[i].get();
	return nullptr;
}

const UiTabStrip* UIRoot::findTabStrip(const std::string& id) const
{
	for (std::size_t i = 0; i < tabStrips_.size(); ++i) if (tabStrips_[i]->id() == id) return tabStrips_[i].get();
	return nullptr;
}

UiSelectionList* UIRoot::findSelectionList(const std::string& id)
{
	for (std::size_t i = 0; i < selectionLists_.size(); ++i) if (selectionLists_[i]->id() == id) return selectionLists_[i].get();
	return nullptr;
}

const UiSelectionList* UIRoot::findSelectionList(const std::string& id) const
{
	for (std::size_t i = 0; i < selectionLists_.size(); ++i) if (selectionLists_[i]->id() == id) return selectionLists_[i].get();
	return nullptr;
}

UiEditableField* UIRoot::findEditableField(const std::string& id)
{
	for (std::size_t i = 0; i < editableFields_.size(); ++i) if (editableFields_[i]->id() == id) return editableFields_[i].get();
	return nullptr;
}

const UiEditableField* UIRoot::findEditableField(const std::string& id) const
{
	for (std::size_t i = 0; i < editableFields_.size(); ++i) if (editableFields_[i]->id() == id) return editableFields_[i].get();
	return nullptr;
}

void UIRoot::setWidgetEnabled(const std::string& id, bool enabled)
{
	UiWidget* widget = find(id);
	if (widget)
	{
		widget->setEnabled(enabled);
		if (!enabled && openCombo_ == widget) closeCombo();
		if (!enabled && focusedWidget_ == widget) { focus(nullptr); focusNext(false); }
		return;
	}
	if (UiTabStrip* tab = findTabStrip(id))
	{
		tab->setEnabled(enabled);
		if (!enabled && focusedCustomId_ == id) focusCustom(std::string());
		return;
	}
	if (UiSelectionList* list = findSelectionList(id))
	{
		list->setEnabled(enabled);
		if (!enabled && focusedCustomId_ == id) focusCustom(std::string());
		return;
	}
	if (UiEditableField* field = findEditableField(id))
	{
		field->setEnabled(enabled);
		if (!enabled && focusedCustomId_ == id) focusCustom(std::string());
	}
}

void UIRoot::setWidgetVisible(const std::string& id, bool visible)
{
	UiWidget* widget = find(id);
	if (widget)
	{
		if (widget->visible() == visible) return;
		widget->setVisible(visible);
		if (!visible && openCombo_ == widget) closeCombo();
		if (!visible && focusedWidget_ == widget) { focus(nullptr); focusNext(false); }
		return;
	}
	if (UiTabStrip* tab = findTabStrip(id))
	{
		tab->setVisible(visible);
		if (!visible && focusedCustomId_ == id) focusCustom(std::string());
		return;
	}
	if (UiSelectionList* list = findSelectionList(id))
	{
		list->setVisible(visible);
		if (!visible && focusedCustomId_ == id) focusCustom(std::string());
		return;
	}
	if (UiEditableField* field = findEditableField(id))
	{
		field->setVisible(visible);
		if (!visible && focusedCustomId_ == id) focusCustom(std::string());
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
		if (!commitFocusedField(action)) return true;
		const bool emittedCommit = action.event.kind != UiEventKind::None;
		if (pressedWidget_) pressedWidget_->pressed_ = false;
		pressedWidget_ = nullptr;
		pointerShift_ = pointerControl_ = pointerAlt_ = false;
		focus(nullptr);
		focusCustom(std::string());
		if (emittedCommit) return true;
		action.event.kind = UiEventKind::FocusLost;
		return true;
	}
	if (event.type == UiInputType::TextInput)
	{
		UiEditableField* field = findEditableField(focusedCustomId_);
		if (field && field->visible() && field->enabled())
		{
			if (!field->editing_) { field->editing_ = true; field->draft_ = field->value_; }
			field->append(event.text);
			return true;
		}
		if (!focusedWidget_ || !isWidgetVisible(focusedWidget_)) return inputCaptured_;
		action.event.controlId = focusedWidget_->id_;
		action.event.kind = UiEventKind::TextEntered;
		action.event.text = event.text;
		return true;
	}
	if (event.type == UiInputType::Wheel)
	{
		if (openCombo_)
		{
			if (openCombo_->optionViewport_.viewport().contains(event.x, event.y) ||
				openCombo_->optionScrollbar_.bounds().contains(event.x, event.y))
			{
				openCombo_->optionViewport_.scrollWheel(event.x, event.y, event.wheelDelta);
				return true;
			}
		}
		UiSelectionList* list = hitSelectionList(event.x, event.y);
		if (list && list->enabled())
		{
			list->viewport_.scrollWheel(event.x, event.y, event.wheelDelta);
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
		if (openCombo_ && openCombo_->optionScrollbar_.captured())
		{
			openCombo_->optionScrollbar_.pointerMove(event.x, event.y);
			return true;
		}
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
			if (!commitFocusedField(action)) return true;
			if (action.event.kind != UiEventKind::None) return true;
			closeCombo();
			action = UiAction(AppAction(), dismissCommand_);
			return true;
		}
		UiTabStrip* tab = hitTabStrip(event.x, event.y);
		UiSelectionList* list = hitSelectionList(event.x, event.y);
		UiEditableField* field = hitEditableField(event.x, event.y);
		if (tab || list || field)
		{
			if (!commitFocusedField(action)) return true;
			if (action.event.kind != UiEventKind::None) return true;
			closeCombo();
			if (tab)
			{
				focusCustom(tab->id_);
				if (!tab->enabled_) return true;
				for (std::size_t i = 0; i < tab->tabs_.size(); ++i)
					if (tab->tabBounds(i).contains(event.x, event.y))
					{
						if (tab->selectedIndex_ != static_cast<int>(i))
						{
							tab->setSelectedIndex(static_cast<int>(i));
							action = UiAction(tab->action_);
							action.appAction.selection = tab->selectedIndex_;
							action.event.controlId = tab->id_;
							action.event.kind = UiEventKind::SelectionChanged;
							action.event.selection = tab->selectedIndex_;
						}
						break;
					}
				return true;
			}
			if (list)
			{
				focusCustom(list->id_);
				if (!list->enabled_) return true;
				const int selected = list->viewport_.itemAt(event.x, event.y, list->items_.size(), list->rowHeight_);
				if (selected >= 0 && selected != list->selectedIndex_)
				{
					list->setSelectedIndex(selected, false);
					action = UiAction(list->action_);
					action.appAction.selection = selected;
					action.event.controlId = list->id_;
					action.event.kind = UiEventKind::SelectionChanged;
					action.event.selection = selected;
				}
				return true;
			}
			focusCustom(field->id_);
			if (field->enabled_)
			{
				field->editing_ = true;
				field->draft_ = field->value_;
				field->invalid_ = false;
			}
			return true;
		}
		if (!commitFocusedField(action)) return true;
		if (action.event.kind != UiEventKind::None) return true;
		if (openCombo_)
		{
			if (openCombo_->optionScrollbar_.pointerDown(event.x, event.y)) return true;
			for (std::size_t option = 0; option < openCombo_->options_.size(); ++option)
			{
				if (openCombo_->optionViewport_.viewport().contains(event.x, event.y) &&
					openCombo_->optionEnabled(option) && openCombo_->optionBounds(option).contains(event.x, event.y))
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
		if (openCombo_ && openCombo_->optionScrollbar_.captured())
		{
			openCombo_->optionScrollbar_.pointerUp(event.x, event.y);
			return true;
		}
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
	{
		openCombo_->optionScrollbar_.cancelCapture();
		openCombo_->expanded_ = false;
	}
	openCombo_ = nullptr;
}

void UIRoot::setModalBounds(const UiRect& bounds, UiAction::ViewCommand dismissCommand)
{
	modalBounds_ = bounds;
	hasModalBounds_ = true;
	dismissCommand_ = dismissCommand;
}

void UIRoot::setInputCaptured(bool value)
{
	// The frame coordinator refreshes overlay capture every frame. Reapplying
	// the same state must not cancel a mouse press, close a popup, or erase focus;
	// otherwise a normal down/up pair spanning frames never activates its widget.
	if (inputCaptured_ == value) return;
	inputCaptured_ = value;
	if (value) return;
	hasModalBounds_ = false;
	closeCombo();
	if (pressedWidget_) pressedWidget_->pressed_ = false;
	pressedWidget_ = nullptr;
	focus(nullptr);
}

UiWidget* UIRoot::hitWidget(float x, float y) const
{
	for (std::vector<std::unique_ptr<UiWidget> >::const_reverse_iterator i = widgets_.rbegin(); i != widgets_.rend(); ++i)
		if (isWidgetVisible(i->get()) && (*i)->bounds_.contains(x, y))
			return i->get();
	return nullptr;
}

UiTabStrip* UIRoot::hitTabStrip(float x, float y) const
{
	for (std::vector<std::unique_ptr<UiTabStrip> >::const_reverse_iterator i = tabStrips_.rbegin(); i != tabStrips_.rend(); ++i)
		if ((*i)->visible_ && (*i)->bounds_.contains(x, y)) return i->get();
	return nullptr;
}

UiSelectionList* UIRoot::hitSelectionList(float x, float y) const
{
	for (std::vector<std::unique_ptr<UiSelectionList> >::const_reverse_iterator i = selectionLists_.rbegin(); i != selectionLists_.rend(); ++i)
		if ((*i)->visible_ && (*i)->bounds_.contains(x, y)) return i->get();
	return nullptr;
}

UiEditableField* UIRoot::hitEditableField(float x, float y) const
{
	for (std::vector<std::unique_ptr<UiEditableField> >::const_reverse_iterator i = editableFields_.rbegin(); i != editableFields_.rend(); ++i)
		if ((*i)->visible_ && (*i)->bounds_.contains(x, y)) return i->get();
	return nullptr;
}

bool UIRoot::commitFocusedField(UiAction& action)
{
	action = UiAction();
	UiEditableField* field = findEditableField(focusedCustomId_);
	if (!field || !field->editing_) return true;
	return field->commit(action);
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
	focusedCustomId_.clear();
	for (std::size_t i = 0; i < tabStrips_.size(); ++i) tabStrips_[i]->focused_ = false;
	for (std::size_t i = 0; i < selectionLists_.size(); ++i) selectionLists_[i]->focused_ = false;
	for (std::size_t i = 0; i < editableFields_.size(); ++i) editableFields_[i]->focused_ = false;
	focusedWidget_ = widget;
	if (focusedWidget_) focusedWidget_->focused_ = true;
}

void UIRoot::focusCustom(const std::string& id)
{
	if (focusedWidget_) focusedWidget_->focused_ = false;
	focusedWidget_ = nullptr;
	for (std::size_t i = 0; i < tabStrips_.size(); ++i) tabStrips_[i]->focused_ = false;
	for (std::size_t i = 0; i < selectionLists_.size(); ++i) selectionLists_[i]->focused_ = false;
	for (std::size_t i = 0; i < editableFields_.size(); ++i) editableFields_[i]->focused_ = false;
	focusedCustomId_ = id;
	if (UiTabStrip* value = findTabStrip(id)) value->focused_ = true;
	if (UiSelectionList* value = findSelectionList(id)) value->focused_ = true;
	if (UiEditableField* value = findEditableField(id)) value->focused_ = true;
}

void UIRoot::focusNext(bool backwards)
{
	std::vector<std::string> order = focusOrder_;
	for (std::size_t i = 0; i < widgets_.size(); ++i)
		if (std::find(order.begin(), order.end(), widgets_[i]->id()) == order.end()) order.push_back(widgets_[i]->id());
	for (std::size_t i = 0; i < tabStrips_.size(); ++i)
		if (std::find(order.begin(), order.end(), tabStrips_[i]->id()) == order.end()) order.push_back(tabStrips_[i]->id());
	for (std::size_t i = 0; i < selectionLists_.size(); ++i)
		if (std::find(order.begin(), order.end(), selectionLists_[i]->id()) == order.end()) order.push_back(selectionLists_[i]->id());
	for (std::size_t i = 0; i < editableFields_.size(); ++i)
		if (std::find(order.begin(), order.end(), editableFields_[i]->id()) == order.end()) order.push_back(editableFields_[i]->id());
	if (order.empty()) return;
	const std::string focusedId = focusedWidget_ ? focusedWidget_->id() : focusedCustomId_;
	int index = -1;
	for (std::size_t i = 0; i < order.size(); ++i)
		if (order[i] == focusedId) index = static_cast<int>(i);
	for (std::size_t attempt = 0; attempt < order.size(); ++attempt)
	{
		index = backwards ? (index <= 0 ? static_cast<int>(order.size()) - 1 : index - 1)
			: (index + 1) % static_cast<int>(order.size());
		const std::string& candidateId = order[static_cast<std::size_t>(index)];
		UiWidget* widget = find(candidateId);
		if (widget && isWidgetVisible(widget) && widget->enabled_)
		{
			focus(widget);
			closeCombo();
			return;
		}
		UiTabStrip* tab = findTabStrip(candidateId);
		if (tab && tab->visible_ && tab->enabled_)
		{
			focusCustom(candidateId);
			closeCombo();
			return;
		}
		UiSelectionList* list = findSelectionList(candidateId);
		if (list && list->visible_ && list->enabled_)
		{
			focusCustom(candidateId);
			closeCombo();
			return;
		}
		UiEditableField* field = findEditableField(candidateId);
		if (field && field->visible_ && field->enabled_)
		{
			focusCustom(candidateId);
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
		if (!commitFocusedField(action)) return true;
		if (action.event.kind != UiEventKind::None) return true;
		focusNext(event.shift);
		return true;
	}
	if (!focusedCustomId_.empty())
	{
		if (UiEditableField* field = findEditableField(focusedCustomId_))
		{
			if (!field->visible_ || !field->enabled_) return inputCaptured_;
			if (event.key == UiKey::Escape) { field->cancel(); return true; }
			if (event.key == UiKey::Enter && !field->editing_)
			{
				field->editing_ = true;
				field->draft_ = field->value_;
				return true;
			}
			if (event.key == UiKey::Backspace) { field->erase(); return true; }
			if (event.key == UiKey::Enter) return field->commit(action) || true;
			return inputCaptured_;
		}
		if (UiTabStrip* tab = findTabStrip(focusedCustomId_))
		{
			if (!tab->visible_ || !tab->enabled_) return inputCaptured_;
			if (event.key == UiKey::Left || event.key == UiKey::Right)
			{
				const int next = tab->selectedIndex_ + (event.key == UiKey::Left ? -1 : 1);
				if (next >= 0 && next < static_cast<int>(tab->tabs_.size()) && next != tab->selectedIndex_)
				{
					tab->setSelectedIndex(next);
					action = UiAction(tab->action_);
					action.appAction.selection = tab->selectedIndex_;
					action.event.controlId = tab->id_;
					action.event.kind = UiEventKind::SelectionChanged;
					action.event.selection = tab->selectedIndex_;
				}
				return true;
			}
		}
		if (UiSelectionList* list = findSelectionList(focusedCustomId_))
		{
			if (!list->visible_ || !list->enabled_) return inputCaptured_;
			if (event.key == UiKey::Up || event.key == UiKey::Down)
			{
				const int delta = event.key == UiKey::Up ? -1 : 1;
				const int next = list->selectedIndex_ + delta;
				if (next >= 0 && next < static_cast<int>(list->items_.size()) && next != list->selectedIndex_)
				{
					list->setSelectedIndex(next);
					action = UiAction(list->action_);
					action.appAction.selection = next;
					action.event.controlId = list->id_;
					action.event.kind = UiEventKind::SelectionChanged;
					action.event.selection = next;
				}
				return true;
			}
		}
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
