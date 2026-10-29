#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class SeedHistory
{
public:
	SeedHistory();
	explicit SeedHistory(std::uint32_t initialSeed);

	void reset(std::uint32_t seed);
	bool append(std::uint32_t seed);
	bool canGoPrevious() const;
	bool canGoNext() const;
	std::uint32_t previous() const;
	std::uint32_t next() const;
	bool movePrevious();
	bool moveNext();
	std::uint32_t current() const;
	std::size_t cursor() const { return cursor_; }
	const std::vector<std::uint32_t>& entries() const { return entries_; }

private:
	std::vector<std::uint32_t> entries_;
	std::size_t cursor_;
};

struct SeedBookmark
{
	std::uint32_t seed;
	std::string name;

	SeedBookmark(std::uint32_t value = 0, const std::string& label = std::string())
		: seed(value), name(label) {}
};

class SeedBookmarkStore
{
public:
	static const int CurrentVersion = 1;
	static const std::size_t MaximumBookmarks = 4096;
	static const std::size_t MaximumNameLength = 128;

	SeedBookmarkStore();
	const std::vector<SeedBookmark>& bookmarks() const { return bookmarks_; }
	std::size_t selectedIndex() const { return selectedIndex_; }
	bool select(std::size_t index);
	bool save(std::uint32_t seed, const std::string& name = std::string());
	bool rename(std::size_t index, const std::string& name);
	bool remove(std::size_t index);
	bool contains(std::uint32_t seed) const;
	
	static std::string encode(const std::vector<SeedBookmark>& bookmarks);
	static bool decode(const std::string& content, std::vector<SeedBookmark>& bookmarks,
		std::string* error = nullptr);

private:
	std::vector<SeedBookmark> bookmarks_;
	std::size_t selectedIndex_;
};
