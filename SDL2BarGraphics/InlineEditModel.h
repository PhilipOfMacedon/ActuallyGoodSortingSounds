#pragma once

#include <cstddef>
#include <string>

enum class InlineEditTarget
{
	None,
	ItemCount,
	Duration,
	AppearanceNumber,
	AppearanceAlpha
};

// Platform-neutral text-entry state shared by the main setup controls and
// appearance editor. SDL key translation and applying parsed values stay in
// the host view adapter.
class InlineEditModel
{
public:
	static const std::size_t MaximumTextLength = 10;

	InlineEditModel();

	void begin(InlineEditTarget target, const std::string& initialText);
	void finish();
	bool active() const { return target_ != InlineEditTarget::None; }
	bool isAppearanceEdit() const;
	InlineEditTarget target() const { return target_; }
	const std::string& text() const { return text_; }

	bool appendText(const char* text);
	bool backspace();
	void clear();

	bool tryParseInteger(int& value) const;
	bool tryParseFloat(float& value) const;
	bool tryParseDouble(double& value) const;

private:
	InlineEditTarget target_;
	std::string text_;
	bool replaceOnNextAcceptedCharacter_;
};
