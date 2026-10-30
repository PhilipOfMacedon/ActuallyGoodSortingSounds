#include "UiAppearance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace
{
	UiTokenDescriptor token(const char* id, UiTokenCategory category, const UiTokenValue& value,
		double minimum, double maximum, UiInvalidation invalidation, const char* description,
		const std::vector<std::string>& choices = std::vector<std::string>())
	{
		UiTokenDescriptor result;
		result.stableId = id;
		result.category = category;
		result.type = value.type;
		result.defaultValue = value;
		result.minimum = minimum;
		result.maximum = maximum;
		result.choices = choices;
		result.invalidation = invalidation;
		result.description = description;
		return result;
	}

	std::vector<UiTokenDescriptor> makeTokens(bool vga, bool windows95 = false)
	{
		std::vector<UiTokenDescriptor> values;
		const UiInvalidation paint = UiInvalidation::Paint;
		const UiInvalidation layout = UiInvalidation::Layout;
		const UiInvalidation typography = UiInvalidation::Typography;
		const UiColor cyan(0.22f, 0.71f, 0.84f, 1.0f);
		const UiColor text(0.93f, 0.96f, 0.98f, 1.0f);
		const struct ColorToken { const char* id; UiColor value; const char* description; } colors[] =
		{
			{ "color.background", UiColor(0.10f,0.10f,0.10f,1), "Application background" },
			{ "color.bar", cyan, "Primary chart bars" },
			{ "color.compared", UiColor(0.98f,0.62f,0.26f,1), "Compared items" },
			{ "color.held", UiColor(1.0f,0.83f,0.28f,1), "Held item" },
			{ "color.shifted", UiColor(0.67f,0.46f,0.96f,1), "Shifted items" },
			{ "color.write_target", UiColor(0.35f,0.84f,0.58f,1), "Write target" },
			{ "color.gap", UiColor(0.96f,0.28f,0.54f,1), "Gaps in the array" },
			{ "color.panel", UiColor(0.10f,0.16f,0.20f,1), "Panel surfaces" },
			{ "color.button", UiColor(0.13f,0.24f,0.31f,1), "Buttons and selectors" },
			{ "color.disabled_button", UiColor(0.10f,0.16f,0.19f,1), "Disabled controls" },
			{ "color.value_button", UiColor(0.10f,0.16f,0.20f,1), "Editable value labels" },
			{ "color.accent", UiColor(0.29f,0.72f,0.64f,1), "Selection and progress accent" },
			{ "color.run", UiColor(0.48f,0.28f,0.12f,1), "Trace build action" },
			{ "color.play_active", UiColor(0.18f,0.42f,0.34f,1), "Active playback action" },
			{ "color.timeline_rail", UiColor(0.09f,0.14f,0.17f,1), "Timeline track" },
			{ "color.timeline_thumb", UiColor(0.91f,0.78f,0.34f,1), "Timeline position marker" },
			{ "color.reset", UiColor(0.30f,0.21f,0.35f,1), "Reset action" },
			{ "color.text", text, "Primary text" },
			{ "color.muted_text", UiColor(0.68f,0.76f,0.79f,1), "Secondary text" },
			{ "color.overlay_dim", UiColor(0,0,0,0.58f), "Modal backdrop" },
			{ "color.border", UiColor(0.25f,0.38f,0.43f,1), "Control borders" }
		};
		for (std::size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i)
			values.push_back(token(colors[i].id, UiTokenCategory::Colors,
				UiTokenValue::fromColor(colors[i].value), 0.0, 1.0, paint, colors[i].description));

		values.push_back(token("typeface.primary", UiTokenCategory::Typography,
			UiTokenValue::fromFont(UiFontReference()), 0.0, 0.0, typography, "Installed font family and face style"));
		values.push_back(token("typography.title_size", UiTokenCategory::Typography,
			UiTokenValue::fromNumber(18), 8, 48, typography, "Window title size"));
		values.push_back(token("typography.body_size", UiTokenCategory::Typography,
			UiTokenValue::fromNumber(14), 8, 40, typography, "Telemetry and settings text size"));
		values.push_back(token("typography.control_size", UiTokenCategory::Typography,
			UiTokenValue::fromNumber(16), 8, 40, typography, "Control label size"));
		values.push_back(token("typography.caption_size", UiTokenCategory::Typography,
			UiTokenValue::fromNumber(12), 7, 32, typography, "Caption size"));

		if (!windows95)
			values.push_back(token("layout.header_height", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(42), 30, 90, layout, "Top title strip height"));
		values.push_back(token("layout.control_margin", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(vga ? 12 : 16), 0, 48, layout, "Control group outer margin"));
		values.push_back(token("layout.control_gap", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(vga ? 4 : 6), 0, 24, layout, "Gap between neighboring controls"));
		values.push_back(token("layout.control_height", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(windows95 ? 40 : vga ? 32 : 30), 18, 64, layout, "Main control height"));
		values.push_back(token("layout.chart_inset", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(vga ? 16 : 0), 0, 48, layout, "Chart viewport inset"));
		values.push_back(token("layout.content_gap", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(14), 0, 64, layout, "Gap between logical groups"));
		if (vga)
			values.push_back(token("layout.menu_height", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(windows95 ? 36 : 26), 18, 48, layout, "Classic menu bar height"));
		if (vga)
		{
			values.push_back(token("layout.rail_width", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(windows95 ? 292 : 340), 160, 440, layout,
				windows95 ? "Windows 95 setup rail width" : "VGA setup rail width"));
		values.push_back(token("layout.transport_height", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(88), 72, 240, layout, "Bottom transport group height"));
			values.push_back(token("scrollbar.thickness", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(14), 8, 36, layout, "Scrollbar track thickness"));
			values.push_back(token("scrollbar.arrow_size", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(14), 8, 36, layout, "Scrollbar arrow region size"));
			values.push_back(token("scrollbar.thumb_min_size", UiTokenCategory::Metrics,
				UiTokenValue::fromNumber(20), 8, 96, layout, "Minimum scrollbar thumb size"));
		}

		values.push_back(token("control.border_width", UiTokenCategory::Decorations,
			UiTokenValue::fromNumber(windows95 ? 2 : 1), 0, 8, paint, "Control border thickness"));
		values.push_back(token("control.padding_x", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(8), 2, 24, paint, "Horizontal space inside controls"));
		values.push_back(token("control.padding_y", UiTokenCategory::Metrics,
			UiTokenValue::fromNumber(4), 1, 12, paint, "Vertical space inside controls"));
		values.push_back(token("control.shadow_offset", UiTokenCategory::Decorations,
			UiTokenValue::fromNumber(windows95 ? 0 : 1), 0, 8, paint, "Control shadow offset"));
		values.push_back(token("panel.border_width", UiTokenCategory::Decorations,
			UiTokenValue::fromNumber(windows95 ? 2 : 1), 0, 8, paint, "Panel border thickness"));
		values.push_back(token("panel.shadow_offset", UiTokenCategory::Decorations,
			UiTokenValue::fromNumber(windows95 ? 6 : 5), 0, 16, paint, "Panel shadow offset"));
		if (vga)
		{
			values.push_back(token("control.arrow_size", UiTokenCategory::Decorations,
				UiTokenValue::fromNumber(6), 2, 24, paint, "Dropdown arrow size"));
			values.push_back(token("selection.indicator_size", UiTokenCategory::Decorations,
				UiTokenValue::fromNumber(12), 4, 32, paint, "Square selection indicator size"));
			values.push_back(token("scrollbar.show_arrows", UiTokenCategory::Decorations,
				UiTokenValue::fromBoolean(true), 0, 1, paint, "Show DOS scrollbar arrow buttons"));
		}
		return values;
	}

	const UiPresetDefinition& currentPreset()
	{
		static const UiPresetDefinition value = { UiPresetId::Current, "current", "Current", "terminal-flat", "current-grid", makeTokens(false) };
		return value;
	}
	const UiPresetDefinition& vgaPreset()
	{
		static const UiPresetDefinition value = { UiPresetId::FlatVgaDos, "flat-vga-dos", "Flat VGA/DOS", "flat-vga", "vga-rail", makeTokens(true) };
		return value;
	}
	const UiPresetDefinition& windows95Preset()
	{
		static const UiPresetDefinition value = { UiPresetId::ClassicWindows95, "classic-windows-95", "Classic Windows 95", "windows-95", "windows-95-frame", makeTokens(true, true) };
		return value;
	}

	bool validColor(const UiColor& c)
	{
		return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a) &&
			c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f &&
			c.b >= 0.0f && c.b <= 1.0f && c.a >= 0.0f && c.a <= 1.0f;
	}

	std::string escape(const std::string& value)
	{
		static const char hex[] = "0123456789ABCDEF";
		std::string result;
		for (std::size_t i = 0; i < value.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(value[i]);
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-' || c == '.')
				result.push_back(static_cast<char>(c));
			else
			{
				result.push_back('%');
				result.push_back(hex[c >> 4]);
				result.push_back(hex[c & 15]);
			}
		}
		return result;
	}

	int hexDigit(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	}

	bool unescape(const std::string& value, std::string& result)
	{
		result.clear();
		for (std::size_t i = 0; i < value.size(); ++i)
		{
			if (value[i] != '%') { result.push_back(value[i]); continue; }
			if (i + 2 >= value.size()) return false;
			const int high = hexDigit(value[i + 1]), low = hexDigit(value[i + 2]);
			if (high < 0 || low < 0) return false;
			result.push_back(static_cast<char>((high << 4) | low));
			i += 2;
		}
		return true;
	}

	std::string encodeValue(const UiTokenValue& value)
	{
		std::ostringstream out;
		out << std::setprecision(9);
		switch (value.type)
		{
		case UiTokenType::Color: out << "color:" << value.color.r << ',' << value.color.g << ',' << value.color.b << ',' << value.color.a; break;
		case UiTokenType::Number: out << "number:" << value.number; break;
		case UiTokenType::Boolean: out << "bool:" << (value.boolean ? 1 : 0); break;
		case UiTokenType::Choice: out << "choice:" << escape(value.text); break;
		case UiTokenType::Font: out << "font:" << escape(value.font.family) << '|' << escape(value.font.style); break;
		}
		return out.str();
	}

	bool parseFloat(const std::string& source, float& value)
	{
		char* end = nullptr;
		const double parsed = std::strtod(source.c_str(), &end);
		if (!end || end == source.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
		value = static_cast<float>(parsed);
		return std::isfinite(value);
	}

	bool decodeValue(const std::string& encoded, UiTokenType type, UiTokenValue& result)
	{
		const std::string prefix = type == UiTokenType::Color ? "color:" : type == UiTokenType::Number ? "number:" :
			type == UiTokenType::Boolean ? "bool:" : type == UiTokenType::Choice ? "choice:" : "font:";
		if (encoded.compare(0, prefix.size(), prefix) != 0) return false;
		const std::string raw = encoded.substr(prefix.size());
		if (type == UiTokenType::Number)
		{
			char* end = nullptr;
			const double value = std::strtod(raw.c_str(), &end);
			if (!end || end == raw.c_str() || *end != '\0' || !std::isfinite(value)) return false;
			result = UiTokenValue::fromNumber(value); return true;
		}
		if (type == UiTokenType::Boolean)
		{
			if (raw != "0" && raw != "1") return false;
			result = UiTokenValue::fromBoolean(raw == "1"); return true;
		}
		if (type == UiTokenType::Choice)
		{
			std::string value;
			if (!unescape(raw, value)) return false;
			result = UiTokenValue::fromChoice(value); return true;
		}
		if (type == UiTokenType::Font)
		{
			const std::string::size_type split = raw.find('|');
			if (split == std::string::npos) return false;
			std::string family, style;
			if (!unescape(raw.substr(0, split), family) || !unescape(raw.substr(split + 1), style)) return false;
			result = UiTokenValue::fromFont(UiFontReference(family, style)); return true;
		}
		std::istringstream parts(raw);
		char comma = 0;
		UiColor color;
		if (!(parts >> color.r >> comma) || comma != ',' || !(parts >> color.g >> comma) || comma != ',' ||
			!(parts >> color.b >> comma) || comma != ',' || !(parts >> color.a) || !parts.eof()) return false;
		result = UiTokenValue::fromColor(color);
		return true;
	}
}

const char* paletteDisplayName(PaletteId id)
{
	switch (id)
	{
	case PaletteId::Cyan: return "Current Cyan";
	case PaletteId::Amber: return "Amber Terminal";
	case PaletteId::Monochrome: return "High Contrast";
	case PaletteId::Light: return "Paper Light";
	case PaletteId::VgaClassic: return "VGA 95";
	case PaletteId::Windows95: return "Windows 95 Classic";
	}
	return "Current Cyan";
}

const std::vector<PaletteId>& paletteCatalog()
{
	static const std::vector<PaletteId> catalog =
	{
		PaletteId::Cyan,
		PaletteId::Amber,
		PaletteId::Monochrome,
		PaletteId::Light,
		PaletteId::VgaClassic,
		PaletteId::Windows95
	};
	return catalog;
}

UiTokenValue::UiTokenValue() : type(UiTokenType::Number), color(), number(0.0), boolean(false), text(), font() {}
UiTokenValue UiTokenValue::fromColor(const UiColor& value) { UiTokenValue result; result.type = UiTokenType::Color; result.color = value; return result; }
UiTokenValue UiTokenValue::fromNumber(double value) { UiTokenValue result; result.type = UiTokenType::Number; result.number = value; return result; }
UiTokenValue UiTokenValue::fromBoolean(bool value) { UiTokenValue result; result.type = UiTokenType::Boolean; result.boolean = value; return result; }
UiTokenValue UiTokenValue::fromChoice(const std::string& value) { UiTokenValue result; result.type = UiTokenType::Choice; result.text = value; return result; }
UiTokenValue UiTokenValue::fromFont(const UiFontReference& value) { UiTokenValue result; result.type = UiTokenType::Font; result.font = value; return result; }

UiAppearancePreferences::UiAppearancePreferences() : activePreset(UiPresetId::Current), themes()
{
	themes[UiPresetId::Current] = defaultUiThemeCustomization(UiPresetId::Current);
	themes[UiPresetId::FlatVgaDos] = defaultUiThemeCustomization(UiPresetId::FlatVgaDos);
	themes[UiPresetId::ClassicWindows95] = defaultUiThemeCustomization(UiPresetId::ClassicWindows95);
}

std::string uiPresetStableId(UiPresetId id)
{
	if (id == UiPresetId::FlatVgaDos) return "flat-vga-dos";
	if (id == UiPresetId::ClassicWindows95) return "classic-windows-95";
	return "current";
}
bool parseUiPresetId(const std::string& value, UiPresetId& result)
{
	if (value == "current") { result = UiPresetId::Current; return true; }
	if (value == "flat-vga-dos") { result = UiPresetId::FlatVgaDos; return true; }
	if (value == "classic-windows-95") { result = UiPresetId::ClassicWindows95; return true; }
	return false;
}
const char* uiTokenCategoryName(UiTokenCategory category)
{
	switch (category)
	{
	case UiTokenCategory::Colors: return "Colors";
	case UiTokenCategory::Typography: return "Typography";
	case UiTokenCategory::Metrics: return "Spacing and layout metrics";
	case UiTokenCategory::Decorations: return "Decorations";
	}
	return "Other";
}
const UiPresetDefinition& uiPresetDefinition(UiPresetId id)
{
	if (id == UiPresetId::FlatVgaDos) return vgaPreset();
	if (id == UiPresetId::ClassicWindows95) return windows95Preset();
	return currentPreset();
}
const std::vector<UiPresetId>& uiPresetCatalog()
{
	static const std::vector<UiPresetId> catalog = { UiPresetId::Current, UiPresetId::FlatVgaDos, UiPresetId::ClassicWindows95 };
	return catalog;
}
const UiTokenDescriptor* findUiToken(const UiPresetDefinition& preset, const std::string& stableId)
{
	for (std::size_t i = 0; i < preset.tokens.size(); ++i)
		if (preset.tokens[i].stableId == stableId) return &preset.tokens[i];
	return nullptr;
}
UiThemeCustomization defaultUiThemeCustomization(PaletteId palette)
{
	return defaultUiThemeCustomization(UiPresetId::Current, palette);
}
UiThemeCustomization defaultUiThemeCustomization(UiPresetId preset)
{
	return defaultUiThemeCustomization(preset,
		preset == UiPresetId::ClassicWindows95 ? PaletteId::Windows95 :
		preset == UiPresetId::FlatVgaDos ? PaletteId::VgaClassic : PaletteId::Cyan);
}
UiThemeCustomization defaultUiThemeCustomization(UiPresetId, PaletteId palette)
{
	UiThemeCustomization value;
	value.palette = palette;
	return value;
}

bool validateUiThemeCustomization(UiPresetId preset, const UiThemeCustomization& value, std::string* error)
{
	if (static_cast<int>(value.palette) < static_cast<int>(PaletteId::Cyan) || static_cast<int>(value.palette) > static_cast<int>(PaletteId::Windows95))
	{
		if (error) *error = "The selected palette is not registered.";
		return false;
	}
	const UiPresetDefinition& definition = uiPresetDefinition(preset);
	for (std::map<std::string, UiTokenValue>::const_iterator item = value.overrides.begin(); item != value.overrides.end(); ++item)
	{
		const UiTokenDescriptor* descriptor = findUiToken(definition, item->first);
		if (!descriptor)
		{
			if (error) *error = "Appearance token is not registered: " + item->first;
			return false;
		}
		const UiTokenValue& tokenValue = item->second;
		if (tokenValue.type != descriptor->type)
		{
			if (error) *error = "Appearance token has the wrong value type: " + item->first;
			return false;
		}
		bool valid = true;
		if (tokenValue.type == UiTokenType::Color) valid = validColor(tokenValue.color);
		else if (tokenValue.type == UiTokenType::Number)
			valid = std::isfinite(tokenValue.number) && tokenValue.number >= descriptor->minimum && tokenValue.number <= descriptor->maximum;
		else if (tokenValue.type == UiTokenType::Choice)
			valid = descriptor->choices.empty() || std::find(descriptor->choices.begin(), descriptor->choices.end(), tokenValue.text) != descriptor->choices.end();
		else if (tokenValue.type == UiTokenType::Font)
			valid = !tokenValue.font.family.empty() && tokenValue.font.family.size() <= 256 && tokenValue.font.style.size() <= 128;
		if (!valid)
		{
			if (error) *error = "Appearance token value is outside its allowed range: " + item->first;
			return false;
		}
	}
	if (error) error->clear();
	return true;
}

bool setUiTokenValue(UiPresetId preset, UiThemeCustomization& value, const std::string& tokenId,
	const UiTokenValue& tokenValue, std::string* error)
{
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), tokenId);
	if (!descriptor)
	{
		if (error) *error = "Appearance token is not registered: " + tokenId;
		return false;
	}
	UiThemeCustomization candidate = value;
	candidate.overrides[tokenId] = tokenValue;
	if (!validateUiThemeCustomization(preset, candidate, error)) return false;
	value = candidate;
	return true;
}

void selectUiPalette(UiThemeCustomization& value, PaletteId palette)
{
	value.palette = palette;
	for (std::map<std::string, UiTokenValue>::iterator item = value.overrides.begin(); item != value.overrides.end(); )
		if (item->second.type == UiTokenType::Color) value.overrides.erase(item++);
		else ++item;
}

UiTokenValue effectiveUiTokenValue(UiPresetId preset, const UiThemeCustomization& value, const std::string& tokenId)
{
	const std::map<std::string, UiTokenValue>::const_iterator overrideValue = value.overrides.find(tokenId);
	if (overrideValue != value.overrides.end()) return overrideValue->second;
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), tokenId);
	return descriptor ? descriptor->defaultValue : UiTokenValue();
}

bool sameUiAppearancePreferences(const UiAppearancePreferences& left, const UiAppearancePreferences& right)
{
	if (left.activePreset != right.activePreset || left.themes.size() != right.themes.size()) return false;
	std::map<UiPresetId, UiThemeCustomization>::const_iterator a = left.themes.begin();
	std::map<UiPresetId, UiThemeCustomization>::const_iterator b = right.themes.begin();
	for (; a != left.themes.end(); ++a, ++b)
	{
		if (a->first != b->first || a->second.palette != b->second.palette || a->second.overrides.size() != b->second.overrides.size()) return false;
		std::map<std::string, UiTokenValue>::const_iterator x = a->second.overrides.begin();
		std::map<std::string, UiTokenValue>::const_iterator y = b->second.overrides.begin();
		for (; x != a->second.overrides.end(); ++x, ++y)
		{
			if (x->first != y->first || x->second.type != y->second.type) return false;
			if (x->second.type == UiTokenType::Color &&
				(x->second.color.r != y->second.color.r || x->second.color.g != y->second.color.g ||
				 x->second.color.b != y->second.color.b || x->second.color.a != y->second.color.a)) return false;
			if (x->second.type == UiTokenType::Number && x->second.number != y->second.number) return false;
			if (x->second.type == UiTokenType::Boolean && x->second.boolean != y->second.boolean) return false;
			if (x->second.type == UiTokenType::Choice && x->second.text != y->second.text) return false;
			if (x->second.type == UiTokenType::Font &&
				(x->second.font.family != y->second.font.family || x->second.font.style != y->second.font.style)) return false;
		}
	}
	return true;
}

std::string encodeUiTokenValue(const UiTokenValue& value) { return encodeValue(value); }
bool decodeUiTokenValue(const std::string& encoded, UiTokenType type, UiTokenValue& result) { return decodeValue(encoded, type, result); }

std::string serializeUiAppearanceProfile(UiPresetId preset, const UiThemeCustomization& theme)
{
	std::ostringstream out;
	out << "format=agss-ui\nversion=1\npreset=" << uiPresetStableId(preset) << "\npalette=";
	switch (theme.palette)
	{
	case PaletteId::Amber: out << "amber"; break;
	case PaletteId::Monochrome: out << "monochrome"; break;
	case PaletteId::Light: out << "light"; break;
	case PaletteId::VgaClassic: out << "vga-classic"; break;
	case PaletteId::Windows95: out << "windows-95"; break;
	default: out << "cyan"; break;
	}
	out << '\n';
	for (std::map<std::string, UiTokenValue>::const_iterator item = theme.overrides.begin(); item != theme.overrides.end(); ++item)
		out << "override." << item->first << '=' << encodeValue(item->second) << '\n';
	return out.str();
}

bool parseUiAppearanceProfile(const std::string& contents, UiAppearanceProfile& profile, std::string* error)
{
	std::map<std::string, std::string> entries;
	const std::string normalized = contents.compare(0, 3, "\xEF\xBB\xBF") == 0 ? contents.substr(3) : contents;
	std::istringstream input(normalized);
	std::string line;
	while (std::getline(input, line))
	{
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		const std::string::size_type split = line.find('=');
		if (split == std::string::npos) continue;
		entries[line.substr(0, split)] = line.substr(split + 1);
	}
	if (entries["format"] != "agss-ui")
	{
		if (error) *error = "This file is not an AGSS appearance profile.";
		return false;
	}
	if (entries["version"] != "1")
	{
		if (error) *error = "The appearance profile version is not supported.";
		return false;
	}
	UiAppearanceProfile candidate;
	if (!parseUiPresetId(entries["preset"], candidate.preset))
	{
		if (error) *error = "The profile refers to an unavailable base preset.";
		return false;
	}
	const std::string& palette = entries["palette"];
	if (palette == "cyan") candidate.theme.palette = PaletteId::Cyan;
	else if (palette == "amber") candidate.theme.palette = PaletteId::Amber;
	else if (palette == "monochrome") candidate.theme.palette = PaletteId::Monochrome;
	else if (palette == "light") candidate.theme.palette = PaletteId::Light;
	else if (palette == "vga-classic") candidate.theme.palette = PaletteId::VgaClassic;
	else if (palette == "windows-95") candidate.theme.palette = PaletteId::Windows95;
	else
	{
		if (error) *error = "The profile contains an unavailable palette.";
		return false;
	}
	for (std::map<std::string, std::string>::const_iterator item = entries.begin(); item != entries.end(); ++item)
	{
		if (item->first.compare(0, 9, "override.") != 0) continue;
		const std::string id = item->first.substr(9);
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(candidate.preset), id);
		if (!descriptor) { candidate.ignoredTokens.push_back(id); continue; }
		UiTokenValue parsed;
		if (!decodeValue(item->second, descriptor->type, parsed) ||
			!setUiTokenValue(candidate.preset, candidate.theme, id, parsed, error))
		{
			if (error && error->empty()) *error = "The profile contains an invalid value for " + id + ".";
			return false;
		}
	}
	if (!validateUiThemeCustomization(candidate.preset, candidate.theme, error)) return false;
	profile = candidate;
	if (error) error->clear();
	return true;
}
