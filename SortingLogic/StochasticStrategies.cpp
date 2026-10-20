#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::require;
		void bogo(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0xB060B060u);
			const std::size_t count = r.state.slots.size();
			while (true)
			{
				bool sorted = true;
				for (std::size_t i = 1; i < count; ++i)
				{
					const SortItem a = r.at(i - 1), b = r.at(i);
					r.compare(a, b, i - 1, i);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) { sorted = false; break; }
				}
				if (sorted) break;
				for (std::size_t i = count; i > 1; --i)
				{
					std::uniform_int_distribution<std::size_t> choose(0, i - 1);
					r.swap(i - 1, choose(random));
				}
				r.addMetric("shuffle_attempts");
			}
		}

		static bool bogoSorted(Recorder& r)
		{
			for (std::size_t i = 1; i < r.state.slots.size(); ++i)
			{
				const SortItem left = r.at(i - 1), right = r.at(i);
				r.compare(left, right, i - 1, i);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void bozoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xB020B020u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 1);
			while (!bogoSorted(r))
			{
				const std::size_t first = choose(random), second = choose(random);
				r.swap(first, second);
				r.addMetric("random_attempts");
			}
		}

		void bubbleBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xBB060601u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 2);
			while (!bogoSorted(r))
			{
				const std::size_t index = choose(random);
				const SortItem left = r.at(index), right = r.at(index + 1);
				r.compare(left, right, index, index + 1);
				if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) r.swap(index, index + 1);
				r.addMetric("random_attempts");
			}
		}

		void exchangeBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xE8C4A6E1u);
			std::uniform_int_distribution<std::size_t> choose(0, count - 1);
			while (!bogoSorted(r))
			{
				const std::size_t first = choose(random), second = choose(random);
				const SortItem a = r.at(first), b = r.at(second);
				r.compare(a, b, first, second);
				const ComparisonResult result = compareItems(a, b, r.trace.direction);
				if ((first < second && result == ComparisonResult::Greater) ||
					(first > second && result == ComparisonResult::Less)) r.swap(first, second);
				r.addMetric("random_attempts");
			}
		}

		void deterministicBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::function<bool(std::size_t)> enumerate = [&r, count, &enumerate](std::size_t depth) -> bool
			{
				if (depth >= count - 1)
				{
					r.addMetric("permutation_checks");
					return bogoSorted(r);
				}
				for (std::size_t i = count - 1; i > depth; --i)
				{
					if (enumerate(depth + 1)) return true;
					if ((count - depth) % 2 == 0) r.swap(depth, i);
					else r.swap(depth, count - 1);
				}
				return enumerate(depth + 1);
			};
			enumerate(0);
		}

		static bool suffixMinimumAt(Recorder& r, std::size_t first)
		{
			const SortItem candidate = r.at(first);
			for (std::size_t i = first + 1; i < r.state.slots.size(); ++i)
			{
				const SortItem other = r.at(i);
				r.compare(candidate, other, first, i, "main", "main", SemanticRole::CurrentMinimum, SemanticRole::ComparedItem);
				if (compareItems(candidate, other, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		static bool rangeMinimumAt(Recorder& r, std::size_t first, std::size_t candidateIndex, std::size_t end)
		{
			const SortItem candidate = r.at(candidateIndex);
			for (std::size_t i = first; i < end; ++i)
			{
				if (i == candidateIndex) continue;
				const SortItem other = r.at(i);
				r.compare(candidate, other, candidateIndex, i, "main", "main", SemanticRole::CurrentMinimum, SemanticRole::ComparedItem);
				if (compareItems(candidate, other, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		static bool rangeMaximumAt(Recorder& r, std::size_t first, std::size_t candidateIndex, std::size_t end)
		{
			const SortItem candidate = r.at(candidateIndex);
			for (std::size_t i = first; i < end; ++i)
			{
				if (i == candidateIndex) continue;
				const SortItem other = r.at(i);
				r.compare(other, candidate, i, candidateIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(other, candidate, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		static void bogoShuffleRange(Recorder& r, std::size_t first, std::size_t end, std::mt19937& random)
		{
			for (std::size_t i = first; i < end; ++i)
			{
				std::uniform_int_distribution<std::size_t> choose(i, end - 1);
				r.swap(i, choose(random));
			}
		}

		void cocktailBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xC0C7A11u);
			std::size_t first = 0, end = count;
			while (first + 1 < end)
			{
				if (rangeMinimumAt(r, first, first, end)) { ++first; continue; }
				if (rangeMaximumAt(r, first, end - 1, end)) { --end; continue; }
				bogoShuffleRange(r, first, end, random);
				r.addMetric("random_shuffles");
			}
		}

		static bool rangeSplitAt(Recorder& r, std::size_t first, std::size_t middle, std::size_t end)
		{
			if (first >= middle || middle >= end) return true;
			std::size_t leftMaximum = first;
			for (std::size_t i = first + 1; i < middle; ++i)
			{
				const SortItem currentMax = r.at(leftMaximum), candidate = r.at(i);
				r.compare(currentMax, candidate, leftMaximum, i, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (compareItems(currentMax, candidate, r.trace.direction) == ComparisonResult::Less) leftMaximum = i;
			}
			const SortItem maxLeft = r.at(leftMaximum);
			for (std::size_t i = middle; i < end; ++i)
			{
				const SortItem right = r.at(i);
				r.compare(maxLeft, right, leftMaximum, i, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (compareItems(maxLeft, right, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void medianQuickBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x4D454449u);
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &random, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				const std::size_t middle = first + (end - first) / 2;
				const std::size_t begin = r.trace.events.size();
				while (!rangeSplitAt(r, first, middle, end))
				{
					bogoShuffleRange(r, first, end, random);
					r.addMetric("random_shuffles");
				}
				r.addMetric("split_partitions");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Median Quick Bogo split", begin);
				sortRange(first, middle, depth + 1);
				sortRange(middle, end, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		static bool bogoPivotPartitioned(Recorder& r, std::size_t first, std::size_t pivotIndex, std::size_t end)
		{
			const SortItem pivot = r.at(pivotIndex);
			for (std::size_t i = first; i < pivotIndex; ++i)
			{
				const SortItem item = r.at(i);
				r.compare(item, pivot, i, pivotIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::Pivot);
				if (compareItems(item, pivot, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			for (std::size_t i = pivotIndex + 1; i < end; ++i)
			{
				const SortItem item = r.at(i);
				r.compare(pivot, item, pivotIndex, i, "main", "main", SemanticRole::Pivot, SemanticRole::ComparedItem);
				if (compareItems(pivot, item, r.trace.direction) == ComparisonResult::Greater) return false;
			}
			return true;
		}

		void quickBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x0A1CB060u);
			std::function<void(std::size_t, std::size_t, std::size_t)> sortRange;
			sortRange = [&r, &random, &sortRange](std::size_t first, std::size_t end, std::size_t depth)
			{
				if (end - first < 2) return;
				r.setMetric("maximum_depth", std::max<std::uint64_t>(r.state.metrics.algorithm["maximum_depth"], depth));
				std::size_t pivot = first;
				const std::size_t begin = r.trace.events.size();
				while (!bogoPivotPartitioned(r, first, pivot, end))
				{
					for (std::size_t i = first; i < end; ++i)
					{
						std::uniform_int_distribution<std::size_t> choose(i, end - 1);
						const std::size_t other = choose(random);
						if (pivot == i) pivot = other;
						else if (pivot == other) pivot = i;
						r.swap(i, other, SemanticRole::Pivot);
					}
					r.addMetric("random_shuffles");
				}
				r.addMetric("partitions");
				r.annotate("main", RegionKind::Contiguous, first, end, 1, -1, "Quick Bogo partition", begin);
				sortRange(first, pivot, depth + 1);
				sortRange(pivot + 1, end, depth + 1);
			};
			if (r.state.slots.size() > 1) sortRange(0, r.state.slots.size(), 1);
		}

		static bool guessIndicesSorted(Recorder& r, const std::string& lane, bool reverseScan, std::size_t* failureIndex)
		{
			const std::size_t count = r.state.slots.size();
			if (failureIndex) *failureIndex = NoGapIndex;
			for (std::size_t step = 0; step + 1 < count; ++step)
			{
				const std::size_t i = reverseScan ? count - 2 - step : step;
				const std::size_t leftIndex = static_cast<std::size_t>(r.counter(lane, i));
				const std::size_t rightIndex = static_cast<std::size_t>(r.counter(lane, i + 1));
				require(leftIndex < count && rightIndex < count, "Guess workspace index is outside the source input.");
				const SortItem left = r.trace.input[leftIndex], right = r.trace.input[rightIndex];
				r.compare(left, right, leftIndex, rightIndex);
				const ComparisonResult result = compareItems(left, right, r.trace.direction);
				if (result == ComparisonResult::Less || (result == ComparisonResult::Equal && leftIndex < rightIndex)) continue;
				if (failureIndex) *failureIndex = i;
				return false;
			}
			return true;
		}

		static void commitGuessPermutation(Recorder& r, const std::string& indexLane)
		{
			const std::size_t count = r.state.slots.size();
			const std::string outputLane = "guess_output";
			r.resizeAuxiliary(outputLane, count);
			std::vector<SortItem> output;
			output.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				const std::size_t source = static_cast<std::size_t>(r.counter(indexLane, i));
				require(source < r.trace.input.size(), "Guess result index is outside the source input.");
				const SortItem item = r.trace.input[source];
				output.push_back(item);
				r.writeAuxiliary(outputLane, i, item);
			}
			r.writeMainRange(0, output);
			r.resizeAuxiliary(outputLane, 0);
		}

		static void advanceGuessIndices(Recorder& r, const std::string& lane, std::size_t firstEligible)
		{
			const std::size_t count = r.state.slots.size();
			std::size_t position = 0;
			while (position < count)
			{
				if (position >= firstEligible && r.counter(lane, position) < static_cast<int>(count - 1))
				{
					r.writeCounter(lane, position, r.counter(lane, position) + 1);
					return;
				}
				r.writeCounter(lane, position, 0);
				++position;
			}
		}

		void optimizedGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			while (true)
			{
				r.addMetric("candidate_tuples");
				if (guessIndicesSorted(r, lane, false, nullptr)) break;
				advanceGuessIndices(r, lane, 0);
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void smartGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			while (true)
			{
				r.addMetric("candidate_tuples");
				std::size_t failure = NoGapIndex;
				if (guessIndicesSorted(r, lane, true, &failure)) break;
				advanceGuessIndices(r, lane, failure);
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void randomGuessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			r.resizeCounters(lane, count);
			std::mt19937 random(r.trace.inputSeed ^ 0x6A355EEDu);
			while (!guessIndicesSorted(r, lane, false, nullptr))
			{
				for (std::size_t i = 0; i < count; ++i)
			{
					std::uniform_int_distribution<int> choose(0, static_cast<int>(count - 1));
					r.writeCounter(lane, i, choose(random));
				}
				r.addMetric("random_guesses");
			}
			commitGuessPermutation(r, lane);
			r.resizeCounters(lane, 0);
		}

		void guessSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			const std::string lane = "guess_indices";
			const std::string resultLane = "accepted_guess_indices";
			r.resizeCounters(lane, count);
			r.resizeCounters(resultLane, count);
			bool haveCandidate = false;
			while (true)
			{
				r.addMetric("candidate_tuples");
				std::uint64_t total = 0;
				for (std::size_t i = 0; i < count; ++i)
					for (std::size_t j = 0; j < count; ++j)
					{
						r.addMetric("index_pair_checks");
						if (r.counter(lane, i) == r.counter(lane, j)) ++total;
					}
				for (std::size_t i = 0; i < count; ++i)
					for (std::size_t j = 0; j < count; ++j)
					{
						const std::size_t leftIndex = static_cast<std::size_t>(r.counter(lane, i));
						const std::size_t rightIndex = static_cast<std::size_t>(r.counter(lane, j));
						const SortItem left = r.trace.input[leftIndex], right = r.trace.input[rightIndex];
						r.compare(left, right, leftIndex, rightIndex);
						const ComparisonResult expectedInversion = i < j ? ComparisonResult::Greater : ComparisonResult::Less;
						if (compareItems(left, right, r.trace.direction) == expectedInversion) ++total;
					}
				if (total == count)
				{
					haveCandidate = true;
					for (std::size_t i = 0; i < count; ++i)
						r.writeCounter(resultLane, i, r.counter(lane, i));
				}
				const std::size_t previousEvents = r.trace.events.size();
				advanceGuessIndices(r, lane, 0);
				if (r.counter(lane, 0) == 0 && r.trace.events.size() > previousEvents &&
					static_cast<std::size_t>(r.counter(lane, 1)) == 0)
				{
					bool allZero = true;
					for (std::size_t i = 0; i < count; ++i) allZero = allZero && r.counter(lane, i) == 0;
					if (allZero) break;
				}
			}
			require(haveCandidate, "Guess Sort exhausted its index tuples without finding a stable permutation.");
			commitGuessPermutation(r, resultLane);
			r.resizeCounters(lane, 0);
			r.resizeCounters(resultLane, 0);
		}

		SortItem itemInLane(const Recorder& r, const std::string& lane, std::size_t index)
		{
			return lane == "main" ? r.at(index) : r.auxiliaryAt(lane, index);
		}

		static void shuffleLane(Recorder& r, const std::string& lane, std::size_t first, std::size_t end, std::mt19937& random)
		{
			for (std::size_t i = first; i < end; ++i)
			{
				r.chargeWork();
				std::uniform_int_distribution<std::size_t> choose(i, end - 1);
				const std::size_t other = choose(random);
				if (lane == "main") r.swap(i, other);
				else r.swapAuxiliary(lane, i, other);
			}
		}

		static bool bogoBogoIsSorted(Recorder& r, const std::string& sourceLane, std::size_t length,
			std::size_t depth, std::mt19937& random);

		static void bogoBogoSortAuxiliary(Recorder& r, const std::string& lane, std::size_t length,
			std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return;
			while (!bogoBogoIsSorted(r, lane, length, depth, random))
			{
				shuffleLane(r, lane, 0, length, random);
				r.addMetric("random_shuffles");
			}
		}

		static bool bogoBogoIsSorted(Recorder& r, const std::string& sourceLane, std::size_t length,
			std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return true;
			r.chargeWork(length);
			r.addMetric("nested_checks");
			const std::string checkLane = "bogo_bogo_check_" + std::to_string(depth);
			r.resizeAuxiliary(checkLane, length);
			for (std::size_t i = 0; i < length; ++i)
				r.writeAuxiliary(checkLane, i, itemInLane(r, sourceLane, i));
			bogoBogoSortAuxiliary(r, checkLane, length - 1, depth + 1, random);
			while (true)
			{
				const SortItem left = r.auxiliaryAt(checkLane, length - 2);
				const SortItem right = r.auxiliaryAt(checkLane, length - 1);
				r.compare(left, right, length - 2, length - 1, checkLane, checkLane);
				if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
				shuffleLane(r, checkLane, 0, length, random);
				r.addMetric("random_shuffles");
				bogoBogoSortAuxiliary(r, checkLane, length - 1, depth + 1, random);
			}
			bool sameOrder = true;
			for (std::size_t i = 0; i < length; ++i)
			{
				const SortItem input = itemInLane(r, sourceLane, i);
				const SortItem ordered = r.auxiliaryAt(checkLane, i);
				r.compare(input, ordered, i, i, sourceLane, checkLane);
				if (compareItems(input, ordered, r.trace.direction) != ComparisonResult::Equal)
				{
					sameOrder = false;
					break;
				}
			}
			r.resizeAuxiliary(checkLane, 0);
			return sameOrder;
		}

		void bogoBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0xB060B060u);
			while (!bogoBogoIsSorted(r, "main", count, 0, random))
			{
				shuffleLane(r, "main", 0, count, random);
				r.addMetric("random_shuffles");
			}
		}

		static void smartBogoBogoPrefix(Recorder& r, std::size_t length, std::size_t depth, std::mt19937& random)
		{
			if (length < 2) return;
			smartBogoBogoPrefix(r, length - 1, depth + 1, random);
			while (true)
			{
				r.chargeWork();
				const SortItem left = r.at(length - 2), right = r.at(length - 1);
				r.compare(left, right, length - 2, length - 1);
				if (compareItems(left, right, r.trace.direction) != ComparisonResult::Greater) break;
				shuffleLane(r, "main", 0, length, random);
				r.addMetric("random_shuffles");
				r.addMetric("recursive_prefix_retries");
				smartBogoBogoPrefix(r, length - 1, depth + 1, random);
			}
		}

		void smartBogoBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x5B060B06u);
			smartBogoBogoPrefix(r, r.state.slots.size(), 0, random);
		}

		static void mergeBogoRange(Recorder& r, std::size_t first, std::size_t end, std::mt19937& random)
		{
			if (end - first < 2) return;
			const std::size_t middle = first + (end - first) / 2;
			mergeBogoRange(r, first, middle, random);
			mergeBogoRange(r, middle, end, random);
			const std::string lane = "merge_bogo";
			const std::size_t length = end - first;
			r.resizeAuxiliary(lane, length);
			for (std::size_t i = 0; i < length; ++i) r.writeAuxiliary(lane, i, r.at(first + i));
			const std::size_t annotationBegin = r.trace.events.size();
			r.annotate("main", RegionKind::Contiguous, first, middle, 1, -1, "Merge Bogo left run", annotationBegin);
			r.annotate("main", RegionKind::Contiguous, middle, end, 1, -1, "Merge Bogo right run", annotationBegin);
			while (true)
			{
				bool sorted = true;
				for (std::size_t i = first + 1; i < end; ++i)
				{
					r.chargeWork();
					const SortItem left = r.at(i - 1), right = r.at(i);
					r.compare(left, right, i - 1, i);
					if (compareItems(left, right, r.trace.direction) == ComparisonResult::Greater) sorted = false;
				}
				if (sorted) break;
				std::vector<int> sources(length, 1);
				const std::size_t leftLength = middle - first;
				for (std::size_t i = 0; i < leftLength; ++i) sources[i] = 0;
				for (std::size_t i = length; i > 1; --i)
				{
					r.chargeWork();
					std::uniform_int_distribution<std::size_t> choose(0, i - 1);
					std::swap(sources[i - 1], sources[choose(random)]);
				}
				std::size_t left = 0, right = leftLength;
				std::vector<SortItem> weave;
				weave.reserve(length);
				for (std::size_t i = 0; i < length; ++i)
					weave.push_back(r.auxiliaryAt(lane, sources[i] == 0 ? left++ : right++));
				r.writeMainRange(first, weave);
				r.addMetric("random_weaves");
			}
			r.resizeAuxiliary(lane, 0);
		}

		void mergeBogoSort(Recorder& r)
		{
			std::mt19937 random(r.trace.inputSeed ^ 0x4D3B060u);
			if (r.state.slots.size() > 1) mergeBogoRange(r, 0, r.state.slots.size(), random);
		}

		void selectionBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0x5E1EC710u);
			for (std::size_t first = 0; first < count; ++first)
			{
				std::uniform_int_distribution<std::size_t> choose(first, count - 1);
				while (!suffixMinimumAt(r, first))
				{
					r.swap(first, choose(random), SemanticRole::CurrentMinimum);
					r.addMetric("random_attempts");
				}
			}
		}

		void lessBogoSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			if (count < 2) return;
			std::mt19937 random(r.trace.inputSeed ^ 0x1E55B060u);
			for (std::size_t first = 0; first < count; ++first)
			{
				while (!suffixMinimumAt(r, first))
				{
					for (std::size_t i = first; i < count; ++i)
					{
						std::uniform_int_distribution<std::size_t> choose(i, count - 1);
						r.swap(i, choose(random));
					}
					r.addMetric("random_shuffles");
				}
			}
		}
	}
}