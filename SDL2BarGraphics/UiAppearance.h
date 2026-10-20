#pragma once

#include <map>
#include <string>
#include <vector>

// This header contains only value types. It is deliberately independent of
// SDL, OpenGL, and GLM so settings migration and appearance validation stay
// testable without a graphics context.
enum class PaletteId { Cyan, Amber, Monochrome, Light, VgaClassic, Windows95 };
enum class UiPresetId { Current, FlatVgaDos, ClassicWindows95 };
enum class UiTokenType { Color, Number, Boolean, Choice, Font };
enum class UiTokenCategory { Colors, Typography, Metrics, Decorations };
enum class UiInvalidation { Paint = 1, Layout = 2, Typography = 4 };

const char* paletteDisplayName(PaletteId id);
const std::vector<PaletteId>& paletteCatalog();

struct UiColor
{
	float r, g, b, a;
	UiColor(float red = 0.0f, float green = 0.0f, float blue = 0.0f, float alpha = 1.0f)
		: r(red), g(green), b(blue), a(alpha) {}
};

struct UiFontReference
{
	std::string family;
	std::string style;
	UiFontReference(const std::string& familyName = "Courier New", const std::string& faceStyle = "Bold")
		: family(familyName), style(faceStyle) {}
};

// Effective text settings shared by measurement and rasterization. Keeping the
// logical height beside the font raster parameters prevents wrapped text from
// being measured with a different style than the one that is drawn.
struct ResolvedTextStyle
{
	UiFontReference font;
	int rasterSize;
	int dpi;
	float logicalHeight;
	ResolvedTextStyle(const UiFontReference& fontValue = UiFontReference(), int rasterSizeValue = 12,
		int dpiValue = 96, float logicalHeightValue = 12.0f)
		: font(fontValue), rasterSize(rasterSizeValue), dpi(dpiValue), logicalHeight(logicalHeightValue) {}
	bool operator==(const ResolvedTextStyle& other) const
	{
		return font.family == other.font.family && font.style == other.font.style &&
			rasterSize == other.rasterSize && dpi == other.dpi && logicalHeight == other.logicalHeight;
	}
	bool operator!=(const ResolvedTextStyle& other) const { return !(*this == other); }
};

struct UiTokenValue
{
	UiTokenType type;
	UiColor color;
	double number;
	bool boolean;
	std::string text;
	UiFontReference font;
	UiTokenValue();
	static UiTokenValue fromColor(const UiColor& value);
	static UiTokenValue fromNumber(double value);
	static UiTokenValue fromBoolean(bool value);
	static UiTokenValue fromChoice(const std::string& value);
	static UiTokenValue fromFont(const UiFontReference& value);
};

struct UiTokenDescriptor
{
	std::string stableId;
	UiTokenCategory category;
	UiTokenType type;
	UiTokenValue defaultValue;
	double minimum;
	double maximum;
	std::vector<std::string> choices;
	UiInvalidation invalidation;
	std::string description;
};

struct UiThemeCustomization
{
	PaletteId palette;
	std::map<std::string, UiTokenValue> overrides;
	UiThemeCustomization() : palette(PaletteId::Cyan), overrides() {}
};

struct UiAppearancePreferences
{
	UiPresetId activePreset;
	std::map<UiPresetId, UiThemeCustomization> themes;
	UiAppearancePreferences();
};

struct UiPresetDefinition
{
	UiPresetId id;
	std::string stableId;
	std::string displayName;
	std::string skinId;
	std::string layoutId;
	std::vector<UiTokenDescriptor> tokens;
};

std::string uiPresetStableId(UiPresetId id);
bool parseUiPresetId(const std::string& value, UiPresetId& result);
const char* uiTokenCategoryName(UiTokenCategory category);
const UiPresetDefinition& uiPresetDefinition(UiPresetId id);
const std::vector<UiPresetId>& uiPresetCatalog();
const UiTokenDescriptor* findUiToken(const UiPresetDefinition& preset, const std::string& stableId);
UiThemeCustomization defaultUiThemeCustomization(PaletteId palette = PaletteId::Cyan);
UiThemeCustomization defaultUiThemeCustomization(UiPresetId preset);
UiThemeCustomization defaultUiThemeCustomization(UiPresetId preset, PaletteId palette);
bool validateUiThemeCustomization(UiPresetId preset, const UiThemeCustomization& value, std::string* error = nullptr);
bool setUiTokenValue(UiPresetId preset, UiThemeCustomization& value, const std::string& tokenId,
	const UiTokenValue& tokenValue, std::string* error = nullptr);
void selectUiPalette(UiThemeCustomization& value, PaletteId palette);
UiTokenValue effectiveUiTokenValue(UiPresetId preset, const UiThemeCustomization& value, const std::string& tokenId);
bool sameUiAppearancePreferences(const UiAppearancePreferences& left, const UiAppearancePreferences& right);
std::string encodeUiTokenValue(const UiTokenValue& value);
bool decodeUiTokenValue(const std::string& encoded, UiTokenType type, UiTokenValue& result);

// The profile codec deliberately contains only appearance data. It is also
// used by the in-app importer, so parsing never mutates live preferences.
struct UiAppearanceProfile
{
	UiPresetId preset;
	UiThemeCustomization theme;
	std::vector<std::string> ignoredTokens;
	UiAppearanceProfile() : preset(UiPresetId::Current), theme(), ignoredTokens() {}
};

std::string serializeUiAppearanceProfile(UiPresetId preset, const UiThemeCustomization& theme);
bool parseUiAppearanceProfile(const std::string& contents, UiAppearanceProfile& profile, std::string* error = nullptr);
