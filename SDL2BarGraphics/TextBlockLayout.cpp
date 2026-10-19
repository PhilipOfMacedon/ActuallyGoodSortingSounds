#include "TextBlockLayout.h"

#include <stdexcept>
#include <sstream>

TextBlockLayout::TextBlockLayout(const MeasureTextWidth& measureTextWidth)
	: text_(), maximumWidth_(0.0f), style_(), lines_(), measureTextWidth_(measureTextWidth), valid_(false)
{
	if (!measureTextWidth_)
		throw std::invalid_argument("TextBlockLayout requires a text width measurer.");
}

bool TextBlockLayout::update(const std::string& text, float maximumWidth, const ResolvedTextStyle& style)
{
	if (valid_ && text_ == text && maximumWidth_ == maximumWidth && style_ == style)
		return false;
	text_ = text;
	maximumWidth_ = maximumWidth;
	style_ = style;
	valid_ = true;
	wrap();
	return true;
}

void TextBlockLayout::wrap()
{
	lines_.clear();
	if (text_.empty())
	{
		lines_.push_back(std::string());
		return;
	}
	std::istringstream words(text_);
	std::string word;
	std::string line;
	while (words >> word)
	{
		const std::string candidate = line.empty() ? word : line + " " + word;
		if (!line.empty() && measureTextWidth_(candidate, style_) > maximumWidth_)
		{
			lines_.push_back(line);
			line = word;
		}
		else line = candidate;
	}
	if (!line.empty()) lines_.push_back(line);
	else if (lines_.empty()) lines_.push_back(std::string());
}
