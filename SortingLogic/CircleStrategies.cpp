#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		std::size_t nextPowerOfTwo(std::size_t value)
		{
			std::size_t power = 1;
			while (power < value && power <= (static_cast<std::size_t>(-1) / 2)) power *= 2;
			return power;
		}

		void circleSortRecursivePass(Recorder& r, std::size_t low, std::size_t high, std::size_t& swaps)
		{
			if (low >= high) return;
			std::size_t left = low, right = high;
			while (left < right)
			{
				// The pinned implementation pads the range to a power of two and
				// skips pairs whose right member is virtual. Keeping that schedule
				// preserves its distinctive comparison order while safely handling
				// every real tail element.
				if (right < r.state.slots.size())
				{
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
					{
						r.swap(left, right);
						++swaps;
					}
				}
				++left;
				--right;
			}
			const std::size_t middle = low + (high - low) / 2;
			circleSortRecursivePass(r, low, middle, swaps);
			circleSortRecursivePass(r, middle + 1, high, swaps);
		}

		void circleSortKernel(Recorder& r, bool recursiveVariant)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			if (recursiveVariant)
			{
				const std::size_t paddedHigh = nextPowerOfTwo(n) - 1;
				for (;;)
				{
					std::size_t swaps = 0;
					circleSortRecursivePass(r, 0, paddedHigh, swaps);
					if (swaps == 0) break;
				}
				return;
			}

			// Iterative form visits the same mirrored pairs by decreasing dyadic
			// group width. Partial tail groups are clipped to the real sequence;
			// ArrayV's power-of-two padding loop otherwise leaves non-power-of-two
			// tails untouched, which is a source defect rather than a useful variant.
			for (;;)
			{
				std::size_t swaps = 0;
				for (std::size_t gap = nextPowerOfTwo(n) / 2; gap > 0; gap /= 2)
				{
					const std::size_t width = gap > static_cast<std::size_t>(-1) / 2
						? static_cast<std::size_t>(-1) : gap * 2;
					for (std::size_t start = 0; start < n; start += width)
					{
						const std::size_t end = start + width - 1;
						std::size_t left = start, right = end;
						while (left < right)
						{
							if (right < n)
							{
								const SortItem a = r.at(left), b = r.at(right);
								r.compare(a, b, left, right);
								if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
									{ r.swap(left, right); ++swaps; }
							}
							++left;
							--right;
						}
					}
				}
				if (swaps == 0) break;
			}
		}











		void introCircleSortKernel(Recorder& r, bool recursiveVariant)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("circle_passes");
			r.declareMetric("binary_fallbacks");
			if (count < 2) return;
			std::size_t power = 1;
			std::size_t threshold = 0;
			while (power < count)
			{
				power *= 2;
				++threshold;
			}
			threshold /= 2;
			std::size_t iterations = 0;
			for (;;)
			{
				++iterations;
				if (iterations >= threshold)
				{
					r.addMetric("binary_fallbacks");
					binaryInsertionRange(r, 0, count);
					break;
				}
				std::size_t swaps = 0;
				if (recursiveVariant)
					circleSortRecursivePass(r, 0, nextPowerOfTwo(count) - 1, swaps);
				else
				{
					const std::size_t paddedLength = nextPowerOfTwo(count);
					for (std::size_t gap = paddedLength / 2; gap > 0; gap /= 2)
					{
						const std::size_t width = gap * 2;
						for (std::size_t start = 0; start < count; start += width)
						{
							std::size_t left = start, right = start + width - 1;
							while (left < right)
							{
								if (right < count)
								{
									const SortItem a = r.at(left), b = r.at(right);
									r.compare(a, b, left, right);
									if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater)
										{ r.swap(left, right); ++swaps; }
								}
								++left;
								--right;
							}
						}
					}
				}
				r.addMetric("circle_passes");
				if (swaps == 0) break;
			}
		}


		void circleSortIterative(Recorder& recorder)
		{
			circleSortKernel(recorder, false);
		}

		void circleSortRecursive(Recorder& recorder)
		{
			circleSortKernel(recorder, true);
		}

		void introCircleSortIterative(Recorder& recorder)
		{
			introCircleSortKernel(recorder, false);
		}

		void introCircleSortRecursive(Recorder& recorder)
		{
			introCircleSortKernel(recorder, true);
		}
	}
}
