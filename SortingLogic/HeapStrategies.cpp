#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void heapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			const auto sift = [&r, &later](std::size_t root, std::size_t end)
			{
				while (root * 2 + 1 < end)
				{
					std::size_t child = root * 2 + 1;
					if (child + 1 < end)
					{
						const SortItem a = r.at(child), b = r.at(child + 1);
						r.compare(a, b, child, child + 1);
						if (later(b, a)) ++child;
					}
					const SortItem parent = r.at(root), candidate = r.at(child);
					r.compare(parent, candidate, root, child);
					if (!later(candidate, parent)) return;
					r.swap(root, child);
					root = child;
				}
			};
			for (std::size_t start = n / 2; start > 0; --start) sift(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(0, end - 1);
				sift(0, end - 1);
			}
		}

		void arityHeapSort(Recorder& r, std::size_t arity, bool minHeap, bool flipped)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			arity = std::max<std::size_t>(2, std::min<std::size_t>(arity, 16));
			const auto physical = [n, flipped](std::size_t index) { return flipped ? n - 1 - index : index; };
			const auto better = [&r, minHeap](const SortItem& a, const SortItem& b)
			{
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return minHeap ? order == ComparisonResult::Less : order == ComparisonResult::Greater;
			};
			const auto siftDown = [&r, arity, physical, better](std::size_t root, std::size_t end)
			{
				while (root <= (end - 1) / arity)
				{
					const std::size_t firstChild = root * arity + 1;
					if (firstChild >= end) return;
					std::size_t selected = firstChild;
					for (std::size_t child = firstChild + 1; child < end && child < firstChild + arity; ++child)
					{
						const SortItem a = r.at(physical(child)), b = r.at(physical(selected));
						r.compare(a, b, physical(child), physical(selected));
						if (better(a, b)) selected = child;
					}
					const SortItem parent = r.at(physical(root)), candidate = r.at(physical(selected));
					r.compare(parent, candidate, physical(root), physical(selected));
					if (!better(candidate, parent)) return;
					r.swap(physical(root), physical(selected));
					root = selected;
				}
			};
			for (std::size_t start = (n - 2) / arity + 1; start > 0; --start) siftDown(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(physical(0), physical(end - 1));
				siftDown(0, end - 1);
			}
			if (minHeap && !flipped)
				for (std::size_t left = 0, right = n - 1; left < right; ++left, --right) r.swap(left, right);
		}

		void bottomUpSiftDown(Recorder& r, std::size_t root, std::size_t end)
		{
			if (root >= end || root * 2 + 1 >= end) return;
			std::vector<std::size_t> path(1, root);
			std::size_t node = root;
			while (node * 2 + 1 < end)
			{
				std::size_t child = node * 2 + 1;
				if (child + 1 < end)
				{
					const SortItem a = r.at(child), b = r.at(child + 1);
					r.compare(a, b, child, child + 1);
					if (compareItems(b, a, r.trace.direction) == ComparisonResult::Greater) ++child;
				}
				node = child;
				path.push_back(node);
			}
			const SortItem held = r.at(root);
			r.hold(root);
			for (std::size_t depth = 1; depth < path.size(); ++depth) r.shift(path[depth], path[depth - 1]);
			std::size_t position = path.back();
			while (position != root)
			{
				const std::size_t parent = (position - 1) / 2;
				const SortItem candidate = r.at(parent);
				r.compare(held, candidate, root, parent, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				if (compareItems(held, candidate, r.trace.direction) != ComparisonResult::Greater) break;
				r.shift(parent, position);
				position = parent;
			}
			r.place(position, held);
		}

		void bottomUpHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			const auto siftBuild = [&r, &later](std::size_t root, std::size_t end)
			{
				while (root * 2 + 1 < end)
				{
					std::size_t child = root * 2 + 1;
					if (child + 1 < end)
					{
						const SortItem a = r.at(child), b = r.at(child + 1);
						r.compare(a, b, child, child + 1);
						if (later(b, a)) ++child;
					}
					const SortItem parent = r.at(root), candidate = r.at(child);
					r.compare(parent, candidate, root, child);
					if (!later(candidate, parent)) return;
					r.swap(root, child);
					root = child;
				}
			};
			for (std::size_t start = n / 2; start > 0; --start) siftBuild(start - 1, n);
			for (std::size_t end = n; end > 1; --end)
			{
				r.swap(0, end - 1);
				bottomUpSiftDown(r, 0, end - 1);
			}
		}

		void binomialHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			const auto later = [&r](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
			for (std::size_t index = 2; index <= n; index += 2)
			{
				std::size_t maxNode = index;
				do
				{
					const std::size_t focus = maxNode;
					for (std::size_t depth = 1; (focus & depth) == 0 && depth <= focus; depth *= 2)
					{
						const std::size_t left = focus - depth - 1;
						const std::size_t right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus - depth;
					}
					if (focus != maxNode) r.swap(focus - 1, maxNode - 1);
					if (focus == maxNode) break;
				} while (true);
			}
			for (std::size_t index = n; index > 2; --index)
			{
				std::size_t maxNode = index;
				std::size_t focus = index;
				for (std::size_t depth = 1; focus != 0 && depth <= index; depth *= 2)
				{
					if ((focus & depth) != 0)
					{
						const std::size_t left = focus - 1, right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus;
						focus -= depth;
					}
				}
				if (maxNode == index) continue;
				focus = index;
				do
				{
					r.swap(focus - 1, maxNode - 1);
					focus = maxNode;
					for (std::size_t depth = 1; (focus & depth) == 0 && depth <= focus; depth *= 2)
					{
						const std::size_t left = focus - depth - 1, right = maxNode - 1;
						const SortItem a = r.at(left), b = r.at(right);
						r.compare(a, b, left, right);
						if (later(a, b)) maxNode = focus - depth;
					}
				} while (focus != maxNode);
			}
		}

		void lazyHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const std::size_t block = static_cast<std::size_t>(std::sqrt(static_cast<double>(n - 1))) + 1;
			const auto maxToFront = [&r](std::size_t first, std::size_t last)
			{
				if (first >= last) return;
				std::size_t maximum = first;
				for (std::size_t i = first + 1; i < last; ++i)
				{
					const SortItem a = r.at(i), b = r.at(maximum);
					r.compare(a, b, i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
					if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) maximum = i;
				}
				r.swap(maximum, first, SemanticRole::CurrentMaximum);
			};
		for (std::size_t first = 0; first < n; first += block) maxToFront(first, std::min(first + block, n));
		std::size_t remaining = n;
		while (remaining > 0)
		{
			std::size_t maximum = 0;
			for (std::size_t i = block; i < remaining; i += block)
			{
				const SortItem a = r.at(i), b = r.at(maximum);
				r.compare(a, b, i, maximum, "main", "main", SemanticRole::ComparedItem, SemanticRole::CurrentMaximum);
				if (compareItems(a, b, r.trace.direction) != ComparisonResult::Less) maximum = i;
			}
			--remaining;
			r.swap(maximum, remaining, SemanticRole::CurrentMaximum);
			maxToFront(maximum, std::min(maximum + block, remaining));
		}
		}

		void minMaxHeapSort(Recorder& r)
		{
			const std::size_t n = r.state.slots.size();
			if (n < 2) return;
			const auto isMinLevel = [](std::size_t index)
			{
				std::size_t value = index + 1, bits = 0;
				while (value) { ++bits; value >>= 1; }
				return (bits & 1u) != 0;
			};
			const auto precedes = [&r](const SortItem& a, const SortItem& b, bool minimum)
			{
				const ComparisonResult order = compareItems(a, b, r.trace.direction);
				return minimum ? order == ComparisonResult::Less : order == ComparisonResult::Greater;
			};
			std::size_t end = n;
			const auto downheap = [&r, &isMinLevel, &precedes, &end](std::size_t start)
			{
				std::size_t node = start;
				const bool minimum = isMinLevel(node);
				while (node * 2 + 1 < end)
				{
					const std::size_t left = node * 2 + 1, right = left + 1;
					std::size_t candidate = left;
					const std::size_t descendants[5] = { right, left * 2 + 1, left * 2 + 2, right * 2 + 1, right * 2 + 2 };
					for (std::size_t d = 0; d < 5; ++d)
					{
						const std::size_t child = descendants[d];
						if (child >= end) break;
						const SortItem a = r.at(child), b = r.at(candidate);
						r.compare(a, b, child, candidate, "main", "main",
							minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum,
							minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum);
						if (precedes(a, b, minimum)) candidate = child;
					}
					if (!precedes(r.at(candidate), r.at(node), minimum)) return;
					r.swap(candidate, node, minimum ? SemanticRole::CurrentMinimum : SemanticRole::CurrentMaximum);
					if (candidate <= right) return;
					const std::size_t parent = (candidate - 1) / 2;
					if (precedes(r.at(parent), r.at(candidate), minimum)) r.swap(candidate, parent);
					node = candidate;
				}
			};
		for (std::size_t i = (n - 1) / 2 + 1; i > 0; --i) downheap(i - 1);
		for (std::size_t remaining = n; remaining > 1; --remaining)
		{
			std::size_t maximum = 1;
			if (remaining > 2)
			{
				const SortItem a = r.at(1), b = r.at(2);
				r.compare(a, b, 1, 2, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::CurrentMaximum);
				if (precedes(b, a, false)) maximum = 2;
			}
			end = remaining - 1;
			r.swap(maximum, end, SemanticRole::CurrentMaximum);
			if (maximum < end) downheap(maximum);
		}
	}

	void outOfPlaceHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		if (n < 2) return;
		const std::string lane = "out_of_place_heap";
		r.resizeAuxiliary(lane, n);
		for (std::size_t i = 0; i < n; ++i) r.writeAuxiliary(lane, i, r.at(i));
		const auto later = [&r](const SortItem& a, const SortItem& b)
			{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
		const auto sift = [&r, &lane, &later](std::size_t root, std::size_t end)
		{
			while (root * 2 + 1 < end)
			{
				std::size_t child = root * 2 + 1;
				if (child + 1 < end)
				{
					const SortItem a = r.auxiliaryAt(lane, child), b = r.auxiliaryAt(lane, child + 1);
					r.compare(a, b, child, child + 1, lane, lane);
					if (later(b, a)) ++child;
				}
				const SortItem parent = r.auxiliaryAt(lane, root), candidate = r.auxiliaryAt(lane, child);
				r.compare(parent, candidate, root, child, lane, lane);
				if (!later(candidate, parent)) return;
				r.swapAuxiliary(lane, root, child);
				root = child;
			}
		};
		for (std::size_t start = n / 2; start > 0; --start) sift(start - 1, n);
		for (std::size_t end = n; end > 1; --end)
		{
			r.swapAuxiliary(lane, 0, end - 1);
			sift(0, end - 1);
		}
		std::vector<SortItem> output;
		output.reserve(n);
		for (std::size_t i = 0; i < n; ++i) output.push_back(r.auxiliaryAt(lane, i));
		r.writeMainRange(0, output);
		r.resizeAuxiliary(lane, 0);
	}

	void triangularHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		const auto triangularRoot = [](std::size_t value)
		{
			return static_cast<std::size_t>((std::sqrt(static_cast<double>(8 * value + 1)) - 1.0) / 2.0);
		};
		const auto siftDown = [&r, &triangularRoot](std::size_t end, std::size_t start)
		{
			if (start >= end) return;
			const SortItem held = r.at(start);
			r.hold(start);
			std::size_t root = start;
			std::size_t level = triangularRoot(root);
			std::size_t left = root + level + 1, right = left + 1;
			while (left < end)
			{
				std::size_t maximum = left;
				if (right < end)
				{
					const SortItem a = r.at(left), b = r.at(right);
					r.compare(a, b, left, right, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::CurrentMaximum);
					if (compareItems(b, a, r.trace.direction) == ComparisonResult::Greater) maximum = right;
				}
				const SortItem candidate = r.at(maximum);
				r.compare(candidate, held, maximum, start, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::HeldKey);
				if (compareItems(candidate, held, r.trace.direction) != ComparisonResult::Greater) break;
				r.shift(maximum, root);
				root = maximum;
				level = triangularRoot(root);
				left = root + level + 1;
				right = left + 1;
			}
			r.place(root, held);
		};
		for (std::size_t i = n; i > 0; --i) siftDown(n, i - 1);
		for (std::size_t pass = 1; pass + 1 < n; ++pass)
		{
			const std::size_t end = n - pass;
			r.swap(0, end, SemanticRole::CurrentMaximum);
			siftDown(end, 0);
		}
		if (n > 1)
		{
			const SortItem a = r.at(0), b = r.at(1);
			r.compare(a, b, 0, 1, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
			if (compareItems(a, b, r.trace.direction) == ComparisonResult::Greater) r.swap(0, 1);
		}
	}

	void weakHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		if (n < 2) return;
		const std::string lane = "weak_heap_bits";
		r.resizeCounters(lane, (n + 7) / 8);
		const auto flag = [&r, &lane](std::size_t index)
			{ return (r.counter(lane, index >> 3) >> (index & 7)) & 1; };
		const auto toggle = [&r, &lane, &flag](std::size_t index)
		{
			const std::size_t byte = index >> 3;
			r.writeCounter(lane, byte, r.counter(lane, byte) ^ (1 << (index & 7)));
		};
		const auto merge = [&r, &toggle](std::size_t parent, std::size_t child)
		{
			const SortItem a = r.at(parent), b = r.at(child);
			r.compare(a, b, parent, child, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
			if (compareItems(a, b, r.trace.direction) == ComparisonResult::Less)
			{
				toggle(child);
				r.swap(parent, child, SemanticRole::CurrentMaximum);
			}
		};
		for (std::size_t i = n; i > 1; --i)
		{
			std::size_t node = i - 1;
			while ((node & 1u) == static_cast<std::size_t>(flag(node >> 1))) node >>= 1;
			merge(node >> 1, i - 1);
		}
		for (std::size_t end = n; end > 2; --end)
		{
			const std::size_t last = end - 1;
			r.swap(0, last, SemanticRole::CurrentMaximum);
			std::size_t node = 1, child = 2 * node + static_cast<std::size_t>(flag(node));
			while (child < last)
			{
				node = child;
				child = 2 * node + static_cast<std::size_t>(flag(node));
			}
			while (node > 0)
			{
				merge(0, node);
				node >>= 1;
			}
		}
		r.swap(0, 1, SemanticRole::CurrentMaximum);
	}

	void poplarHeapSort(Recorder& r)
	{
		const std::size_t n = r.state.slots.size();
		const auto later = [&r](const SortItem& a, const SortItem& b)
			{ return compareItems(a, b, r.trace.direction) == ComparisonResult::Greater; };
		const auto insertionSort = [&r](std::size_t first, std::size_t last)
		{
			for (std::size_t current = first + 1; current < last; ++current)
			{
				const SortItem held = r.at(current);
				const SortItem previous = r.at(current - 1);
				r.compare(held, previous, current, current - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
				if (compareItems(held, previous, r.trace.direction) != ComparisonResult::Less) continue;
				r.hold(current);
				std::size_t position = current;
				do
				{
					r.shift(position - 1, position);
					--position;
					if (position == first) break;
					const SortItem candidate = r.at(position - 1);
					r.compare(held, candidate, current, position - 1, "main", "main", SemanticRole::HeldKey, SemanticRole::ComparedItem);
					if (compareItems(held, candidate, r.trace.direction) != ComparisonResult::Less) break;
				} while (true);
				r.place(position, held);
			}
		};
		const auto sift = [&r, &later](std::size_t first, std::size_t size)
		{
			if (size < 2) return;
			std::size_t root = first + size - 1;
			std::size_t childRoot1 = root - 1;
			std::size_t childRoot2 = first + (size / 2 - 1);
			while (true)
			{
				std::size_t maximum = root;
				SortItem a = r.at(maximum), b = r.at(childRoot1);
				r.compare(a, b, maximum, childRoot1, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) maximum = childRoot1;
				a = r.at(maximum); b = r.at(childRoot2);
				r.compare(a, b, maximum, childRoot2, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) maximum = childRoot2;
				if (maximum == root) return;
				r.swap(root, maximum, SemanticRole::CurrentMaximum);
				size /= 2;
				if (size < 2) return;
				root = maximum;
				childRoot1 = root - 1;
				childRoot2 = maximum - (size - size / 2);
			}
		};
		const auto hyperfloor = [](std::size_t value)
		{
			std::size_t power = 1;
			while (power <= value / 2) power *= 2;
			return power;
		};
		const auto popHeapWithSize = [&r, &later, &sift, &hyperfloor](std::size_t first, std::size_t last, std::size_t size)
		{
			std::size_t poplarSize = hyperfloor(size + 1) - 1;
			const std::size_t lastRoot = last - 1;
			std::size_t bigger = lastRoot;
			std::size_t biggerSize = poplarSize;
			std::size_t position = first;
			std::size_t remaining = size;
			while (true)
			{
				const std::size_t root = position + poplarSize - 1;
				if (root == lastRoot) break;
				const SortItem a = r.at(bigger), b = r.at(root);
				r.compare(a, b, bigger, root, "main", "main", SemanticRole::CurrentMaximum, SemanticRole::ComparedItem);
				if (later(b, a)) { bigger = root; biggerSize = poplarSize; }
				position = root + 1;
				remaining -= poplarSize;
				poplarSize = hyperfloor(remaining + 1) - 1;
			}
			if (bigger != lastRoot)
			{
				r.swap(bigger, lastRoot, SemanticRole::CurrentMaximum);
				sift(bigger - (biggerSize - 1), biggerSize);
			}
		};
		if (n <= 15)
		{
			insertionSort(0, n);
			return;
		}
		const std::size_t smallPoplarSize = 15;
		std::size_t level = 1, first = 0, next = smallPoplarSize;
		while (true)
		{
			insertionSort(first, next);
			std::size_t poplarSize = smallPoplarSize;
			for (std::size_t carry = ((level & (~level + 1)) >> 1); carry != 0; carry >>= 1)
			{
				first -= poplarSize;
				poplarSize = 2 * poplarSize + 1;
				sift(first, poplarSize);
				++next;
			}
			if (n - next <= smallPoplarSize)
			{
				insertionSort(next, n);
				break;
			}
			first = next;
			next += smallPoplarSize;
			++level;
		}
		std::size_t size = n;
		while (size > 1)
		{
			popHeapWithSize(0, size, size);
			--size;
		}
	}


		void ternaryHeapSort(Recorder& recorder)
		{
			arityHeapSort(recorder, 3, false, false);
		}

		void baseNHeapSort(Recorder& recorder)
		{
			arityHeapSort(recorder, static_cast<std::size_t>(recorder.trace.configuration.heapArity), false, false);
		}

		void minHeapSort(Recorder& recorder)
		{
			arityHeapSort(recorder, 2, true, false);
		}

		void flippedMinHeapSort(Recorder& recorder)
		{
			arityHeapSort(recorder, 2, true, true);
		}

	}
}
