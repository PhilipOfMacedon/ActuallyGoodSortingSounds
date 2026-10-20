#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../SortingLogic/SortingLogic.h"

bool findDominantActiveRole(const std::vector<sortinglogic::ActiveRole>& roles,
	std::uint64_t itemId, const std::string& lane, sortinglogic::SemanticRole& selectedRole);

class ActiveRoleLookup
{
public:
	ActiveRoleLookup();
	void rebuild(const std::vector<sortinglogic::ActiveRole>& roles);
	void release() { roles_ = nullptr; indexed_ = false; }
	bool find(std::uint64_t itemId, const std::string& lane, sortinglogic::SemanticRole& selectedRole) const;

private:
	const std::vector<sortinglogic::ActiveRole>* roles_;
	std::vector<std::size_t> roleIndices_;
	bool indexed_;
};
