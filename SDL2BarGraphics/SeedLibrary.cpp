#include "SeedLibrary.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace
{
	bool parseSeed(const std::string& text, std::uint32_t& seed)
	{
		if (text.empty() || text.size() > 10) return false;
		std::uint64_t value = 0;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			if (c < '0' || c > '9') return false;
			value = value * 10 + (c - '0');
			if (value > (std::numeric_limits<std::uint32_t>::max)()) return false;
		}
		seed = static_cast<std::uint32_t>(value);
		return true;
	}

	std::string escapeName(const std::string& name)
	{
		static const char Hex[] = "0123456789ABCDEF";
		std::string encoded;
		for (std::size_t i = 0; i < name.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			if (c == '%' || c == '\t' || c == '\r' || c == '\n')
			{
				encoded += '%';
				encoded += Hex[c >> 4];
				encoded += Hex[c & 15];
			}
			else encoded += static_cast<char>(c);
		}
		return encoded;
	}

	int fromHex(char value)
	{
		if (value >= '0' && value <= '9') return value - '0';
		if (value >= 'A' && value <= 'F') return value - 'A' + 10;
		if (value >= 'a' && value <= 'f') return value - 'a' + 10;
		return -1;
	}

	bool unescapeName(const std::string& text, std::string& name)
	{
		name.clear();
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] != '%') name += text[i];
			else
			{
				if (i + 2 >= text.size()) return false;
				const int high = fromHex(text[i + 1]);
				const int low = fromHex(text[i + 2]);
				if (high < 0 || low < 0) return false;
				name += static_cast<char>((high << 4) | low);
				i += 2;
			}
			if (name.size() > SeedBookmarkStore::MaximumNameLength) return false;
		}
		return true;
	}
}

const int SeedBookmarkStore::CurrentVersion;
const std::size_t SeedBookmarkStore::MaximumBookmarks;
const std::size_t SeedBookmarkStore::MaximumNameLength;

SeedHistory::SeedHistory() : entries_(), cursor_(0) {}

SeedHistory::SeedHistory(std::uint32_t initialSeed) : entries_(1, initialSeed), cursor_(0) {}

void SeedHistory::reset(std::uint32_t seed)
{
	entries_.assign(1, seed);
	cursor_ = 0;
}

bool SeedHistory::append(std::uint32_t seed)
{
	if (entries_.empty())
	{
		entries_.push_back(seed);
		cursor_ = 0;
		return true;
	}
	if (entries_[cursor_] == seed) return false;
	entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1), entries_.end());
	entries_.push_back(seed);
	cursor_ = entries_.size() - 1;
	return true;
}

bool SeedHistory::canGoPrevious() const { return !entries_.empty() && cursor_ > 0; }
bool SeedHistory::canGoNext() const { return !entries_.empty() && cursor_ + 1 < entries_.size(); }
std::uint32_t SeedHistory::previous() const { return canGoPrevious() ? entries_[cursor_ - 1] : current(); }
std::uint32_t SeedHistory::next() const { return canGoNext() ? entries_[cursor_ + 1] : current(); }
bool SeedHistory::movePrevious() { if (!canGoPrevious()) return false; --cursor_; return true; }
bool SeedHistory::moveNext() { if (!canGoNext()) return false; ++cursor_; return true; }
std::uint32_t SeedHistory::current() const { return entries_.empty() ? 0 : entries_[cursor_]; }

SeedBookmarkStore::SeedBookmarkStore() : bookmarks_(), selectedIndex_(0) {}

bool SeedBookmarkStore::select(std::size_t index)
{
	if (index >= bookmarks_.size()) return false;
	selectedIndex_ = index;
	return true;
}

bool SeedBookmarkStore::save(std::uint32_t seed, const std::string& name)
{
	if (name.size() > MaximumNameLength) return false;
	for (std::size_t i = 0; i < bookmarks_.size(); ++i)
		if (bookmarks_[i].seed == seed)
		{
			selectedIndex_ = i;
			if (!name.empty() && bookmarks_[i].name != name)
			{
				bookmarks_[i].name = name;
				return true;
			}
			return false;
		}
	if (bookmarks_.size() >= MaximumBookmarks) return false;
	bookmarks_.push_back(SeedBookmark(seed, name));
	selectedIndex_ = bookmarks_.size() - 1;
	return true;
}

bool SeedBookmarkStore::rename(std::size_t index, const std::string& name)
{
	if (index >= bookmarks_.size() || name.size() > MaximumNameLength) return false;
	if (bookmarks_[index].name == name) return false;
	bookmarks_[index].name = name;
	return true;
}

bool SeedBookmarkStore::remove(std::size_t index)
{
	if (index >= bookmarks_.size()) return false;
	bookmarks_.erase(bookmarks_.begin() + static_cast<std::ptrdiff_t>(index));
	if (bookmarks_.empty()) selectedIndex_ = 0;
	else if (selectedIndex_ > index) --selectedIndex_;
	else if (selectedIndex_ >= bookmarks_.size()) selectedIndex_ = bookmarks_.size() - 1;
	return true;
}

bool SeedBookmarkStore::contains(std::uint32_t seed) const
{
	for (std::size_t i = 0; i < bookmarks_.size(); ++i)
		if (bookmarks_[i].seed == seed) return true;
	return false;
}

std::string SeedBookmarkStore::encode(const std::vector<SeedBookmark>& bookmarks)
{
	std::ostringstream content;
	content << "version=" << CurrentVersion << '\n';
	std::vector<std::uint32_t> encodedSeeds;
	for (std::size_t i = 0; i < bookmarks.size() && encodedSeeds.size() < MaximumBookmarks; ++i)
	{
		if (std::find(encodedSeeds.begin(), encodedSeeds.end(), bookmarks[i].seed) != encodedSeeds.end() ||
			bookmarks[i].name.size() > MaximumNameLength) continue;
		encodedSeeds.push_back(bookmarks[i].seed);
		content << "seed=" << bookmarks[i].seed;
		if (!bookmarks[i].name.empty()) content << '\t' << escapeName(bookmarks[i].name);
		content << '\n';
	}
	return content.str();
}

bool SeedBookmarkStore::decode(const std::string& content, std::vector<SeedBookmark>& bookmarks,
	std::string* error)
{
	bookmarks.clear();
	std::istringstream lines(content);
	std::string line;
	bool versionSeen = false;
	std::size_t validRecords = 0;
	while (std::getline(lines, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.empty()) continue;
		if (line.compare(0, 8, "version=") == 0)
		{
			if (versionSeen || line != "version=1")
			{
				if (error) *error = "The saved-seed file version is unsupported or repeated.";
				bookmarks.clear();
				return false;
			}
			versionSeen = true;
			continue;
		}
		if (!versionSeen || line.compare(0, 5, "seed=") != 0) continue;
		const std::string::size_type tab = line.find('\t', 5);
		const std::string seedText = line.substr(5, tab == std::string::npos ? std::string::npos : tab - 5);
		std::uint32_t seed = 0;
		std::string name;
		if (!parseSeed(seedText, seed) || (tab != std::string::npos && !unescapeName(line.substr(tab + 1), name))) continue;
		bool duplicate = false;
		for (std::size_t i = 0; i < bookmarks.size(); ++i) duplicate = duplicate || bookmarks[i].seed == seed;
		if (duplicate || bookmarks.size() >= MaximumBookmarks) continue;
		bookmarks.push_back(SeedBookmark(seed, name));
		++validRecords;
	}
	if (!versionSeen)
	{
		if (error) *error = "The saved-seed file has no supported version header.";
		return false;
	}
	(void)validRecords;
	if (error) error->clear();
	return true;
}
