#pragma once

#include <functional>
#include <string>
#include <vector>

#include "UiAppearance.h"

// Caches measured line breaks for a single text block. The caller supplies a
// renderer-specific width measurement function, keeping this layout value
// independent of SDL, OpenGL, and font loading.
class TextBlockLayout
{
public:
	typedef std::function<float(const std::string&, const ResolvedTextStyle&)> MeasureTextWidth;

	explicit TextBlockLayout(const MeasureTextWidth& measureTextWidth);
	bool update(const std::string& text, float maximumWidth, const ResolvedTextStyle& style);
	const std::vector<std::string>& lines() const { return lines_; }

private:
	std::string text_;
	float maximumWidth_;
	ResolvedTextStyle style_;
	std::vector<std::string> lines_;
	MeasureTextWidth measureTextWidth_;
	bool valid_;

	void wrap();
};
