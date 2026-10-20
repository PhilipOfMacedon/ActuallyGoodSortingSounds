#include "AlgorithmStrategies.h"
#include "PermutationTraceUtilities.h"
#include "TraceRecorder.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sortinglogic
{
	namespace strategies
	{
		using detail::Recorder;
		using detail::emitPermutationOrder;

		struct TreeNode
		{
			SortItem item;
			std::size_t originalIndex;
			int left, right, parent, level, height, balance;
			bool red;
			TreeNode(const SortItem& value, std::size_t index)
				: item(value), originalIndex(index), left(-1), right(-1), parent(-1), level(0), height(1), balance(0), red(false) {}
		};

		void setTreeParent(Recorder& r, std::vector<TreeNode>& nodes, int child, int parent)
		{
			if (child < 0 || nodes[static_cast<std::size_t>(child)].parent == parent) return;
			nodes[static_cast<std::size_t>(child)].parent = parent;
			r.writeCounter("tree_parent", static_cast<std::size_t>(child), parent < 0 ? 0 : parent + 1);
		}

		void setTreeChild(Recorder& r, std::vector<TreeNode>& nodes, std::size_t parent, bool left, int child)
		{
			const int oldChild = left ? nodes[parent].left : nodes[parent].right;
			const bool trackParents = r.state.integerWorkspaces.find("tree_parent") != r.state.integerWorkspaces.end();
			if (trackParents && oldChild >= 0 && oldChild != child && nodes[static_cast<std::size_t>(oldChild)].parent == static_cast<int>(parent))
				setTreeParent(r, nodes, oldChild, -1);
			if (left) nodes[parent].left = child;
			else nodes[parent].right = child;
			r.writeCounter(left ? "tree_left" : "tree_right", parent, child < 0 ? 0 : child + 1);
			if (trackParents && child >= 0) setTreeParent(r, nodes, child, static_cast<int>(parent));
		}

		void refreshAVLNode(Recorder& r, std::vector<TreeNode>& nodes, std::size_t index)
		{
			const int leftHeight = nodes[index].left < 0 ? 0 : nodes[static_cast<std::size_t>(nodes[index].left)].height;
			const int rightHeight = nodes[index].right < 0 ? 0 : nodes[static_cast<std::size_t>(nodes[index].right)].height;
			nodes[index].height = 1 + (std::max)(leftHeight, rightHeight);
			nodes[index].balance = leftHeight - rightHeight;
			r.writeCounter("tree_height", index, nodes[index].height);
			r.writeCounter("tree_balance", index, nodes[index].balance);
		}

		int rotateTreeRight(Recorder& r, std::vector<TreeNode>& nodes, int root)
		{
			const int pivot = nodes[static_cast<std::size_t>(root)].left;
			if (pivot < 0) throw std::logic_error("Right rotation requires a left child.");
			const int pivotRight = nodes[static_cast<std::size_t>(pivot)].right;
			setTreeChild(r, nodes, static_cast<std::size_t>(root), true, pivotRight);
			setTreeChild(r, nodes, static_cast<std::size_t>(pivot), false, root);
			if (r.state.integerWorkspaces.find("tree_balance") != r.state.integerWorkspaces.end())
			{
				refreshAVLNode(r, nodes, static_cast<std::size_t>(root));
				refreshAVLNode(r, nodes, static_cast<std::size_t>(pivot));
			}
			r.addMetric("tree_rotations");
			return pivot;
		}

		int rotateTreeLeft(Recorder& r, std::vector<TreeNode>& nodes, int root)
		{
			const int pivot = nodes[static_cast<std::size_t>(root)].right;
			if (pivot < 0) throw std::logic_error("Left rotation requires a right child.");
			const int pivotLeft = nodes[static_cast<std::size_t>(pivot)].left;
			setTreeChild(r, nodes, static_cast<std::size_t>(root), false, pivotLeft);
			setTreeChild(r, nodes, static_cast<std::size_t>(pivot), true, root);
			if (r.state.integerWorkspaces.find("tree_balance") != r.state.integerWorkspaces.end())
			{
				refreshAVLNode(r, nodes, static_cast<std::size_t>(root));
				refreshAVLNode(r, nodes, static_cast<std::size_t>(pivot));
			}
			r.addMetric("tree_rotations");
			return pivot;
		}

		void balancedTreeStorage(Recorder& r, std::size_t count)
		{
			r.resizeAuxiliary("tree_nodes", count);
			r.resizeCounters("tree_left", count);
			r.resizeCounters("tree_right", count);
			r.resizeCounters("tree_root", 1);
		}

		void balancedTreeInsertItems(Recorder& r, std::vector<TreeNode>& nodes, std::size_t count)
		{
			nodes.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				nodes.push_back(TreeNode(r.trace.input[i], i));
				r.writeAuxiliary("tree_nodes", i, r.trace.input[i]);
			}
		}

		std::vector<std::size_t> balancedTreeInOrder(Recorder& r, const std::vector<TreeNode>& nodes, int root)
		{
			std::vector<std::size_t> order, stack;
			std::vector<bool> seen(nodes.size(), false);
			int node = root;
			while (node >= 0 || !stack.empty())
			{
				while (node >= 0)
				{
					r.chargeWork();
					if (seen[static_cast<std::size_t>(node)])
						throw std::logic_error("Tree workspace contains a repeated child link.");
					seen[static_cast<std::size_t>(node)] = true;
					stack.push_back(static_cast<std::size_t>(node));
					node = nodes[static_cast<std::size_t>(node)].left;
				}
				node = static_cast<int>(stack.back()); stack.pop_back();
				order.push_back(nodes[static_cast<std::size_t>(node)].originalIndex);
				r.addMetric("tree_traversal_visits");
				node = nodes[static_cast<std::size_t>(node)].right;
			}
			return order;
		}

		int splayTreeKey(Recorder& r, std::vector<TreeNode>& nodes, int root, std::size_t itemIndex, std::size_t depth = 0)
		{
			r.chargeWork();
			if (root < 0) return root;
			if (depth > nodes.size() * 2 + 2) throw std::logic_error("Splay tree recursion detected a cycle in the child links.");
			const SortItem item = nodes[itemIndex].item;
			TreeNode& node = nodes[static_cast<std::size_t>(root)];
			r.compare(item, node.item, itemIndex, static_cast<std::size_t>(root), "main", "tree_nodes");
			const ComparisonResult relation = compareItems(item, node.item, r.trace.direction);
			if (relation == ComparisonResult::Less)
			{
				const int left = node.left;
				if (left < 0) return root;
				r.compare(item, nodes[static_cast<std::size_t>(left)].item, itemIndex, static_cast<std::size_t>(left), "main", "tree_nodes");
				const ComparisonResult childRelation = compareItems(item, nodes[static_cast<std::size_t>(left)].item, r.trace.direction);
				if (childRelation == ComparisonResult::Less)
				{
					const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(left)].left, itemIndex, depth + 1);
					setTreeChild(r, nodes, static_cast<std::size_t>(left), true, grandchild);
					root = rotateTreeRight(r, nodes, root);
				}
				else
				{
					const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(left)].right, itemIndex, depth + 1);
					setTreeChild(r, nodes, static_cast<std::size_t>(left), false, grandchild);
				if (nodes[static_cast<std::size_t>(root)].left >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(root)].left)].right >= 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(root), true,
							rotateTreeLeft(r, nodes, nodes[static_cast<std::size_t>(root)].left));
				}
				return nodes[static_cast<std::size_t>(root)].left < 0 ? root : rotateTreeRight(r, nodes, root);
			}
			const int right = node.right;
			if (right < 0) return root;
			r.compare(item, nodes[static_cast<std::size_t>(right)].item, itemIndex, static_cast<std::size_t>(right), "main", "tree_nodes");
			const ComparisonResult childRelation = compareItems(item, nodes[static_cast<std::size_t>(right)].item, r.trace.direction);
			if (childRelation == ComparisonResult::Less)
			{
				const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(right)].left, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(right), true, grandchild);
				if (nodes[static_cast<std::size_t>(root)].right >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(root)].right)].left >= 0)
					setTreeChild(r, nodes, static_cast<std::size_t>(root), false,
						rotateTreeRight(r, nodes, nodes[static_cast<std::size_t>(root)].right));
			}
			else
			{
				const int grandchild = splayTreeKey(r, nodes, nodes[static_cast<std::size_t>(right)].right, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(right), false, grandchild);
				root = rotateTreeLeft(r, nodes, root);
			}
			return nodes[static_cast<std::size_t>(root)].right < 0 ? root : rotateTreeLeft(r, nodes, root);
		}

		void splayTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				r.chargeWork();
				if (root < 0) root = static_cast<int>(i);
				else
				{
					root = splayTreeKey(r, nodes, root, i);
					const SortItem item = nodes[i].item;
					r.compare(item, nodes[static_cast<std::size_t>(root)].item, i, static_cast<std::size_t>(root), "main", "tree_nodes");
					const bool goLeft = compareItems(item, nodes[static_cast<std::size_t>(root)].item, r.trace.direction) == ComparisonResult::Less;
					if (goLeft)
					{
						const int oldLeft = nodes[static_cast<std::size_t>(root)].left;
						setTreeChild(r, nodes, i, true, oldLeft);
						setTreeChild(r, nodes, i, false, root);
						setTreeChild(r, nodes, static_cast<std::size_t>(root), true, -1);
					}
					else
					{
						const int oldRight = nodes[static_cast<std::size_t>(root)].right;
						setTreeChild(r, nodes, i, true, root);
						setTreeChild(r, nodes, i, false, oldRight);
						setTreeChild(r, nodes, static_cast<std::size_t>(root), false, -1);
					}
					root = static_cast<int>(i);
				}
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			std::vector<std::pair<int, std::size_t> > pending(1, std::make_pair(root, static_cast<std::size_t>(1)));
			std::vector<bool> seen(count, false);
			std::size_t maximumDepth = 0;
			while (!pending.empty())
			{
				r.chargeWork();
				const std::pair<int, std::size_t> entry = pending.back(); pending.pop_back();
				if (seen[static_cast<std::size_t>(entry.first)])
					throw std::logic_error("Splay tree workspace contains a repeated child link.");
				seen[static_cast<std::size_t>(entry.first)] = true;
				maximumDepth = (std::max)(maximumDepth, entry.second);
				const TreeNode& node = nodes[static_cast<std::size_t>(entry.first)];
				if (node.left >= 0) pending.push_back(std::make_pair(node.left, entry.second + 1));
				if (node.right >= 0) pending.push_back(std::make_pair(node.right, entry.second + 1));
			}
			r.setMetric("tree_height", maximumDepth);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "Splay tree in-order extraction");
		}

		void aaTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_skews");
			r.declareMetric("tree_splits");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_level", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			int root = -1;
			std::function<int(int, std::size_t)> insert = [&](int current, std::size_t itemIndex) -> int
			{
				r.chargeWork();
				if (current < 0) return static_cast<int>(itemIndex);
				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = insert(goLeft ? node.left : node.right, itemIndex);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, child);
				int adjusted = current;
				if (nodes[static_cast<std::size_t>(adjusted)].left >= 0 &&
					nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(adjusted)].left)].level == nodes[static_cast<std::size_t>(adjusted)].level)
				{
					adjusted = rotateTreeRight(r, nodes, adjusted);
					r.addMetric("tree_skews");
				}
				const int right = nodes[static_cast<std::size_t>(adjusted)].right;
				if (right >= 0)
				{
					const int rightRight = nodes[static_cast<std::size_t>(right)].right;
					if (rightRight >= 0 && nodes[static_cast<std::size_t>(rightRight)].level == nodes[static_cast<std::size_t>(adjusted)].level)
					{
						adjusted = rotateTreeLeft(r, nodes, adjusted);
						++nodes[static_cast<std::size_t>(adjusted)].level;
						r.writeCounter("tree_level", static_cast<std::size_t>(adjusted), nodes[static_cast<std::size_t>(adjusted)].level);
						r.addMetric("tree_splits");
					}
				}
				return adjusted;
			};
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i);
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			int maximumLevel = 0;
			for (std::size_t i = 0; i < count; ++i) maximumLevel = (std::max)(maximumLevel, nodes[i].level);
			r.setMetric("tree_height", static_cast<std::uint64_t>(maximumLevel + 1));
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "AA tree in-order extraction");
		}

		void avlTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_rotations");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_height", count);
			r.resizeCounters("tree_balance", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			const auto nodeHeight = [&nodes](int node) -> int
				{ return node < 0 ? 0 : nodes[static_cast<std::size_t>(node)].height; };
			std::function<int(int, std::size_t)> insert = [&](int current, std::size_t itemIndex) -> int
			{
				r.chargeWork();
				if (current < 0) return static_cast<int>(itemIndex);
				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = insert(goLeft ? node.left : node.right, itemIndex);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, child);
				TreeNode& updated = nodes[static_cast<std::size_t>(current)];
				updated.height = 1 + (std::max)(nodeHeight(updated.left), nodeHeight(updated.right));
				updated.balance = nodeHeight(updated.left) - nodeHeight(updated.right);
				r.writeCounter("tree_height", static_cast<std::size_t>(current), updated.height);
				r.writeCounter("tree_balance", static_cast<std::size_t>(current), updated.balance);
				if (updated.balance > 1)
				{
					if (nodes[static_cast<std::size_t>(updated.left)].balance < 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(current), true, rotateTreeLeft(r, nodes, updated.left));
					return rotateTreeRight(r, nodes, current);
				}
				if (updated.balance < -1)
				{
					if (nodes[static_cast<std::size_t>(updated.right)].balance > 0)
						setTreeChild(r, nodes, static_cast<std::size_t>(current), false, rotateTreeRight(r, nodes, updated.right));
					return rotateTreeLeft(r, nodes, current);
				}
				return current;
			};
			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i);
				r.writeCounter("tree_root", 0, root + 1);
				r.setMetric("tree_nodes", i + 1);
			}
			r.setMetric("tree_height", nodes[static_cast<std::size_t>(root)].height);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "AVL tree in-order extraction");
		}

		void classicTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (count == 0) return;
			r.resizeCounters("tree_lower", count);
			r.resizeCounters("tree_upper", count);
			r.setMetric("tree_height", 1);
			for (std::size_t i = 1; i < count; ++i)
			{
				std::size_t node = 0, depth = 1;
				while (true)
				{
					r.chargeWork();
					const SortItem item = r.at(i), parent = r.at(node);
					r.compare(item, parent, i, node, "main", "main", SemanticRole::ComparedItem, SemanticRole::ProbedItem);
					int& child = compareItems(item, parent, r.trace.direction) == ComparisonResult::Less
						? r.state.integerWorkspaces["tree_lower"][node] : r.state.integerWorkspaces["tree_upper"][node];
					if (child == 0)
					{
						r.writeCounter(compareItems(item, parent, r.trace.direction) == ComparisonResult::Less ? "tree_lower" : "tree_upper",
							node, static_cast<int>(i + 1));
						break;
					}
					node = static_cast<std::size_t>(child - 1);
					++depth;
				}
				r.setMetric("tree_height", static_cast<std::uint64_t>(std::max<std::size_t>(
					static_cast<std::size_t>(r.state.metrics.algorithm["tree_height"]), depth + 1)));
				r.addMetric("tree_nodes");
			}
			std::vector<std::size_t> order;
			std::vector<std::size_t> stack;
			std::size_t node = 0;
			while (node < count || !stack.empty())
			{
				while (node < count)
				{
					r.chargeWork();
					stack.push_back(node);
					const int left = r.state.integerWorkspaces["tree_lower"][node];
					node = left == 0 ? count : static_cast<std::size_t>(left - 1);
				}
				node = stack.back(); stack.pop_back();
				order.push_back(node);
				r.addMetric("tree_traversal_visits");
				const int right = r.state.integerWorkspaces["tree_upper"][node];
				node = right == 0 ? count : static_cast<std::size_t>(right - 1);
			}
			r.setMetric("tree_nodes", count);
			emitPermutationOrder(r, order, std::string(), "Classic tree in-order extraction");
		}

		void treeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes");
			r.declareMetric("tree_height");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (count == 0) return;
			std::vector<TreeNode> nodes;
			nodes.reserve(count);
			r.resizeAuxiliary("tree_nodes", count);
			r.resizeCounters("tree_left", count);
			r.resizeCounters("tree_right", count);
			std::size_t root = 0, maximumDepth = 1;
			for (std::size_t i = 0; i < count; ++i)
			{
				const SortItem item = r.at(i);
				nodes.push_back(TreeNode(item, i));
				r.writeAuxiliary("tree_nodes", i, item);
				if (i == 0) continue;
				std::size_t node = root, depth = 1;
				while (true)
				{
					r.chargeWork();
					TreeNode& parent = nodes[node];
					r.compare(item, parent.item, i, node, "main", "tree_nodes");
					int& child = compareItems(item, parent.item, r.trace.direction) == ComparisonResult::Less
						? parent.left : parent.right;
					const std::string lane = compareItems(item, parent.item, r.trace.direction) == ComparisonResult::Less
						? "tree_left" : "tree_right";
					if (child < 0)
					{
						child = static_cast<int>(i);
						r.writeCounter(lane, node, static_cast<int>(i + 1));
						break;
					}
					node = static_cast<std::size_t>(child);
					++depth;
				}
				maximumDepth = std::max(maximumDepth, depth + 1);
				r.setMetric("tree_height", maximumDepth);
			}
			std::vector<std::size_t> order;
			std::vector<std::size_t> stack;
			std::size_t node = root;
			while (node < count || !stack.empty())
			{
				while (node < count)
				{
					r.chargeWork();
					stack.push_back(node);
					node = nodes[node].left < 0 ? count : static_cast<std::size_t>(nodes[node].left);
				}
				node = stack.back(); stack.pop_back();
				order.push_back(nodes[node].originalIndex);
				r.addMetric("tree_traversal_visits");
				node = nodes[node].right < 0 ? count : static_cast<std::size_t>(nodes[node].right);
			}
			r.setMetric("tree_nodes", count);
			emitPermutationOrder(r, order, "tree_nodes", "Tree in-order extraction");
		}

		void setTreeColor(Recorder& r, std::vector<TreeNode>& nodes, int node, bool red)
		{
			if (node < 0 || nodes[static_cast<std::size_t>(node)].red == red) return;
			nodes[static_cast<std::size_t>(node)].red = red;
			r.writeCounter("tree_color", static_cast<std::size_t>(node), red ? 1 : 0);
			r.addMetric("tree_recolors");
		}

		void redBlackTreeSort(Recorder& r)
		{
			r.declareMetric("tree_nodes"); r.declareMetric("tree_height");
			r.declareMetric("tree_rotations"); r.declareMetric("tree_recolors");
			r.declareMetric("tree_traversal_visits");
			const std::size_t count = r.state.slots.size();
			if (!count) return;
			balancedTreeStorage(r, count);
			r.resizeCounters("tree_parent", count); r.resizeCounters("tree_color", count);
			std::vector<TreeNode> nodes;
			balancedTreeInsertItems(r, nodes, count);
			typedef std::pair<int, bool> InsertResult;
			const auto singleRotate = [&r, &nodes](int subtreeRoot, bool rotateLeft) -> int
			{
				const int newRoot = rotateLeft
					? rotateTreeLeft(r, nodes, subtreeRoot)
					: rotateTreeRight(r, nodes, subtreeRoot);
				setTreeColor(r, nodes, newRoot, false);
				setTreeColor(r, nodes, subtreeRoot, true);
				return newRoot;
			};
			std::function<InsertResult(int, std::size_t, std::size_t)> insert =
				[&](int current, std::size_t itemIndex, std::size_t depth) -> InsertResult
			{
				r.chargeWork();
				if (depth > count + 1) throw std::logic_error("Red-black insertion exceeded its recursion guard.");
				if (current < 0)
				{
					setTreeColor(r, nodes, static_cast<int>(itemIndex), true);
					return InsertResult(static_cast<int>(itemIndex), false);
				}

				TreeNode& node = nodes[static_cast<std::size_t>(current)];
				if (!node.red && node.left >= 0 && node.right >= 0 &&
					nodes[static_cast<std::size_t>(node.left)].red && nodes[static_cast<std::size_t>(node.right)].red)
				{
					setTreeColor(r, nodes, current, true);
					setTreeColor(r, nodes, node.left, false);
					setTreeColor(r, nodes, node.right, false);
				}

				const SortItem item = nodes[itemIndex].item;
				r.compare(item, node.item, itemIndex, static_cast<std::size_t>(current), "main", "tree_nodes");
				const bool goLeft = compareItems(item, node.item, r.trace.direction) == ComparisonResult::Less;
				const int child = goLeft ? node.left : node.right;
				const InsertResult inserted = insert(child, itemIndex, depth + 1);
				setTreeChild(r, nodes, static_cast<std::size_t>(current), goLeft, inserted.first);

				if (inserted.second)
				{
					const int childRoot = goLeft
						? nodes[static_cast<std::size_t>(current)].left
						: nodes[static_cast<std::size_t>(current)].right;
					if (goLeft)
					{
						if (nodes[static_cast<std::size_t>(childRoot)].left >= 0 &&
							nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(childRoot)].left)].red)
							return InsertResult(singleRotate(current, false), false);
						const int pivot = singleRotate(childRoot, true);
						setTreeChild(r, nodes, static_cast<std::size_t>(current), true, pivot);
						return InsertResult(singleRotate(current, false), false);
					}
					if (nodes[static_cast<std::size_t>(childRoot)].right >= 0 &&
						nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(childRoot)].right)].red)
						return InsertResult(singleRotate(current, true), false);
					const int pivot = singleRotate(childRoot, false);
					setTreeChild(r, nodes, static_cast<std::size_t>(current), false, pivot);
					return InsertResult(singleRotate(current, true), false);
				}
				return InsertResult(current, nodes[static_cast<std::size_t>(current)].red &&
					nodes[static_cast<std::size_t>(inserted.first)].red);
			};

			int root = -1;
			for (std::size_t i = 0; i < count; ++i)
			{
				root = insert(root, i, 1).first;
				setTreeColor(r, nodes, root, false);
				r.setMetric("tree_nodes", i + 1);
				r.writeCounter("tree_root", 0, root + 1);
			}
			std::vector<std::pair<int, std::size_t> > pending(1, std::make_pair(root, static_cast<std::size_t>(1)));
			std::vector<bool> seen(count, false);
			std::size_t maximumDepth = 0;
			while (!pending.empty())
			{
				r.chargeWork();
				const std::pair<int, std::size_t> entry = pending.back(); pending.pop_back();
				if (seen[static_cast<std::size_t>(entry.first)]) throw std::logic_error("Red-black tree contains a repeated child link.");
				seen[static_cast<std::size_t>(entry.first)] = true;
				maximumDepth = (std::max)(maximumDepth, entry.second);
				const TreeNode& node = nodes[static_cast<std::size_t>(entry.first)];
				if (node.red && ((node.left >= 0 && nodes[static_cast<std::size_t>(node.left)].red) ||
					(node.right >= 0 && nodes[static_cast<std::size_t>(node.right)].red)))
					throw std::logic_error("Red-black tree contains adjacent red nodes.");
				if (node.left >= 0) pending.push_back(std::make_pair(node.left, entry.second + 1));
				if (node.right >= 0) pending.push_back(std::make_pair(node.right, entry.second + 1));
			}
			std::function<int(int)> blackHeight = [&](int node) -> int
			{
				if (node < 0) return 1;
				const int leftHeight = blackHeight(nodes[static_cast<std::size_t>(node)].left);
				const int rightHeight = blackHeight(nodes[static_cast<std::size_t>(node)].right);
				if (leftHeight != rightHeight) throw std::logic_error("Red-black tree has unequal black heights.");
				return leftHeight + (nodes[static_cast<std::size_t>(node)].red ? 0 : 1);
			};
			blackHeight(root);
			r.setMetric("tree_height", maximumDepth);
			emitPermutationOrder(r, balancedTreeInOrder(r, nodes, root), "tree_nodes", "Red-black tree in-order extraction");
		}

	}
}
