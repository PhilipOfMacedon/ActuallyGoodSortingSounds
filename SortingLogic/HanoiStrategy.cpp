#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::require;

		void hanoiSort(Recorder& r)
		{
			const std::size_t length = r.state.slots.size();
			const char* metricKeys[] = { "hanoi_item_transfers", "hanoi_duplicate_batches", "hanoi_comparisons", "hanoi_tower_moves" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (length < 2) return;

			// ArrayV's version moves a run of equal keys together, then runs an
			// iterative three-peg Hanoi schedule to insert each next key into the
			// ordered tower. The app keeps the same move schedule but represents the
			// pegs as reversible item lanes and commits the returned tower once.
			std::vector<SortItem> array;
			array.reserve(length);
			for (std::size_t i = 0; i < length; ++i) array.push_back(r.at(i));
			r.resizeAuxiliary("hanoi-main-stack", length);
			for (std::size_t i = 0; i < length; ++i) r.writeAuxiliary("hanoi-main-stack", i, array[i]);
			std::vector<SortItem> stack2, stack3;
			std::size_t sp = 0, unsorted = 0, targetIndex = 0;
			SortItem target;
			std::uint64_t targetMoves = 0;
			const auto pegLane = [](int peg) { return peg == 2 ? std::string("hanoi-peg-2") : std::string("hanoi-peg-3"); };
			const auto push = [&r](std::vector<SortItem>& stack, const std::string& lane, const SortItem& item)
			{
				const std::size_t index = stack.size();
				stack.push_back(item);
				r.resizeAuxiliary(lane, stack.size());
				r.writeAuxiliary(lane, index, item);
				r.addMetric("hanoi_item_transfers");
			};
			const auto pop = [&r](std::vector<SortItem>& stack, const std::string& lane)
			{
				require(!stack.empty(), "Hanoi peg pop requires an item.");
				const SortItem item = stack.back();
				stack.pop_back();
				r.resizeAuxiliary(lane, stack.size());
				r.addMetric("hanoi_item_transfers");
				return item;
			};
			const auto compareMainToPeg = [&r, &pegLane](std::size_t mainIndex, const SortItem& item,
				int peg, const SortItem& top, std::size_t topIndex, SemanticRole role)
			{
				r.compare(item, top, mainIndex, topIndex, "hanoi-main-stack", pegLane(peg), role, SemanticRole::ComparedItem);
				r.addMetric("hanoi_comparisons");
			};
			const auto moveFromMain = [&r, &array, &sp, &unsorted, &push, &compareMainToPeg, &pegLane]
				(std::vector<SortItem>& stack, int peg, bool checkUnsorted)
			{
				// The source always transfers the first item; checkUnsorted limits
				// only the following equal-key batch, after sp advances.
				if (sp >= array.size()) return std::size_t(0);
				std::size_t duplicates = 1;
				const SortItem first = array[sp];
				push(stack, pegLane(peg), first);
				++sp;
				bool ended = sp >= array.size() || (checkUnsorted && sp >= unsorted);
				while (!ended)
				{
					r.chargeWork();
					compareMainToPeg(sp, array[sp], peg, stack.back(), stack.size() - 1, SemanticRole::ComparedItem);
					if (compareItems(array[sp], stack.back(), r.trace.direction) != ComparisonResult::Equal) break;
					push(stack, pegLane(peg), array[sp]);
					++sp;
					++duplicates;
					ended = sp >= array.size() || (checkUnsorted && sp >= unsorted);
				}
				if (duplicates > 1) r.addMetric("hanoi_duplicate_batches");
				return duplicates;
			};
			const auto moveToMain = [&r, &array, &sp, &pop, &compareMainToPeg, &pegLane]
				(std::vector<SortItem>& stack, int peg)
			{
				require(sp > 0 && !stack.empty(), "Hanoi return move exceeds the main stack or an empty peg.");
				--sp;
				array[sp] = pop(stack, pegLane(peg));
				r.writeAuxiliary("hanoi-main-stack", sp, array[sp]);
				while (!stack.empty())
				{
					r.chargeWork();
					compareMainToPeg(sp, array[sp], peg, stack.back(), stack.size() - 1, SemanticRole::ComparedItem);
					if (compareItems(stack.back(), array[sp], r.trace.direction) != ComparisonResult::Equal) break;
					require(sp > 0, "Hanoi equal-key batch moved before the main stack start.");
					--sp;
					array[sp] = pop(stack, pegLane(peg));
				}
			};
			const auto moveBetweenStacks = [&r, &push, &pop, &pegLane]
				(std::vector<SortItem>& from, int fromPeg, std::vector<SortItem>& to, int toPeg)
			{
				require(!from.empty(), "Hanoi inter-peg move requires a source item.");
				push(to, pegLane(toPeg), pop(from, pegLane(fromPeg)));
				while (!from.empty())
				{
					r.chargeWork();
					r.compare(from.back(), to.back(), from.size() - 1, to.size() - 1,
						pegLane(fromPeg), pegLane(toPeg));
					r.addMetric("hanoi_comparisons");
					if (compareItems(from.back(), to.back(), r.trace.direction) != ComparisonResult::Equal) break;
					push(to, pegLane(toPeg), pop(from, pegLane(fromPeg)));
				}
			};
			const auto validMoveCount = [](std::uint64_t moves)
			{
				return (moves & (moves + 1)) == 0;
			};
			const auto heightForMoves = [](std::uint64_t movesPlusOne)
			{
				std::size_t height = 0;
				while (movesPlusOne > 1) { movesPlusOne >>= 1; ++height; }
				return height;
			};
			const auto hanoi = [&r, &array, &sp, &unsorted, &targetIndex, &target, &targetMoves,
				&stack2, &stack3, &moveFromMain, &moveToMain, &moveBetweenStacks, &pegLane, &validMoveCount]
				(int startPeg, bool goRight, int endCondition)
			{
				std::uint64_t moves = 0;
				int smallestPeg = startPeg;
				const auto endConditionMet = [&r, &targetIndex, &target, &targetMoves, &stack2, &pegLane, &validMoveCount,
					endCondition, &moves]()
				{
					if (!validMoveCount(moves)) return false;
					switch (endCondition)
					{
					case 1:
						if (stack2.empty()) return true;
						r.compare(target, stack2.back(), targetIndex, stack2.size() - 1,
							"main", pegLane(2), SemanticRole::HeldKey, SemanticRole::ComparedItem);
						r.addMetric("hanoi_comparisons");
						return compareItems(target, stack2.back(), r.trace.direction) != ComparisonResult::Greater;
					case 2: return moves == targetMoves;
					case 3: return stack2.empty();
					default: throw std::logic_error("Unknown Hanoi end condition.");
					}
				};
			if (!endConditionMet())
			{
				++moves;
				r.addMetric("hanoi_tower_moves");
				switch (smallestPeg)
				{
				case 1:
					if (goRight) { moveFromMain(stack2, 2, true); smallestPeg = 2; }
					else { moveFromMain(stack3, 3, true); smallestPeg = 3; }
					break;
				case 2:
					if (goRight) { moveBetweenStacks(stack2, 2, stack3, 3); smallestPeg = 3; }
					else moveToMain(stack2, 2);
					break;
				case 3:
					if (goRight) { moveToMain(stack3, 3); smallestPeg = 1; }
					else { moveBetweenStacks(stack3, 3, stack2, 2); smallestPeg = 2; }
					break;
				}
			}
			while (!endConditionMet())
			{
				moves += 2;
				r.addMetric("hanoi_tower_moves", 2);
				switch (smallestPeg)
				{
				case 1:
					if (!stack2.empty() && (stack3.empty() ||
						(compareItems(stack2.back(), stack3.back(), r.trace.direction) == ComparisonResult::Less)))
						moveBetweenStacks(stack2, 2, stack3, 3);
					else if (!stack3.empty()) moveBetweenStacks(stack3, 3, stack2, 2);
					if (goRight) { moveFromMain(stack2, 2, true); smallestPeg = 2; }
					else { moveFromMain(stack3, 3, true); smallestPeg = 3; }
					break;
				case 2:
					if (stack3.empty() || (sp < unsorted &&
						compareItems(array[sp], stack3.back(), r.trace.direction) == ComparisonResult::Less))
						moveFromMain(stack3, 3, true);
					else moveToMain(stack3, 3);
					if (goRight) { moveBetweenStacks(stack2, 2, stack3, 3); smallestPeg = 3; }
					else { moveToMain(stack2, 2); smallestPeg = 1; }
					break;
				case 3:
					if (stack2.empty() || (sp < unsorted &&
						compareItems(array[sp], stack2.back(), r.trace.direction) == ComparisonResult::Less))
						moveFromMain(stack2, 2, true);
					else moveToMain(stack2, 2);
					if (goRight) { moveToMain(stack3, 3); smallestPeg = 1; }
					else { moveBetweenStacks(stack3, 3, stack2, 2); smallestPeg = 2; }
					break;
				}
			}
			return moves;
		};

			while (unsorted < length)
			{
				r.chargeWork();
				require(sp < length, "Hanoi sort exhausted its input before all items entered the ordered tower.");
				targetIndex = sp;
				target = array[sp];
				std::uint64_t moves = hanoi(2, true, 1);
				const std::size_t height = heightForMoves(moves + 1);
				targetMoves = moves;
				const bool evenHeight = height % 2 == 0;
				if (evenHeight) hanoi(1, true, 2);
				unsorted += moveFromMain(stack2, 2, false);
				hanoi(3, evenHeight, 2);
			}
			std::uint64_t moves = hanoi(2, true, 3);
			const std::size_t height = heightForMoves(moves + 1);
			if (height % 2 == 1)
			{
				targetMoves = moves;
				hanoi(3, true, 2);
			}
			require(stack2.empty() && stack3.empty() && sp == 0,
				"Hanoi return phase must empty both pegs and restore the main stack.");
			r.writeMainRange(0, array);
			r.resizeAuxiliary("hanoi-main-stack", 0);
		}
	}
}
