#pragma once

#include <algorithm>
#include <cctype>
#include <string>

inline std::string lowerAsciiText(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

inline bool containsTextInsensitive(const std::string& value, const std::string& query)
{
	return query.empty() || lowerAsciiText(value).find(lowerAsciiText(query)) != std::string::npos;
}
