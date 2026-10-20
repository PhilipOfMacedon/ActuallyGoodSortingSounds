#pragma once

// Shared logical-space rectangle used by retained UI layout and chart views.
// The UI alias preserves existing control and layout call sites.
struct Rect2D
{
	float x, y, width, height;
	Rect2D(float left = 0.0f, float top = 0.0f, float w = 0.0f, float h = 0.0f)
		: x(left), y(top), width(w), height(h) {}
	bool contains(float px, float py) const
	{
		return px >= x && py >= y && px < x + width && py < y + height;
	}
};

typedef Rect2D UiRect;

// Axis-aligned logical-space clipping rectangle. A disabled clip means that
// the draw command is not clipped.
struct ClipBounds
{
	float x;
	float y;
	float width;
	float height;
	bool enabled;

	ClipBounds() : x(0.0f), y(0.0f), width(0.0f), height(0.0f), enabled(false) {}
	ClipBounds(float left, float top, float clipWidth, float clipHeight)
		: x(left), y(top), width(clipWidth), height(clipHeight), enabled(true) {}
};
