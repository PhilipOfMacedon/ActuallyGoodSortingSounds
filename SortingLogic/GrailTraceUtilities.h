#pragma once

#include <cstddef>

namespace sortinglogic
{
	namespace detail
	{
		class Recorder;

		std::size_t grailBinarySearch(Recorder& recorder, std::size_t position, std::size_t length,
			std::size_t keyIndex, bool lowerBound, const char* metricPrefix);
		void grailRotate(Recorder& recorder, std::size_t position, std::size_t leftLength,
			std::size_t rightLength, const char* metricPrefix);
		void grailMergeWithoutBuffer(Recorder& recorder, std::size_t position, std::size_t leftLength,
			std::size_t rightLength, const char* metricPrefix);
		void grailBufferedMerge(Recorder& recorder, std::size_t first, std::size_t middle, std::size_t last,
			std::size_t bufferCapacity, const char* lane, const char* metricPrefix);
	}
}
