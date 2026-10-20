#include "AlgorithmStrategies.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;

		void smoothSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "smooth_sift_calls", "smooth_trinkle_calls", "smooth_heap_comparisons",
				"smooth_heap_moves", "smooth_heap_insertions", "smooth_heap_extractions", "smooth_max_leonardo_order" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;

			std::vector<std::size_t> leonardo(2, 1);
			while (leonardo.back() < count)
			{
				r.chargeWork();
				leonardo.push_back(leonardo[leonardo.size() - 1] + leonardo[leonardo.size() - 2] + 1);
			}
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			const auto compareItemsAt = [&r](const SortItem& first, const SortItem& second, std::size_t firstIndex, std::size_t secondIndex)
			{
				r.compare(first, second, firstIndex, secondIndex, "main", "main", SemanticRole::ComparedItem, SemanticRole::ComparedItem);
				r.addMetric("smooth_heap_comparisons");
			};
			const auto sift = [&r, &later, &compareItemsAt, &leonardo](std::size_t order, std::size_t root)
			{
				r.addMetric("smooth_sift_calls");
				const SortItem held = r.at(root);
				r.hold(root);
				while (order > 1)
				{
					r.chargeWork();
					const std::size_t right = root - 1;
					const std::size_t left = root - 1 - leonardo[order - 2];
					compareItemsAt(held, r.at(left), root, left);
					const bool heldAboveLeft = !later(r.at(left), held);
					compareItemsAt(held, r.at(right), root, right);
					const bool heldAboveRight = !later(r.at(right), held);
					if (heldAboveLeft && heldAboveRight) break;
					compareItemsAt(r.at(left), r.at(right), left, right);
					if (!later(r.at(right), r.at(left)))
					{
						r.shift(left, root);
						root = left;
						--order;
					}
					else
					{
						r.shift(right, root);
						root = right;
						order -= 2;
					}
					r.addMetric("smooth_heap_moves");
				}
				r.place(root, held);
			};
			const auto trinkle = [&r, &later, &compareItemsAt, &leonardo, &sift](std::size_t forest, std::size_t order,
				std::size_t root, bool trusty)
			{
				r.addMetric("smooth_trinkle_calls");
				const SortItem held = r.at(root);
				r.hold(root);
				while (forest != 1)
				{
					r.chargeWork();
					const std::size_t ancestor = root - leonardo[order];
					compareItemsAt(r.at(ancestor), held, ancestor, root);
					if (!later(r.at(ancestor), held)) break;
					if (!trusty && order > 1)
					{
						const std::size_t right = root - 1;
						const std::size_t left = root - 1 - leonardo[order - 2];
						compareItemsAt(r.at(right), r.at(ancestor), right, ancestor);
						const bool rightBlocks = later(r.at(right), r.at(ancestor));
						compareItemsAt(r.at(left), r.at(ancestor), left, ancestor);
						const bool leftBlocks = later(r.at(left), r.at(ancestor));
						if (rightBlocks || leftBlocks) break;
					}
					r.shift(ancestor, root);
					root = ancestor;
					std::size_t trailing = 0;
					unsigned int bits = static_cast<unsigned int>(forest & ~1u);
					while ((bits & 1u) == 0u) { ++trailing; bits >>= 1; r.chargeWork(); }
					forest >>= trailing;
					order += trailing;
					trusty = false;
				r.addMetric("smooth_heap_moves");
				}
				r.place(root, held);
				if (!trusty) sift(order, root);
			};

			int buildPhase = r.beginPhase("Build Leonardo heap forest");
			std::size_t root = 0;
			std::size_t forest = 1;
			std::size_t order = 1;
			while (root < count - 1)
			{
				r.chargeWork();
				if ((forest & 3u) == 3u)
				{
					sift(order, root);
					forest >>= 2;
					order += 2;
				}
				else
				{
					if (order == 0 || order >= leonardo.size()) throw std::logic_error("Smooth Sort generated an invalid Leonardo order.");
					if (leonardo[order - 1] >= count - 1 - root)
						trinkle(forest, order, root, false);
					else sift(order, root);
					if (order == 1) { forest <<= 1; --order; }
					else { forest <<= (order - 1); order = 1; }
				}
				forest |= 1u;
				++root;
				r.addMetric("smooth_heap_insertions");
				r.setMetric("smooth_max_leonardo_order", std::max<std::uint64_t>(r.state.metrics.algorithm["smooth_max_leonardo_order"], order));
			}
			r.endPhase(buildPhase, "Leonardo heap forest built");

			int extractionPhase = r.beginPhase("Drain Leonardo heap forest");
			trinkle(forest, order, root, false);
			while (order != 1 || forest != 1)
			{
				r.chargeWork();
				if (order <= 1)
				{
					std::size_t trailing = 0;
					unsigned int bits = static_cast<unsigned int>(forest & ~1u);
					while ((bits & 1u) == 0u) { ++trailing; bits >>= 1; r.chargeWork(); }
					forest >>= trailing;
					order += trailing;
				}
				else
				{
					forest = (forest << 2) ^ 7u;
					order -= 2;
					if (order + 1 >= leonardo.size() || leonardo[order] + 1 > root)
						throw std::logic_error("Smooth Sort split exceeded its Leonardo forest bounds.");
					trinkle(forest >> 1, order + 1, root - leonardo[order] - 1, true);
					trinkle(forest, order, root - 1, true);
				}
				--root;
				r.addMetric("smooth_heap_extractions");
			}
			r.endPhase(extractionPhase, "Leonardo heap forest drained");
		}

		void binomialSmoothSort(Recorder& r)
		{
			const std::size_t count = r.state.slots.size();
			const char* metricKeys[] = { "binomial_smooth_thrift_calls", "binomial_smooth_comparisons",
				"binomial_smooth_swaps", "binomial_smooth_insertions", "binomial_smooth_heap_visits", "binomial_smooth_max_height" };
			for (std::size_t i = 0; i < sizeof(metricKeys) / sizeof(metricKeys[0]); ++i) r.declareMetric(metricKeys[i]);
			if (count < 2) return;
			const auto heapHeight = [](std::size_t node)
			{
				std::size_t height = 0;
				while ((node >> height) & 1u) ++height;
				return height;
			};
			const auto later = [&r](const SortItem& first, const SortItem& second)
				{ return compareItems(first, second, r.trace.direction) == ComparisonResult::Greater; };
			std::function<void(std::size_t, bool, bool)> thrift;
			thrift = [&r, &heapHeight, &later, &thrift](std::size_t node, bool hasParent, bool isHeapRoot)
			{
				r.chargeWork();
				r.addMetric("binomial_smooth_thrift_calls");
				const std::size_t height = heapHeight(node);
				isHeapRoot = isHeapRoot && node >= (std::size_t(1) << height);
				if (!isHeapRoot && !hasParent) return;
				std::size_t choice = height - (isHeapRoot ? 0u : 1u);
				if (hasParent)
				{
					for (std::ptrdiff_t child = static_cast<std::ptrdiff_t>(choice) - 1; child >= 0; --child)
					{
						const std::size_t first = node - (std::size_t(1) << choice);
						const std::size_t second = node - (std::size_t(1) << static_cast<std::size_t>(child));
						r.compare(r.at(first), r.at(second), first, second);
						r.addMetric("binomial_smooth_comparisons");
						if (!later(r.at(first), r.at(second))) choice = static_cast<std::size_t>(child);
					}
				}
				const std::size_t child = node - (std::size_t(1) << choice);
				r.compare(r.at(child), r.at(node), child, node);
				r.addMetric("binomial_smooth_comparisons");
				r.setMetric("binomial_smooth_max_height", std::max<std::uint64_t>(r.state.metrics.algorithm["binomial_smooth_max_height"], height));
				if (!later(r.at(child), r.at(node))) return;
				r.swap(node, child);
				r.addMetric("binomial_smooth_swaps");
				r.addMetric("binomial_smooth_heap_visits");
				thrift(child, (child & 1u) == 1u, choice == height);
			};

			int buildPhase = r.beginPhase("Grow binomial heap forest");
			for (std::size_t node = 1; node < count; ++node)
			{
				const std::size_t height = heapHeight(node);
				r.setMetric("binomial_smooth_max_height", std::max<std::uint64_t>(r.state.metrics.algorithm["binomial_smooth_max_height"], height));
				thrift(node, (node & 1u) == 1u, node + (std::size_t(1) << height) >= count);
				r.addMetric("binomial_smooth_insertions");
			}
			r.endPhase(buildPhase, "Binomial heap forest built");
			int extractPhase = r.beginPhase("Reheapify binomial forest");
			std::size_t node = count;
			node -= (node - 1) % 2;
			while (node > 2)
			{
				const std::size_t height = heapHeight(node);
				for (std::ptrdiff_t childOrder = static_cast<std::ptrdiff_t>(height) - 1; childOrder >= 0; --childOrder)
				{
					const std::size_t child = node - (std::size_t(1) << static_cast<std::size_t>(childOrder));
					thrift(child, false, true);
					r.addMetric("binomial_smooth_heap_visits");
				}
				node -= 2;
			}
			r.endPhase(extractPhase, "Binomial forest reheapified");
		}


	}
}
