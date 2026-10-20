#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::require;

		void simplifiedLibrarySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "simplified_library_rebalances", "simplified_library_gap_inserts",
				"simplified_library_binary_comparisons", "simplified_library_small_fallbacks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const std::size_t rebalanceFactor = 4;
			std::size_t minimumRun = count;
			while (minimumRun >= 32) minimumRun = (minimumRun - 1) / rebalanceFactor + 1;
			strategies::binaryInsertionRange(r, 0, minimumRun);
			if (count < 32)
			{
				r.addMetric("simplified_library_small_fallbacks");
				return;
			}

			std::size_t maxLevel = minimumRun;
			while (maxLevel * rebalanceFactor < count) maxLevel *= rebalanceFactor;
			r.resizeAuxiliary("simplified-library-items", count);
			r.resizeCounters("simplified-library-counts", maxLevel + 2);
			r.resizeCounters("simplified-library-locations", count - maxLevel);

			const auto rebalance = [&r](std::size_t sortedCount, std::size_t prefixEnd)
			{
				// Convert per-rank insertion counts to the dense boundaries used to
				// redistribute books; then place pending items before their insertion
				// successor, matching the source's stable upper-bound convention.
				for (std::size_t i = 0; i < sortedCount; ++i)
				{
					r.chargeWork();
					const int cumulative = r.counter("simplified-library-counts", i + 1) +
						r.counter("simplified-library-counts", i) + 1;
					r.writeCounter("simplified-library-counts", i + 1, cumulative);
				}
				std::vector<SortItem> dense(prefixEnd);
				std::vector<bool> assigned(prefixEnd, false);
				for (std::size_t i = sortedCount; i < prefixEnd; ++i)
				{
					r.chargeWork();
					const std::size_t location = static_cast<std::size_t>(r.counter("simplified-library-locations", i - sortedCount));
					const std::size_t position = static_cast<std::size_t>(r.counter("simplified-library-counts", location));
					require(location <= sortedCount && position < prefixEnd && !assigned[position],
						"Simplified Library Sort produced an invalid pending-item rank.");
					const SortItem item = r.at(i);
					r.writeAuxiliary("simplified-library-items", position, item);
					dense[position] = item;
					assigned[position] = true;
					r.writeCounter("simplified-library-counts", location,
						r.counter("simplified-library-counts", location) + 1);
				}
				for (std::size_t i = 0; i < sortedCount; ++i)
				{
					r.chargeWork();
					const std::size_t position = static_cast<std::size_t>(r.counter("simplified-library-counts", i));
					require(position < prefixEnd && !assigned[position],
						"Simplified Library Sort's rebalance rank overlaps an existing book.");
					const SortItem item = r.at(i);
					r.writeAuxiliary("simplified-library-items", position, item);
					dense[position] = item;
					assigned[position] = true;
					r.writeCounter("simplified-library-counts", i,
						r.counter("simplified-library-counts", i) + 1);
				}
				for (std::size_t i = 0; i < assigned.size(); ++i)
					require(assigned[i], "Simplified Library Sort left a hole in its rebuilt prefix.");
				r.writeMainRange(0, dense);

				const std::size_t firstEnd = static_cast<std::size_t>(r.counter("simplified-library-counts", 0));
				require(firstEnd <= prefixEnd, "Simplified Library Sort's first rebuilt shelf exceeds its prefix.");
				strategies::binaryInsertionRange(r, 0, firstEnd);
				for (std::size_t i = 0; i + 1 < sortedCount; ++i)
				{
					const std::size_t begin = static_cast<std::size_t>(r.counter("simplified-library-counts", i));
					const std::size_t end = static_cast<std::size_t>(r.counter("simplified-library-counts", i + 1));
					require(begin <= end && end <= prefixEnd, "Simplified Library Sort's rebuilt shelf bounds are invalid.");
					strategies::binaryInsertionRange(r, begin, end);
				}
				const std::size_t lastBegin = static_cast<std::size_t>(r.counter("simplified-library-counts", sortedCount - 1));
				const std::size_t lastEnd = static_cast<std::size_t>(r.counter("simplified-library-counts", sortedCount));
				require(lastBegin <= lastEnd && lastEnd <= prefixEnd, "Simplified Library Sort's final shelf bounds are invalid.");
				strategies::binaryInsertionRange(r, lastBegin, lastEnd);
				for (std::size_t i = 0; i < sortedCount + 2; ++i)
					r.writeCounter("simplified-library-counts", i, 0);
				r.addMetric("simplified_library_rebalances");
			};

			std::size_t insertionIndex = minimumRun, pendingCount = 0;
			for (std::size_t i = minimumRun; i < count; ++i)
			{
				r.chargeWork();
				if (minimumRun <= count / rebalanceFactor && minimumRun * rebalanceFactor == i)
				{
					rebalance(minimumRun, i);
					minimumRun = i;
					pendingCount = 0;
				}
				const SortItem key = r.at(i);
				std::size_t low = 0, high = minimumRun;
				while (low < high)
				{
					const std::size_t middle = low + (high - low) / 2;
					const SortItem candidate = r.at(middle);
					r.compare(key, candidate, i, middle, "main", "main", SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("simplified_library_binary_comparisons");
					if (compareItems(key, candidate, r.trace.direction) == ComparisonResult::Less) high = middle;
					else low = middle + 1;
				}
				r.writeCounter("simplified-library-counts", low + 1,
					r.counter("simplified-library-counts", low + 1) + 1);
				r.writeCounter("simplified-library-locations", pendingCount++, static_cast<int>(low));
				++insertionIndex;
				r.addMetric("simplified_library_gap_inserts");
			}
			(void)insertionIndex;
			rebalance(minimumRun, count);
			r.resizeAuxiliary("simplified-library-items", 0);
			r.resizeCounters("simplified-library-counts", 0);
			r.resizeCounters("simplified-library-locations", 0);
		}

		void librarySort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "library_gap_inserts", "library_block_search_comparisons",
				"library_local_search_comparisons", "library_tie_choices", "library_spills",
				"library_rebalances", "library_prefix_retrievals", "library_small_fallbacks" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const std::size_t blockWidth = 16;
			const std::size_t rebalanceFactor = 4;
			std::size_t sortedPrefix = count;
			while (sortedPrefix >= 32) sortedPrefix = (sortedPrefix - 1) / rebalanceFactor + 1;
			strategies::binaryInsertionRange(r, 0, sortedPrefix);
			if (count < 32)
			{
				r.addMetric("library_small_fallbacks");
				return;
			}

			struct ShelfValue
			{
				SortItem item;
				std::size_t cell;
				ShelfValue(const SortItem& value, std::size_t position) : item(value), cell(position) {}
			};
			struct ShelfBlock { std::vector<ShelfValue> values; };
			const std::string laneName("library-shelves");
			const std::size_t blockCount = count + 1;
			const std::size_t workspaceSize = blockCount * blockWidth;
			std::vector<ShelfBlock> blocks(blockCount);
			std::size_t activeBlocks = 0;
			std::mt19937 tieRandom(static_cast<std::uint32_t>(r.trace.inputSeed ^ 0x4C494252u));
			r.resizeAuxiliary(laneName, workspaceSize);

			const auto blockStart = [&blockWidth](std::size_t block) { return block * blockWidth; };
			const auto findFreeCell = [&blocks, &blockWidth, &blockStart](std::size_t block)
			{
				for (std::size_t offset = 0; offset < blockWidth; ++offset)
				{
					bool occupied = false;
					const std::size_t cell = blockStart(block) + offset;
					for (std::size_t i = 0; i < blocks[block].values.size(); ++i)
						if (blocks[block].values[i].cell == cell) { occupied = true; break; }
					if (!occupied) return cell;
				}
				return NoGapIndex;
			};
			const auto swapCells = [&r, &laneName](std::size_t first, std::size_t second)
			{
				if (first == second) return;
				SemanticEvent event;
				event.type = SemanticEventType::Swap;
				event.firstIndex = first;
				event.secondIndex = second;
				event.firstLane = laneName;
				event.secondLane = laneName;
				event.firstRole = SemanticRole::AuxiliaryCopy;
				event.secondRole = SemanticRole::WriteTarget;
				const std::vector<SortSlot>& cells = r.state.auxiliaryItems.find(laneName)->second;
				if (cells[first].occupied) event.firstItemId = cells[first].item.stableId;
				if (cells[second].occupied) event.secondItemId = cells[second].item.stableId;
				r.append(event);
			};
			const auto normalizeBlock = [&blocks, &blockWidth, &blockStart, &swapCells](std::size_t block)
			{
				std::vector<ShelfValue>& values = blocks[block].values;
				const std::size_t base = blockStart(block);
				for (std::size_t index = 0; index < values.size(); ++index)
				{
					const std::size_t target = base + ((index + 1) * blockWidth) / (values.size() + 1);
					if (values[index].cell == target) continue;
					std::size_t occupant = values.size();
					for (std::size_t other = 0; other < values.size(); ++other)
						if (values[other].cell == target) { occupant = other; break; }
					const std::size_t previous = values[index].cell;
					swapCells(previous, target);
					values[index].cell = target;
					if (occupant < values.size()) values[occupant].cell = previous;
				}
			};
			const auto recordInsert = [&r, &blocks, &laneName, &findFreeCell, &normalizeBlock]
				(std::size_t block, const SortItem& item, std::size_t position)
			{
				const std::size_t cell = findFreeCell(block);
				require(cell != NoGapIndex, "Library Sort attempted to insert into a full shelf block.");
				r.writeAuxiliary(laneName, cell, item);
				blocks[block].values.insert(blocks[block].values.begin() + static_cast<std::ptrdiff_t>(position), ShelfValue(item, cell));
				normalizeBlock(block);
			};
			const auto moveLargestRight = [&r, &blocks, &laneName, &findFreeCell, &normalizeBlock, &swapCells]
				(std::size_t source, std::size_t destination)
			{
				require(!blocks[source].values.empty() && blocks[destination].values.size() < 16,
					"Library Sort boundary spill requires a populated source and an open destination slot.");
				const ShelfValue item = blocks[source].values.back();
				const std::size_t destinationCell = findFreeCell(destination);
				require(destinationCell != NoGapIndex, "Library Sort spill destination unexpectedly has no gap.");
				swapCells(item.cell, destinationCell);
				blocks[source].values.pop_back();
				blocks[destination].values.insert(blocks[destination].values.begin(), ShelfValue(item.item, destinationCell));
				normalizeBlock(source);
				normalizeBlock(destination);
				r.addMetric("library_spills");
			};
			const auto makeRoom = [&blocks, &activeBlocks, &blockCount, &moveLargestRight](std::size_t target)
			{
				if (blocks[target].values.size() < 16) return;
				std::size_t freeBlock = target + 1;
				while (freeBlock < blockCount && blocks[freeBlock].values.size() >= 16) ++freeBlock;
				require(freeBlock < blockCount, "Library Sort exhausted its extra gap blocks.");
				for (std::size_t cursor = freeBlock; cursor > target; --cursor)
					moveLargestRight(cursor - 1, cursor);
				activeBlocks = std::max(activeBlocks, freeBlock + 1);
			};

			for (std::size_t i = 0; i < sortedPrefix; ++i)
			{
				const std::size_t cell = blockStart(i) + blockWidth - 1;
				const SortItem item = r.at(i);
				r.writeAuxiliary(laneName, cell, item);
				blocks[i].values.push_back(ShelfValue(item, cell));
			}
			activeBlocks = sortedPrefix;

			const auto retrieve = [&r, &blocks, &activeBlocks, &sortedPrefix, &laneName,
				&blockWidth, &blockCount, &blockStart, &workspaceSize](std::size_t prefixEnd, bool rebuild)
			{
				std::vector<SortItem> sorted;
				sorted.reserve(prefixEnd);
				for (std::size_t block = 0; block < activeBlocks; ++block)
					for (std::size_t item = 0; item < blocks[block].values.size(); ++item)
						sorted.push_back(blocks[block].values[item].item);
				require(sorted.size() == prefixEnd, "Library Sort shelf count does not match its sorted prefix.");
				r.writeMainRange(0, sorted);
				r.addMetric("library_prefix_retrievals");
				r.resizeAuxiliary(laneName, 0);
				if (!rebuild) return;
				r.resizeAuxiliary(laneName, workspaceSize);
				for (std::size_t block = 0; block < blockCount; ++block) blocks[block].values.clear();
				for (std::size_t i = 0; i < sorted.size(); ++i)
				{
					const std::size_t cell = blockStart(i) + blockWidth - 1;
					r.writeAuxiliary(laneName, cell, sorted[i]);
					blocks[i].values.push_back(ShelfValue(sorted[i], cell));
				}
				activeBlocks = sorted.size();
				sortedPrefix = prefixEnd;
				r.addMetric("library_rebalances");
			};
			const auto selectShelf = [&r, &blocks, &activeBlocks, &tieRandom, &laneName]
				(const SortItem& key, std::size_t keyIndex)
			{
				std::size_t block = 0, high = activeBlocks;
				while (block < high)
				{
					const std::size_t middle = block + (high - block) / 2;
					const ShelfValue& maximum = blocks[middle].values.back();
					r.compare(key, maximum.item, keyIndex, maximum.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("library_block_search_comparisons");
					if (compareItems(key, maximum.item, r.trace.direction) != ComparisonResult::Greater) high = middle;
					else block = middle + 1;
				}
				if (block == activeBlocks) block = activeBlocks - 1;

				const auto equalsShelfMaximum = [&r, &blocks, &laneName, &key, keyIndex](std::size_t candidateBlock)
				{
					const ShelfValue& maximum = blocks[candidateBlock].values.back();
					r.compare(key, maximum.item, keyIndex, maximum.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					return compareItems(key, maximum.item, r.trace.direction) == ComparisonResult::Equal;
				};
				if (equalsShelfMaximum(block))
				{
					std::size_t firstEqual = block, lastEqual = block + 1;
					while (firstEqual > 0 && equalsShelfMaximum(firstEqual - 1)) --firstEqual;
					while (lastEqual < activeBlocks && equalsShelfMaximum(lastEqual)) ++lastEqual;
					if (lastEqual - firstEqual > 1)
					{
						std::uniform_int_distribution<std::size_t> chooseBlock(firstEqual, lastEqual - 1);
						block = chooseBlock(tieRandom);
						r.addMetric("library_tie_choices");
					}
				}
				return block;
			};

			std::size_t nextRebalance = sortedPrefix * rebalanceFactor;
			for (std::size_t i = sortedPrefix; i < count; ++i)
			{
				r.chargeWork();
				if (i == nextRebalance)
				{
					retrieve(i, true);
					nextRebalance *= rebalanceFactor;
				}
				const SortItem key = r.at(i);
				std::size_t block = 0;
				for (;;)
				{
					block = selectShelf(key, i);
					if (blocks[block].values.size() < blockWidth) break;
					makeRoom(block);
				}
				std::size_t low = 0, upper = blocks[block].values.size();
				while (low < upper)
				{
					const std::size_t middle = low + (upper - low) / 2;
					const ShelfValue& candidate = blocks[block].values[middle];
					r.compare(key, candidate.item, i, candidate.cell, "main", laneName,
						SemanticRole::ProbedItem, SemanticRole::ComparedItem);
					r.addMetric("library_local_search_comparisons");
					if (compareItems(key, candidate.item, r.trace.direction) == ComparisonResult::Less) upper = middle;
					else low = middle + 1;
				}
				recordInsert(block, key, low);
				r.addMetric("library_gap_inserts");
				activeBlocks = std::max(activeBlocks, block + 1);
			}
			retrieve(count, false);
			r.resizeAuxiliary(laneName, 0);
		}
	}
}
