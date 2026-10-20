#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace playbacktimebase
{
	enum
	{
		ReferenceTimelineSampleRate = 44100,
		ReferenceCadenceEventsPerSecond = 300
	};

	double durationForReferenceEvents(std::size_t eventCount);
	double elapsedForReferenceCursor(const std::vector<std::size_t>& eligibleEvents, std::size_t cursor);
	std::size_t cursorForReferenceElapsed(const std::vector<std::size_t>& eligibleEvents,
		std::size_t eventCount, double elapsedSeconds, double durationSeconds);
	double secondsFromReferenceFrames(std::uint64_t frames);
}
