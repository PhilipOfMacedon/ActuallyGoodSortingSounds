#pragma once

#include <vector>

#include "AlgorithmBrowserModel.h"

// Converts the sorting registry into the small stable-identity catalog the UI
// needs. The browser model and view do not depend on SortingLogic descriptors.
const std::vector<AlgorithmFamilyEntry>& algorithmBrowserCatalog();
