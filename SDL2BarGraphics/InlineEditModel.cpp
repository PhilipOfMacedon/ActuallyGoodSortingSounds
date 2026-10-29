#include "InlineEditModel.h"

#include <cmath>
#include <cstdlib>
#include <limits>

InlineEditModel::InlineEditModel()
	: target_(InlineEditTarget::None), text_(), replaceOnNextAcceptedCharacter_(false)
{
}

void InlineEditModel::begin(InlineEditTarget target, const std::string& initialText)
{
	if (target == InlineEditTarget::None)
	{
		finish();
		return;
	}
	target_ = target;
	text_ = initialText;
	replaceOnNextAcceptedCharacter_ = true;
}

void InlineEditModel::finish()
{
	target_ = InlineEditTarget::None;
	text_.clear();
	replaceOnNextAcceptedCharacter_ = false;
}

bool InlineEditModel::isAppearanceEdit() const
{
	return target_ == InlineEditTarget::AppearanceNumber || target_ == InlineEditTarget::AppearanceAlpha;
}

bool InlineEditModel::appendText(const char* text)
{
	if (!active()) return false;
	if (!text) return true;

	for (const char* character = text; *character; ++character)
	{
		const bool digit = *character >= '0' && *character <= '9';
		const bool decimal = *character == '.' &&
			(target_ == InlineEditTarget::Duration || isAppearanceEdit());
		const bool sign = *character == '-' && isAppearanceEdit();
		if (!digit && !decimal && !sign) continue;
		if (target_ == InlineEditTarget::Seed && !digit) continue;
		if (replaceOnNextAcceptedCharacter_)
		{
			text_.clear();
			replaceOnNextAcceptedCharacter_ = false;
		}
		if (text_.size() < MaximumTextLength) text_ += *character;
	}
	return true;
}

bool InlineEditModel::backspace()
{
	if (!active()) return false;
	replaceOnNextAcceptedCharacter_ = false;
	if (!text_.empty()) text_.erase(text_.size() - 1);
	return true;
}

void InlineEditModel::clear()
{
	if (!active()) return;
	text_.clear();
	replaceOnNextAcceptedCharacter_ = false;
}

bool InlineEditModel::tryParseInteger(int& value) const
{
	if (text_.empty()) return false;
	int parsed = 0;
	for (std::size_t i = 0; i < text_.size(); ++i)
	{
		const unsigned char character = static_cast<unsigned char>(text_[i]);
		if (character < '0' || character > '9') return false;
		const int digit = character - '0';
		if (parsed > (std::numeric_limits<int>::max() - digit) / 10) return false;
		parsed = parsed * 10 + digit;
	}
	value = parsed;
	return true;
}

bool InlineEditModel::tryParseUInt32(std::uint32_t& value) const
{
	if (text_.empty() || text_.size() > 10) return false;
	std::uint64_t parsed = 0;
	for (std::size_t i = 0; i < text_.size(); ++i)
	{
		const unsigned char character = static_cast<unsigned char>(text_[i]);
		if (character < '0' || character > '9') return false;
		parsed = parsed * 10 + (character - '0');
		if (parsed > (std::numeric_limits<std::uint32_t>::max)()) return false;
	}
	value = static_cast<std::uint32_t>(parsed);
	return true;
}

bool InlineEditModel::tryParseFloat(float& value) const
{
	char* end = nullptr;
	const float parsed = std::strtof(text_.c_str(), &end);
	if (!end || end == text_.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
	value = parsed;
	return true;
}

bool InlineEditModel::tryParseDouble(double& value) const
{
	char* end = nullptr;
	const double parsed = std::strtod(text_.c_str(), &end);
	if (!end || end == text_.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
	value = parsed;
	return true;
}
