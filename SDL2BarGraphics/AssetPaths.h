#pragma once

#include <string>

// Resolves packaged application assets beside the executable instead of
// depending on the process working directory.
std::string applicationAssetPath(const std::string& relativePath);
