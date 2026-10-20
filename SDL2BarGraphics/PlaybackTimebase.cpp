#include "PlaybackTimebase.h"

#include <algorithm>
#include <cmath>

namespace playbacktimebase
{
	double durationForReferenceEvents(std::size_t eventCount)
	{
		return static_cast<double>(eventCount) / static_cast<double>(ReferenceCadenceEventsPerSecond);
	}

	double elapsedForReferenceCursor(const std::vector<std::size_t>& eligibleEvents, std::size_t cursor)
	{
		const std::vector<std::size_t>::const_iterator end = std::lower_bound(eligibleEvents.begin(), eligibleEvents.end(), cursor);
		return static_cast<double>(end - eligibleEvents.begin()) /
			static_cast<double>(ReferenceCadenceEventsPerSecond);
	}

	std::size_t cursorForReferenceElapsed(const std::vector<std::size_t>& eligibleEvents,
		std::size_t eventCount, double elapsedSeconds, double durationSeconds)
	{
		if (elapsedSeconds >= durationSeconds) return eventCount;
		const double nonnegativeElapsed = (std::max)(0.0, elapsedSeconds);
		const double dueValue = std::floor(nonnegativeElapsed * ReferenceCadenceEventsPerSecond) + 1.0;
		const std::size_t due = dueValue <= 0.0 ? 0 : dueValue >= static_cast<double>(eligibleEvents.size())
			? eligibleEvents.size() : static_cast<std::size_t>(dueValue);
		if (due == 0) return 0;
		return (std::min)(eventCount, eligibleEvents[due - 1] + 1);
	}

	double secondsFromReferenceFrames(std::uint64_t frames)
	{
		return static_cast<double>(frames) / static_cast<double>(ReferenceTimelineSampleRate);
	}
}
