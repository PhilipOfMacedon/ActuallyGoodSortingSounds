#include "ActiveRoleLookup.h"

#include <algorithm>
#include <utility>

namespace
{
	int rolePriority(sortinglogic::SemanticRole role)
	{
		using sortinglogic::SemanticRole;
		switch (role)
		{
		case SemanticRole::ComparedItem: return 1;
		case SemanticRole::ShiftedItem: return 2;
		case SemanticRole::WriteTarget: return 3;
		case SemanticRole::HeldKey: return 4;
		case SemanticRole::Gap: return 0;
		case SemanticRole::Pivot: return 5;
		case SemanticRole::CurrentMinimum:
		case SemanticRole::CurrentMaximum: return 3;
		case SemanticRole::AuxiliaryCopy:
		case SemanticRole::ProcessedItem:
		case SemanticRole::ProbedItem: return 2;
		case SemanticRole::DiscardedItem: return 5;
		}
		return 0;
	}
}

bool findDominantActiveRole(const std::vector<sortinglogic::ActiveRole>& roles,
	std::uint64_t itemId, const std::string& lane, sortinglogic::SemanticRole& selectedRole)
{
	int priority = 0;
	selectedRole = sortinglogic::SemanticRole::ComparedItem;
	for (std::size_t i = 0; i < roles.size(); ++i)
	{
		if (roles[i].itemId != itemId || roles[i].lane != lane) continue;
		const int candidatePriority = rolePriority(roles[i].role);
		if (candidatePriority > priority)
		{
			priority = candidatePriority;
			selectedRole = roles[i].role;
		}
	}
	return priority != 0;
}

ActiveRoleLookup::ActiveRoleLookup() : roles_(nullptr), roleIndices_(), indexed_(false)
{
}

void ActiveRoleLookup::rebuild(const std::vector<sortinglogic::ActiveRole>& roles)
{
	roles_ = &roles;
	indexed_ = roles.size() >= 12;
	roleIndices_.clear();
	if (!indexed_) return;
	roleIndices_.reserve(roles.size());
	for (std::size_t i = 0; i < roles.size(); ++i)
		if (rolePriority(roles[i].role) > 0)
			roleIndices_.push_back(i);
	std::sort(roleIndices_.begin(), roleIndices_.end(), [this](std::size_t aIndex, std::size_t bIndex)
	{
		const sortinglogic::ActiveRole& a = (*roles_)[aIndex];
		const sortinglogic::ActiveRole& b = (*roles_)[bIndex];
		if (a.lane != b.lane) return a.lane < b.lane;
		if (a.itemId != b.itemId) return a.itemId < b.itemId;
		return aIndex < bIndex;
	});
	std::size_t compacted = 0;
	for (std::size_t i = 0; i < roleIndices_.size(); ++i)
	{
		const sortinglogic::ActiveRole& candidate = roles[roleIndices_[i]];
		if (compacted != 0)
		{
			const sortinglogic::ActiveRole& current = roles[roleIndices_[compacted - 1]];
			if (current.itemId == candidate.itemId && current.lane == candidate.lane)
			{
				if (rolePriority(candidate.role) > rolePriority(current.role))
					roleIndices_[compacted - 1] = roleIndices_[i];
				continue;
			}
		}
		if (compacted != i) roleIndices_[compacted] = roleIndices_[i];
		++compacted;
	}
	roleIndices_.resize(compacted);
}

bool ActiveRoleLookup::find(std::uint64_t itemId, const std::string& lane,
	sortinglogic::SemanticRole& selectedRole) const
{
	selectedRole = sortinglogic::SemanticRole::ComparedItem;
	if (!roles_) return false;
	if (!indexed_) return findDominantActiveRole(*roles_, itemId, lane, selectedRole);
	const std::vector<std::size_t>::const_iterator found = std::lower_bound(roleIndices_.begin(), roleIndices_.end(),
		std::make_pair(lane, itemId), [this](std::size_t roleIndex, const std::pair<std::string, std::uint64_t>& key)
		{
			const sortinglogic::ActiveRole& role = (*roles_)[roleIndex];
			if (role.lane != key.first) return role.lane < key.first;
			return role.itemId < key.second;
		});
	if (found == roleIndices_.end()) return false;
	const sortinglogic::ActiveRole& role = (*roles_)[*found];
	if (role.itemId != itemId || role.lane != lane) return false;
	selectedRole = role.role;
	return true;
}
