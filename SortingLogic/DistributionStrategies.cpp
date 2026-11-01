#include "AlgorithmStrategies.h"
#include "SortingLogic.h"
#include "SortingLogicInternal.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace sortinglogic
{
	using detail::TraceBudgetReached;
	using detail::require;

	namespace
	{
		using detail::Recorder;
		void counting(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n == 0) return;
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey);
				maximum = std::max(maximum, r.at(i).integerKey);
			}
			const std::size_t range = static_cast<std::size_t>(static_cast<std::int64_t>(maximum) - minimum + 1);
			r.resizeCounters("counts", range);
			r.resizeAuxiliary("count_output", n);
			std::vector<std::size_t> bucketBegin(range, NoGapIndex);
			for (std::size_t i = 0; i < n; ++i)
			{
				const int key = r.at(i).integerKey;
				const std::size_t bucket = static_cast<std::size_t>(r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(key) - minimum : static_cast<std::int64_t>(maximum) - key);
				if (bucketBegin[bucket] == NoGapIndex) bucketBegin[bucket] = r.trace.events.size();
				r.writeCounter("counts", bucket, r.counter("counts", bucket) + 1);
			}
			for (std::size_t bucket = 0; bucket < range; ++bucket)
				if (bucketBegin[bucket] != NoGapIndex)
					r.annotate("counts", RegionKind::Bucket, bucket, bucket + 1, 1, static_cast<int>(bucket), "Key bucket", bucketBegin[bucket]);
			for (std::size_t i = 1; i < range; ++i)
				r.writeCounter("counts", i, r.counter("counts", i) + r.counter("counts", i - 1));
			for (std::size_t i = n; i > 0; --i)
			{
				const SortItem item = r.at(i - 1);
				const std::size_t bucket = static_cast<std::size_t>(r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(item.integerKey) - minimum : static_cast<std::int64_t>(maximum) - item.integerKey);
				const int target = r.counter("counts", bucket) - 1;
				r.writeAuxiliary("count_output", static_cast<std::size_t>(target), item);
				r.writeCounter("counts", bucket, target);
			}
			r.publishAuxiliaryToMain("count_output", 0, 0, n, "Counting Sort output reconstruction");
		}

		void radix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n == 0) return;
			const int defaultBase = r.trace.algorithm == SortAlgorithm::ArrayvLsdRadix ? 4 : 10;
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t base = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 256) : defaultBase);
			r.resizeCounters("digits", base);
			r.resizeAuxiliary("radix_output", n);
			r.probe(0, SemanticRole::ProcessedItem);
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 1; i < n; ++i)
			{
				r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey);
				maximum = std::max(maximum, r.at(i).integerKey);
			}
			const std::int64_t transformedMaximum = static_cast<std::int64_t>(maximum) - minimum;
			std::int64_t place = 1;
			do
			{
				std::vector<std::size_t> bucketBegin(base, NoGapIndex);
				for (std::size_t d = 0; d < base; ++d) r.writeCounter("digits", d, 0);
				const auto transformed = [&r, minimum, maximum](const SortItem& item)
					{ return r.trace.direction == SortDirection::Ascending ? static_cast<std::int64_t>(item.integerKey) - minimum : static_cast<std::int64_t>(maximum) - item.integerKey; };
				for (std::size_t i = 0; i < n; ++i)
				{
					r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = static_cast<std::size_t>((transformed(r.at(i)) / place) % static_cast<std::int64_t>(base));
					if (bucketBegin[digit] == NoGapIndex) bucketBegin[digit] = r.trace.events.size();
					r.writeCounter("digits", digit, r.counter("digits", digit) + 1);
				}
				for (std::size_t d = 1; d < base; ++d)
					r.writeCounter("digits", d, r.counter("digits", d) + r.counter("digits", d - 1));
				for (std::size_t i = n; i > 0; --i)
				{
					r.probe(i - 1, SemanticRole::ProcessedItem);
					const SortItem item = r.at(i - 1);
					const std::size_t digit = static_cast<std::size_t>((transformed(item) / place) % static_cast<std::int64_t>(base));
					const int target = r.counter("digits", digit) - 1;
					r.writeAuxiliary("radix_output", static_cast<std::size_t>(target), item);
					r.writeCounter("digits", digit, target);
				}
				r.publishAuxiliaryToMain("radix_output", 0, 0, n,
					"Radix digit pass reconstruction");
				r.addMetric("digit_passes");
				for (std::size_t d = 0; d < base; ++d)
					if (bucketBegin[d] != NoGapIndex)
						r.annotate("digits", RegionKind::Bucket, d, d + 1, 1, static_cast<int>(d),
							"Digit " + std::to_string(d), bucketBegin[d]);
				if (transformedMaximum / place < static_cast<std::int64_t>(base)) break;
				place *= static_cast<std::int64_t>(base);
			} while (place > 0);
		}

		std::int64_t orderedIntegerKey(const SortItem& item, SortDirection direction)
		{
			return direction == SortDirection::Ascending ? static_cast<std::int64_t>(item.integerKey) : -static_cast<std::int64_t>(item.integerKey);
		}

		std::uint32_t binaryKey(const SortItem& item, SortDirection direction)
		{
			std::uint32_t key = static_cast<std::uint32_t>(item.integerKey) ^ 0x80000000u;
			return direction == SortDirection::Ascending ? key : ~key;
		}

		std::size_t maxKeyBits(const Recorder& r)
		{
			std::uint32_t maximum = 0;
			for (std::size_t i = 0; i < r.state.slots.size(); ++i)
				maximum = std::max(maximum, binaryKey(r.at(i), r.trace.direction));
			std::size_t bits = 0;
			while (maximum) { ++bits; maximum >>= 1; }
			return bits;
		}

		void americanFlag(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t base = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 256) : 128);
			std::size_t bitsPerDigit = 0;
			while ((static_cast<std::size_t>(1) << bitsPerDigit) < base) ++bitsPerDigit;
			const std::size_t actualBase = static_cast<std::size_t>(1) << bitsPerDigit;
			const std::uint32_t mask = static_cast<std::uint32_t>(actualBase - 1);
			const std::size_t keyBits = maxKeyBits(r);
			if (keyBits == 0) { for (std::size_t i = 0; i < n; ++i) r.probe(i, SemanticRole::ProcessedItem); return; }
			std::size_t topShift = ((keyBits - 1) / bitsPerDigit) * bitsPerDigit;
			struct Range { std::size_t first, last, shift; };
			std::vector<Range> pending(1, Range{ 0, n, topShift });
			r.resizeCounters("american_flag_counts", actualBase);
			r.resizeCounters("american_flag_next", actualBase);
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				std::vector<std::size_t> counts(actualBase, 0), starts(actualBase, range.first), ends(actualBase, range.first);
				const std::size_t begin = r.trace.events.size();
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					r.chargeWork();
					r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = (binaryKey(r.at(i), r.trace.direction) >> range.shift) & mask;
					++counts[digit];
				}
				std::size_t cursor = range.first;
				for (std::size_t digit = 0; digit < actualBase; ++digit)
				{
					starts[digit] = cursor;
					cursor += counts[digit];
					ends[digit] = cursor;
					r.writeCounter("american_flag_counts", digit, static_cast<int>(counts[digit]));
					r.writeCounter("american_flag_next", digit, static_cast<int>(starts[digit]));
				}
				for (std::size_t digit = 0; digit < actualBase; ++digit)
				{
					while (static_cast<std::size_t>(r.counter("american_flag_next", digit)) < ends[digit])
					{
						r.chargeWork();
						const std::size_t at = static_cast<std::size_t>(r.counter("american_flag_next", digit));
						const std::size_t destination = (binaryKey(r.at(at), r.trace.direction) >> range.shift) & mask;
						if (destination == digit)
							r.writeCounter("american_flag_next", digit, static_cast<int>(at + 1));
						else
					{
						const std::size_t target = static_cast<std::size_t>(r.counter("american_flag_next", destination));
						require(target < ends[destination], "American Flag bucket cursor exceeded its destination range.");
						r.swap(at, target, SemanticRole::ProcessedItem);
						r.writeCounter("american_flag_next", destination, static_cast<int>(target + 1));
					}
					}
					if (counts[digit] != 0)
						r.annotate("american_flag_counts", RegionKind::Bucket, digit, digit + 1, 1,
							static_cast<int>(digit), "Digit bucket " + std::to_string(digit), begin);
					if (range.shift > 0 && ends[digit] - starts[digit] > 1)
						pending.push_back(Range{ starts[digit], ends[digit], range.shift - bitsPerDigit });
				}
			}
		}

		std::size_t partitionBinary(Recorder& r, std::size_t first, std::size_t last, int bit)
		{
			if (bit < 0 || last - first < 2) return first;
			std::size_t left = first, right = last;
			while (left < right)
			{
				while (left < right && ((binaryKey(r.at(left), r.trace.direction) >> bit) & 1u) == 0)
				{
					r.chargeWork(); r.probe(left, SemanticRole::ProcessedItem); ++left;
				}
				while (left < right && ((binaryKey(r.at(right - 1), r.trace.direction) >> bit) & 1u) != 0)
				{
					r.chargeWork(); r.probe(right - 1, SemanticRole::ProcessedItem); --right;
				}
				if (left < right)
				{
					r.probe(left, SemanticRole::ProcessedItem);
					r.probe(right - 1, SemanticRole::ProcessedItem);
					r.swap(left, right - 1, SemanticRole::ProcessedItem);
					++left; --right;
				}
			}
			return left;
		}

		void binaryQuick(Recorder& r, int mode)
		{
			if (r.state.slots.size() < 2) return;
			const int highestBit = static_cast<int>(maxKeyBits(r)) - 1;
			if (mode == 1)
			{
				std::function<void(std::size_t, std::size_t, int)> sortRange;
				sortRange = [&r, &sortRange](std::size_t first, std::size_t last, int bit)
				{
					if (bit < 0 || last - first < 2) return;
					std::size_t left = first, right = last;
					while (left < right)
					{
						while (left < right && ((binaryKey(r.at(left), r.trace.direction) >> bit) & 1u) == 0)
						{ r.chargeWork(); r.probe(left, SemanticRole::ProcessedItem); ++left; }
						while (left < right && ((binaryKey(r.at(right - 1), r.trace.direction) >> bit) & 1u) != 0)
						{ r.chargeWork(); r.probe(right - 1, SemanticRole::ProcessedItem); --right; }
						if (left < right)
						{
							r.probe(left, SemanticRole::ProcessedItem); r.probe(right - 1, SemanticRole::ProcessedItem);
							r.swap(left, right - 1, SemanticRole::ProcessedItem); ++left; --right;
						}
					}
					sortRange(first, left, bit - 1);
					sortRange(left, last, bit - 1);
				};
				sortRange(0, r.state.slots.size(), highestBit);
				return;
			}
			struct BinaryRange { std::size_t first, last; int bit; };
			std::vector<BinaryRange> pending(1, BinaryRange{ 0, r.state.slots.size(), highestBit });
			std::size_t queueCursor = 0;
			while (mode == 0 ? queueCursor < pending.size() : !pending.empty())
			{
				const BinaryRange range = mode == 0 ? pending[queueCursor++] : pending.back();
				if (mode != 0) pending.pop_back();
				if (range.bit < 0 || range.last - range.first < 2) continue;
				const std::size_t middle = partitionBinary(r, range.first, range.last, range.bit);
				if (middle - range.first > 1) pending.push_back(BinaryRange{ range.first, middle, range.bit - 1 });
				if (range.last - middle > 1) pending.push_back(BinaryRange{ middle, range.last, range.bit - 1 });
			}
		}

		void stacklessBinaryRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const int highestBit = static_cast<int>(maxKeyBits(r)) - 1;
			for (int bit = highestBit; bit >= 0; --bit)
			{
				std::size_t first = 0;
				while (first < n)
				{
					r.chargeWork();
					const std::uint32_t prefix = bit == 31 ? 0u : (binaryKey(r.at(first), r.trace.direction) >> (bit + 1));
					std::size_t last = first + 1;
					while (last < n)
					{
						r.chargeWork();
						const std::uint32_t candidate = bit == 31 ? 0u : (binaryKey(r.at(last), r.trace.direction) >> (bit + 1));
						if (candidate != prefix) break;
						++last;
					}
					partitionBinary(r, first, last, bit);
					first = last;
				}
				r.addMetric("bit_passes");
			}
		}

		void stacklessAmericanFlag(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t requested = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 256) : 16);
			std::size_t bits = 0;
			while ((static_cast<std::size_t>(1) << bits) < requested) ++bits;
			const std::size_t base = static_cast<std::size_t>(1) << bits;
			const std::uint32_t mask = static_cast<std::uint32_t>(base - 1);
			const std::size_t keyBits = maxKeyBits(r);
			if (keyBits == 0) return;
			const int topShift = static_cast<int>(((keyBits - 1) / bits) * bits);
			r.resizeCounters("stackless_flag_count", base);
			r.resizeCounters("stackless_flag_next", base);
			for (int shift = topShift; shift >= 0; shift -= static_cast<int>(bits))
			{
				std::size_t first = 0;
				while (first < n)
				{
					const std::uint32_t prefix = shift + static_cast<int>(bits) >= 32 ? 0u :
						(binaryKey(r.at(first), r.trace.direction) >> (shift + static_cast<int>(bits)));
					std::size_t last = first + 1;
					while (last < n)
					{
						r.chargeWork();
						const std::uint32_t candidate = shift + static_cast<int>(bits) >= 32 ? 0u :
							(binaryKey(r.at(last), r.trace.direction) >> (shift + static_cast<int>(bits)));
						if (candidate != prefix) break;
						++last;
					}
					std::vector<std::size_t> counts(base, 0), starts(base, first), ends(base, first);
					for (std::size_t i = first; i < last; ++i)
					{
						r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
						++counts[(binaryKey(r.at(i), r.trace.direction) >> shift) & mask];
					}
					std::size_t cursor = first;
					for (std::size_t digit = 0; digit < base; ++digit)
					{
						starts[digit] = cursor; cursor += counts[digit]; ends[digit] = cursor;
						r.writeCounter("stackless_flag_count", digit, static_cast<int>(counts[digit]));
						r.writeCounter("stackless_flag_next", digit, static_cast<int>(starts[digit]));
					}
					const std::size_t phase = r.trace.events.size();
					for (std::size_t digit = 0; digit < base; ++digit)
						while (static_cast<std::size_t>(r.counter("stackless_flag_next", digit)) < ends[digit])
						{
							r.chargeWork();
							const std::size_t at = static_cast<std::size_t>(r.counter("stackless_flag_next", digit));
							const std::size_t targetDigit = (binaryKey(r.at(at), r.trace.direction) >> shift) & mask;
							if (targetDigit == digit) r.writeCounter("stackless_flag_next", digit, static_cast<int>(at + 1));
							else
							{
								const std::size_t target = static_cast<std::size_t>(r.counter("stackless_flag_next", targetDigit));
								require(target < ends[targetDigit], "Stackless American Flag cursor exceeded its bucket.");
								r.swap(at, target, SemanticRole::ProcessedItem);
								r.writeCounter("stackless_flag_next", targetDigit, static_cast<int>(target + 1));
							}
						}
					for (std::size_t digit = 0; digit < base; ++digit)
						if (counts[digit] > 0) r.annotate("stackless_flag_count", RegionKind::Bucket, digit, digit + 1, 1,
							static_cast<int>(digit), "Digit bucket " + std::to_string(digit), phase);
					first = last;
				}
				r.addMetric("digit_passes");
			}
		}

		void flash(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const std::size_t classes = std::max<std::size_t>(2, std::min<std::size_t>(n, static_cast<std::size_t>(0.2 * n) + 2));
			// Direction is folded into a monotone scalar before assigning flash classes.
			const auto classFor = [&r, minimum, maximum, classes](const SortItem& item)
			{
				const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
				const std::uint64_t offset = static_cast<std::uint64_t>(orderedIntegerKey(item, r.trace.direction) - minimum);
				return static_cast<std::size_t>((offset * (classes - 1)) / span);
			};
			std::vector<std::size_t> counts(classes, 0), starts(classes, 0), ends(classes, 0);
			const std::size_t begin = r.trace.events.size();
			r.resizeCounters("flash_classes", classes);
			r.resizeCounters("flash_next", classes);
			for (std::size_t i = 0; i < n; ++i) { r.probe(i, SemanticRole::ProcessedItem); ++counts[classFor(r.at(i))]; }
			std::size_t cursor = 0;
			for (std::size_t c = 0; c < classes; ++c)
			{
				starts[c] = cursor; cursor += counts[c]; ends[c] = cursor;
				r.writeCounter("flash_classes", c, static_cast<int>(counts[c]));
				r.writeCounter("flash_next", c, static_cast<int>(starts[c]));
			}
			for (std::size_t c = 0; c < classes; ++c)
			{
				while (static_cast<std::size_t>(r.counter("flash_next", c)) < ends[c])
				{
					r.chargeWork();
					const std::size_t at = static_cast<std::size_t>(r.counter("flash_next", c));
					const std::size_t targetClass = classFor(r.at(at));
					if (targetClass == c) r.writeCounter("flash_next", c, static_cast<int>(at + 1));
					else
					{
						const std::size_t target = static_cast<std::size_t>(r.counter("flash_next", targetClass));
						require(target < ends[targetClass], "Flash class cursor exceeded its target range.");
						r.swap(at, target, SemanticRole::ProcessedItem);
						r.writeCounter("flash_next", targetClass, static_cast<int>(target + 1));
					}
				}
				if (counts[c] > 0) r.annotate("flash_classes", RegionKind::Bucket, c, c + 1, 1, static_cast<int>(c),
					"Flash class " + std::to_string(c), begin);
			}
			for (std::size_t c = 0; c < classes; ++c)
			{
				for (std::size_t i = starts[c] + 1; i < ends[c]; ++i)
				{
					const SortItem key = r.at(i); r.hold(i); std::size_t position = i;
					while (position > starts[c])
					{
						const SortItem previous = r.at(position - 1);
						r.compare(previous, key, position - 1, i, "main", "main", SemanticRole::ComparedItem, SemanticRole::HeldKey);
						if (compareItems(previous, key, r.trace.direction) != ComparisonResult::Greater) break;
						r.shift(position - 1, position); --position;
					}
					r.place(position, key);
				}
			}
		}

		void pigeonhole(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			int minimum = r.at(0).integerKey, maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				minimum = std::min(minimum, r.at(i).integerKey); maximum = std::max(maximum, r.at(i).integerKey);
			}
			(void)minimum; (void)maximum;
			r.resizeAuxiliary("pigeonhole_output", n);
			std::int64_t low = orderedIntegerKey(r.at(0), r.trace.direction), high = low;
			for (std::size_t i = 1; i < n; ++i) { low = std::min(low, orderedIntegerKey(r.at(i), r.trace.direction)); high = std::max(high, orderedIntegerKey(r.at(i), r.trace.direction)); }
			const std::size_t orderedRange = static_cast<std::size_t>(high - low + 1);
			r.resizeCounters("pigeonholes", orderedRange); r.resizeCounters("pigeonhole_next", orderedRange);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t bucket = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - low);
				r.writeCounter("pigeonholes", bucket, r.counter("pigeonholes", bucket) + 1);
			}
			std::size_t position = 0;
			for (std::size_t bucket = 0; bucket < orderedRange; ++bucket)
			{
				r.writeCounter("pigeonhole_next", bucket, static_cast<int>(position));
				position += static_cast<std::size_t>(r.counter("pigeonholes", bucket));
			}
			for (std::size_t i = 0; i < n; ++i)
			{
				const SortItem item = r.at(i);
				const std::size_t bucket = static_cast<std::size_t>(orderedIntegerKey(item, r.trace.direction) - low);
				const std::size_t target = static_cast<std::size_t>(r.counter("pigeonhole_next", bucket));
				r.writeAuxiliary("pigeonhole_output", target, item);
				r.writeCounter("pigeonhole_next", bucket, static_cast<int>(target + 1));
			}
			r.publishAuxiliaryToMain("pigeonhole_output", 0, 0, n, "Pigeonhole output reconstruction");
		}

		void indexSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			for (std::size_t i = 0; i < n; ++i)
			{
				std::size_t guard = 0;
				while (true)
				{
					const std::size_t destination = r.trace.direction == SortDirection::Ascending
						? static_cast<std::size_t>(r.at(i).integerKey) : n - 1 - static_cast<std::size_t>(r.at(i).integerKey);
					if (destination == i) break;
					r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
					if (destination >= n) break;
					r.swap(i, destination, SemanticRole::ProcessedItem);
					if (++guard > n) throw std::logic_error("Index Sort requires a unique permutation of [0,N-1].");
				}
			}
		}

		void stableBucketPartition(Recorder& r, std::size_t first, std::size_t last,
			std::int64_t minimum, std::int64_t maximum, std::size_t base, std::int64_t place, const std::string& lane)
		{
			if (last - first < 2 || minimum == maximum) return;
			std::vector<std::size_t> counts(base, 0);
			const std::size_t begin = r.trace.events.size();
			const auto digit = [&r, minimum, maximum, base, place](const SortItem& item)
			{
				const std::int64_t key = orderedIntegerKey(item, r.trace.direction) - minimum;
				const std::int64_t divisor = std::max<std::int64_t>(1, place);
				return static_cast<std::size_t>((key / divisor) % static_cast<std::int64_t>(base));
			};
			r.resizeCounters(lane, base);
			for (std::size_t i = first; i < last; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				++counts[digit(r.at(i))];
			}
			for (std::size_t b = 0; b < base; ++b) r.writeCounter(lane, b, static_cast<int>(counts[b]));
			std::size_t destination = first;
			for (std::size_t b = 0; b < base; ++b)
			{
				const std::size_t bucketStart = destination;
				std::size_t scan = destination;
				while (scan < last)
				{
					r.chargeWork(); r.probe(scan, SemanticRole::ProcessedItem);
					if (digit(r.at(scan)) == b)
					{
						if (scan > destination)
						{
							const SortItem held = r.at(scan); r.hold(scan);
							for (std::size_t p = scan; p > destination; --p) r.shift(p - 1, p);
							r.place(destination, held);
						}
						++destination; scan = destination;
					}
					else ++scan;
				}
				if (destination > bucketStart)
					r.annotate(lane, RegionKind::Bucket, b, b + 1, 1, static_cast<int>(b),
						"Bucket " + std::to_string(b), begin);
			}
		}

		void inPlaceLsdRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t base = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 16) : 4);
			std::int64_t place = 1;
			do
			{
				stableBucketPartition(r, 0, n, minimum, maximum, base, place, "inplace_lsd_buckets");
				r.addMetric("digit_passes");
				if ((maximum - minimum) / place < static_cast<std::int64_t>(base)) break;
				place *= static_cast<std::int64_t>(base);
			} while (place > 0);
		}

		void rotateRadix(Recorder& r, bool mostSignificantFirst)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t base = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 16) : 4);
			std::int64_t topPlace = 1;
			while (topPlace <= (maximum - minimum) / static_cast<std::int64_t>(base)) topPlace *= static_cast<std::int64_t>(base);
			if (!mostSignificantFirst)
			{
				for (std::int64_t place = 1; place <= maximum - minimum; place *= static_cast<std::int64_t>(base))
				{
					stableBucketPartition(r, 0, n, minimum, maximum, base, place, "rotate_lsd_buckets");
					r.addMetric("digit_passes");
					if (place > (maximum - minimum) / static_cast<std::int64_t>(base)) break;
				}
				return;
			}
			struct Range { std::size_t first, last; std::int64_t place; };
			std::vector<Range> pending(1, Range{ 0, n, topPlace });
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				const std::size_t begin = r.trace.events.size();
				stableBucketPartition(r, range.first, range.last, minimum, maximum, base, range.place, "rotate_msd_buckets");
				r.addMetric("digit_passes");
				std::vector<std::size_t> sizes(base, 0);
				for (std::size_t i = range.first; i < range.last; ++i) ++sizes[static_cast<std::size_t>(((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base))];
				std::size_t cursor = range.first;
				for (std::size_t b = 0; b < base; ++b)
				{
					const std::size_t next = cursor + sizes[b];
					if (next - cursor > 1 && range.place > 1)
						pending.push_back(Range{ cursor, next, range.place / static_cast<std::int64_t>(base) });
					cursor = next;
				}
				(void)begin;
			}
		}

		void msdRadix(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			const int requestedBase = r.trace.configuration.value("radix_base", 0);
			const std::size_t base = static_cast<std::size_t>(requestedBase >= 2 ? std::min(requestedBase, 64) : 4);
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction);
				minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			std::int64_t topPlace = 1;
			while (topPlace <= (maximum - minimum) / static_cast<std::int64_t>(base)) topPlace *= static_cast<std::int64_t>(base);
			r.resizeAuxiliary("msd_radix_output", n);
			r.resizeCounters("msd_radix_counts", base);
			struct Range { std::size_t first, last; std::int64_t place; };
			std::vector<Range> pending(1, Range{ 0, n, topPlace });
			while (!pending.empty())
			{
				const Range range = pending.back(); pending.pop_back();
				if (range.last - range.first < 2) continue;
				const std::size_t begin = r.trace.events.size();
				std::vector<std::size_t> counts(base, 0), starts(base, range.first), ends(base, range.first);
				for (std::size_t b = 0; b < base; ++b) r.writeCounter("msd_radix_counts", b, 0);
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
					const std::size_t digit = static_cast<std::size_t>(((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base));
					++counts[digit]; r.writeCounter("msd_radix_counts", digit, static_cast<int>(counts[digit]));
				}
				std::size_t cursor = range.first;
				for (std::size_t b = 0; b < base; ++b) { starts[b] = cursor; cursor += counts[b]; ends[b] = cursor; }
				std::vector<std::size_t> next = starts;
				for (std::size_t i = range.first; i < range.last; ++i)
				{
					const SortItem item = r.at(i);
					const std::size_t digit = static_cast<std::size_t>(((orderedIntegerKey(item, r.trace.direction) - minimum) / range.place) % static_cast<std::int64_t>(base));
					r.writeAuxiliary("msd_radix_output", next[digit]++, item);
				}
				r.publishAuxiliaryToMain("msd_radix_output", range.first, range.first, range.last - range.first,
					"MSD Radix prefix-bucket reconstruction");
				for (std::size_t b = 0; b < base; ++b)
				{
					if (counts[b]) r.annotate("msd_radix_counts", RegionKind::Bucket, b, b + 1, 1, static_cast<int>(b),
						"Digit bucket " + std::to_string(b), begin);
					if (range.place > 1 && ends[b] - starts[b] > 1)
						pending.push_back(Range{ starts[b], ends[b], range.place / static_cast<std::int64_t>(base) });
				}
				r.addMetric("digit_passes");
			}
		}

		void gravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t range = static_cast<std::size_t>(maximum - minimum + 1);
			r.resizeCounters("gravity_histogram", range);
			r.resizeCounters("gravity_columns", range);
			r.resizeAuxiliary("gravity_output", n);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t key = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				r.writeCounter("gravity_histogram", key, r.counter("gravity_histogram", key) + 1);
			}
			std::size_t target = 0;
			for (std::size_t key = 0; key < range; ++key)
			{
				r.chargeWork();
				const int count = r.counter("gravity_histogram", key);
				r.writeCounter("gravity_columns", key, count);
				for (std::size_t i = 0; i < n; ++i)
				{
					r.chargeWork();
					if (static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum) == key)
						r.writeAuxiliary("gravity_output", target++, r.at(i));
				}
			}
			r.publishAuxiliaryToMain("gravity_output", 0, 0, n, "Gravity Sort output reconstruction");
			r.addMetric("gravity_columns", static_cast<std::uint64_t>(range));
		}

		void classicGravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t height = static_cast<std::size_t>(maximum - minimum + 1);
			if (height != 0 && n > 256) throw TraceBudgetReached("Classic Gravity is limited to 256 items because its bead matrix is quadratic.");
			r.resizeCounters("gravity_beads", n * height);
			r.resizeCounters("gravity_column_counts", height);
			for (std::size_t row = 0; row < n; ++row)
			{
				const std::size_t beads = static_cast<std::size_t>(orderedIntegerKey(r.at(row), r.trace.direction) - minimum);
				for (std::size_t column = 0; column < beads; ++column)
				{
					r.chargeWork(); r.writeCounter("gravity_beads", row * height + column, 1);
				}
			}
			for (std::size_t column = 0; column < height; ++column)
			{
				std::size_t beads = 0;
				for (std::size_t row = 0; row < n; ++row)
				{
					r.chargeWork();
					beads += r.counter("gravity_beads", row * height + column) != 0;
				}
				r.writeCounter("gravity_column_counts", column, static_cast<int>(beads));
				for (std::size_t row = 0; row < n; ++row)
				{
					r.chargeWork();
					const int next = row >= n - beads ? 1 : 0;
					if (r.counter("gravity_beads", row * height + column) != next)
						r.writeCounter("gravity_beads", row * height + column, next);
				}
			}
			std::vector<SortItem> items;
			items.reserve(n);
			for (std::size_t i = 0; i < n; ++i) items.push_back(r.at(i));
			std::stable_sort(items.begin(), items.end(), [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
			r.resizeAuxiliary("gravity_bead_output", n);
			for (std::size_t i = 0; i < n; ++i) r.writeAuxiliary("gravity_bead_output", i, items[i]);
			r.publishAuxiliaryToMain("gravity_bead_output", 0, 0, n, "Bead output reconstruction");
		}

		void simplisticGravity(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n == 0) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::size_t range = static_cast<std::size_t>(maximum - minimum + 1);
			r.resizeCounters("gravity_transfer_columns", range);
			r.resizeAuxiliary("gravity_transfer_output", n);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::size_t beads = static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				for (std::size_t bead = 0; bead < beads; ++bead)
				{
					r.chargeWork(); r.writeCounter("gravity_transfer_columns", bead, r.counter("gravity_transfer_columns", bead) + 1);
				}
			}
			std::size_t target = 0;
			for (std::size_t key = 0; key < range; ++key)
				for (std::size_t i = 0; i < n; ++i)
				{
					r.chargeWork();
					if (static_cast<std::size_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum) == key)
						r.writeAuxiliary("gravity_transfer_output", target++, r.at(i));
				}
			r.publishAuxiliaryToMain("gravity_transfer_output", 0, 0, n, "Gravity transfer reconstruction");
		}

		void shatter(Recorder& r, bool simple)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			const std::int64_t range = maximum - minimum + 1;
			if (simple)
			{
				for (std::int64_t place = 1; place < range; place *= 2)
					stableBucketPartition(r, 0, n, minimum, maximum, 2, place, "simple_shatter_bits");
				return;
			}
			const std::size_t groups = std::max<std::size_t>(2, static_cast<std::size_t>(std::sqrt(static_cast<double>(n))));
			const std::int64_t width = std::max<std::int64_t>(1, (range + static_cast<std::int64_t>(groups) - 1) / static_cast<std::int64_t>(groups));
			const std::size_t bucketCount = static_cast<std::size_t>((range + width - 1) / width);
			std::vector<std::size_t> counts(bucketCount, 0), starts(bucketCount, 0);
			std::vector<std::vector<SortItem> > buckets(bucketCount);
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::size_t bucket = static_cast<std::size_t>((orderedIntegerKey(r.at(i), r.trace.direction) - minimum) / width);
				buckets[bucket].push_back(r.at(i)); ++counts[bucket];
			}
			r.resizeCounters("shatter_groups", bucketCount); r.resizeAuxiliary("shatter_output", n);
			std::size_t cursor = 0;
			for (std::size_t b = 0; b < bucketCount; ++b) { starts[b] = cursor; cursor += counts[b]; r.writeCounter("shatter_groups", b, static_cast<int>(counts[b])); }
			for (std::size_t b = 0; b < bucketCount; ++b)
			{
				r.chargeWork(buckets[b].size() * buckets[b].size());
				std::stable_sort(buckets[b].begin(), buckets[b].end(), [&r](const SortItem& a, const SortItem& b)
					{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
				for (std::size_t i = 0; i < buckets[b].size(); ++i) r.writeAuxiliary("shatter_output", starts[b] + i, buckets[b][i]);
			}
			r.publishAuxiliaryToMain("shatter_output", 0, 0, n, "Shatter group output reconstruction");
		}

		void staticSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			std::int64_t minimum = orderedIntegerKey(r.at(0), r.trace.direction), maximum = minimum;
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const std::int64_t key = orderedIntegerKey(r.at(i), r.trace.direction); minimum = std::min(minimum, key); maximum = std::max(maximum, key);
			}
			if (minimum == maximum) return;
			const std::size_t bucketsCount = n;
			const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum);
			std::vector<std::vector<SortItem> > buckets(bucketsCount);
			for (std::size_t i = 0; i < n; ++i)
			{
				const std::uint64_t offset = static_cast<std::uint64_t>(orderedIntegerKey(r.at(i), r.trace.direction) - minimum);
				const std::size_t b = static_cast<std::size_t>((offset * (bucketsCount - 1)) / span);
				buckets[b].push_back(r.at(i));
			}
			r.resizeCounters("static_buckets", bucketsCount); r.resizeAuxiliary("static_output", n);
			std::size_t cursor = 0;
			for (std::size_t b = 0; b < bucketsCount; ++b)
			{
				r.chargeWork(buckets[b].size() * buckets[b].size());
				r.chargeWork();
				r.writeCounter("static_buckets", b, static_cast<int>(buckets[b].size()));
				std::stable_sort(buckets[b].begin(), buckets[b].end(), [&r](const SortItem& a, const SortItem& b)
					{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Less; });
				for (std::size_t i = 0; i < buckets[b].size(); ++i) r.writeAuxiliary("static_output", cursor++, buckets[b][i]);
			}
			r.publishAuxiliaryToMain("static_output", 0, 0, n, "Static bucket output reconstruction");
		}

		void timeSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size(); if (n < 2) return;
			if (n > 128) throw TraceBudgetReached("Time Sort is limited to 128 items so deterministic deadline events stay inspectable.");
			const int magnitude = std::max(1, std::min(1000, r.trace.configuration.value("time_magnitude", 10)));
			r.resizeAuxiliary("time_deadlines", n);
			std::vector<std::pair<std::int64_t, SortItem> > deadlines;
			deadlines.reserve(n);
			const int phase = r.beginPhase("Deterministic deadlines (ties retain input order)");
			for (std::size_t i = 0; i < n; ++i)
			{
				r.chargeWork(); r.probe(i, SemanticRole::ProcessedItem);
				const SortItem item = r.at(i); deadlines.push_back(std::make_pair(orderedIntegerKey(item, r.trace.direction) * magnitude, item));
				r.writeAuxiliary("time_deadlines", i, item);
			}
			std::stable_sort(deadlines.begin(), deadlines.end(), [](const std::pair<std::int64_t, SortItem>& a, const std::pair<std::int64_t, SortItem>& b)
				{ return a.first < b.first; });
			r.endPhase(phase, "Deadline order published");
			r.resizeAuxiliary("time_output", n);
			for (std::size_t i = 0; i < n; ++i) r.writeAuxiliary("time_output", i, deadlines[i].second);
			r.publishAuxiliaryToMain("time_output", 0, 0, n, "Deadline output reconstruction");
			for (std::size_t i = 1; i < n; ++i)
			{
				const SortItem a = r.at(i - 1), b = r.at(i);
				r.compare(a, b, i - 1, i);
			}
			r.addMetric("maximum_deadline", static_cast<std::uint64_t>(std::max<std::int64_t>(0,
				orderedIntegerKey(r.at(n - 1), r.trace.direction) * magnitude)));
		}

		void asynchronousSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			r.declareMetric("asynchronous_key_passes");
			r.declareMetric("asynchronous_source_probes");
			r.declareMetric("asynchronous_emitted_items");
			if (count == 0) return;

			const std::string lane = "asynchronous_scan";
			const std::string consumedLane = "asynchronous_consumed";
			r.resizeAuxiliary(lane, count);
			r.resizeCounters(consumedLane, count);
			const auto valueRank = [&r](const SortItem& item) -> std::int64_t
			{
				return r.trace.direction == SortDirection::Ascending
					? static_cast<std::int64_t>(item.integerKey) : -static_cast<std::int64_t>(item.integerKey);
			};
			std::int64_t minimumRank = valueRank(r.at(0)), maximumRank = minimumRank;
			for (std::size_t i = 0; i < count; ++i)
			{
				r.chargeWork();
				const SortItem item = r.at(i);
				r.writeAuxiliary(lane, i, item);
				const std::int64_t rank = valueRank(item);
				minimumRank = (std::min)(minimumRank, rank);
				maximumRank = (std::max)(maximumRank, rank);
			}

			std::uint64_t completedPasses = 0, probes = 0;
			std::size_t output = 0;
			const bool detailedReconstruction = r.detailedReconstructionEnabled();
			for (std::int64_t currentRank = minimumRank; currentRank <= maximumRank; ++currentRank)
			{
				const int phase = r.beginPhase("Asynchronous value pass " + std::to_string(currentRank));
				std::size_t thisPassOutputs = 0;
				if (detailedReconstruction)
					for (std::size_t source = 0; source < count; ++source)
					{
						r.chargeWork();
						if (r.counter(consumedLane, source) == 0 && valueRank(r.auxiliaryAt(lane, source)) <= currentRank)
							++thisPassOutputs;
					}
				const int reconstruction = detailedReconstruction && thisPassOutputs != 0
					? r.beginReconstruction("Asynchronous emitted-item reconstruction", thisPassOutputs) : -1;
				std::size_t completedThisPass = 0;
				for (std::size_t source = 0; source < count; ++source)
				{
					r.probeAuxiliary(lane, source, SemanticRole::ProbedItem);
					const SortItem item = r.auxiliaryAt(lane, source);
					if (r.counter(consumedLane, source) == 0 && valueRank(item) <= currentRank)
					{
						if (reconstruction >= 0)
						{
							++completedThisPass;
							++output;
							r.writeMainFromAuxiliary(output - 1, lane, source, 0, completedThisPass, thisPassOutputs);
						}
						else r.writeMain(output++, item, SemanticRole::WriteTarget);
						r.writeCounter(consumedLane, source, 1);
					}
				}
				if (reconstruction >= 0)
					r.endReconstruction(reconstruction, "Asynchronous emitted-item reconstruction");
				r.endPhase(phase, "Asynchronous value pass " + std::to_string(currentRank));
				++completedPasses;
				probes += count;
				r.setMetric("asynchronous_key_passes", completedPasses);
				r.setMetric("asynchronous_source_probes", probes);
				r.setMetric("asynchronous_emitted_items", output);
			}
			if (output != count) throw std::logic_error("Asynchronous Sort did not emit every input item.");
			r.resizeAuxiliary(lane, 0);
			r.resizeCounters(consumedLane, 0);
		}
	}

	namespace strategies
	{
		void counting(detail::Recorder& recorder) { sortinglogic::counting(recorder); }
		void radix(detail::Recorder& recorder) { sortinglogic::radix(recorder); }
		void americanFlag(detail::Recorder& recorder) { sortinglogic::americanFlag(recorder); }
		void binaryQuickIterative(detail::Recorder& recorder) { sortinglogic::binaryQuick(recorder, 0); }
		void binaryQuickRecursive(detail::Recorder& recorder) { sortinglogic::binaryQuick(recorder, 1); }
		void stacklessBinaryRadix(detail::Recorder& recorder) { sortinglogic::stacklessBinaryRadix(recorder); }
		void stacklessAmericanFlag(detail::Recorder& recorder) { sortinglogic::stacklessAmericanFlag(recorder); }
		void flash(detail::Recorder& recorder) { sortinglogic::flash(recorder); }
		void pigeonhole(detail::Recorder& recorder) { sortinglogic::pigeonhole(recorder); }
		void indexSort(detail::Recorder& recorder) { sortinglogic::indexSort(recorder); }
		void inPlaceLsdRadix(detail::Recorder& recorder) { sortinglogic::inPlaceLsdRadix(recorder); }
		void rotateLsdRadix(detail::Recorder& recorder) { sortinglogic::rotateRadix(recorder, false); }
		void rotateMsdRadix(detail::Recorder& recorder) { sortinglogic::rotateRadix(recorder, true); }
		void msdRadix(detail::Recorder& recorder) { sortinglogic::msdRadix(recorder); }
		void gravity(detail::Recorder& recorder) { sortinglogic::gravity(recorder); }
		void classicGravity(detail::Recorder& recorder) { sortinglogic::classicGravity(recorder); }
		void simplisticGravity(detail::Recorder& recorder) { sortinglogic::simplisticGravity(recorder); }
		void shatterSort(detail::Recorder& recorder) { sortinglogic::shatter(recorder, false); }
		void simpleShatterSort(detail::Recorder& recorder) { sortinglogic::shatter(recorder, true); }
		void staticSort(detail::Recorder& recorder) { sortinglogic::staticSort(recorder); }
		void timeSort(detail::Recorder& recorder) { sortinglogic::timeSort(recorder); }
		void asynchronousSort(detail::Recorder& recorder) { sortinglogic::asynchronousSort(recorder); }
	}
}
