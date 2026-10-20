#pragma once

#include <string>

struct UserPreferences;

namespace settingsini
{
	UserPreferences decode(const std::string& text);
	std::string encode(const UserPreferences& preferences);
}
