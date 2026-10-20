#include "AssetPaths.h"

#include <SDL.h>

namespace
{
	const std::string& applicationDirectory()
	{
		static const std::string directory = []()
		{
			char* basePath = SDL_GetBasePath();
			if (!basePath) return std::string();
			const std::string result(basePath);
			SDL_free(basePath);
			return result;
		}();
		return directory;
	}
}

std::string applicationAssetPath(const std::string& relativePath)
{
	const std::string& directory = applicationDirectory();
	if (directory.empty()) return relativePath;
	if (relativePath.empty()) return directory;
	if (directory[directory.size() - 1] == '/' || directory[directory.size() - 1] == '\\')
		return directory + relativePath;
#if defined(_WIN32)
	return directory + '\\' + relativePath;
#else
	return directory + '/' + relativePath;
#endif
}
