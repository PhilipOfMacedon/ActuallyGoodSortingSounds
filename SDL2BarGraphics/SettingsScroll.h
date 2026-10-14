#pragma once

inline float settingsScrollOffsetAfterWheel(float currentOffset, int wheelY,
	float rowHeight, float contentHeight, float viewportHeight)
{
	float maximum = contentHeight - viewportHeight;
	if (maximum < 0.0f) maximum = 0.0f;
	const float requested = currentOffset - static_cast<float>(wheelY) * rowHeight * 3.0f;
	if (requested < 0.0f) return 0.0f;
	if (requested > maximum) return maximum;
	return requested;
}
