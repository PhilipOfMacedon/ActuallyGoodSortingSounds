#include "UiAppearance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace
{
	const char* const colorTokenIds[] = { "color.background", "color.bar", "color.compared", "color.held",
		"color.shifted", "color.write_target", "color.gap", "color.panel", "color.button",
		"color.disabled_button", "color.value_button", "color.accent", "color.run", "color.play_active",
		"color.timeline_rail", "color.timeline_thumb", "color.reset", "color.text", "color.muted_text",
		"color.overlay_dim", "color.border", "color.chart_background", "color.workspace_background" };
	const UiColor builtInColors[][21] =
	{
		{ { .10f,.10f,.10f,1 },{ .22f,.71f,.84f,1 },{ .98f,.62f,.26f,1 },{ 1,.83f,.28f,1 },{ .67f,.46f,.96f,1 },{ .35f,.84f,.58f,1 },{ .96f,.28f,.54f,1 },{ .10f,.16f,.20f,1 },{ .13f,.24f,.31f,1 },{ .10f,.16f,.19f,1 },{ .10f,.16f,.20f,1 },{ .29f,.72f,.64f,1 },{ .48f,.28f,.12f,1 },{ .18f,.42f,.34f,1 },{ .09f,.14f,.17f,1 },{ .91f,.78f,.34f,1 },{ .30f,.21f,.35f,1 },{ .93f,.96f,.98f,1 },{ .68f,.76f,.79f,1 },{ 0,0,0,.58f },{ .25f,.38f,.43f,1 } },
		{ { .075f,.055f,.035f,1 },{ .90f,.48f,.16f,1 },{ 1,.66f,.23f,1 },{ 1,.86f,.48f,1 },{ .79f,.43f,.73f,1 },{ .41f,.76f,.43f,1 },{ .98f,.31f,.18f,1 },{ .15f,.11f,.07f,1 },{ .27f,.18f,.09f,1 },{ .14f,.11f,.08f,1 },{ .17f,.12f,.07f,1 },{ .86f,.54f,.19f,1 },{ .48f,.23f,.07f,1 },{ .20f,.39f,.24f,1 },{ .13f,.10f,.07f,1 },{ .97f,.74f,.30f,1 },{ .36f,.19f,.08f,1 },{ 1,.89f,.69f,1 },{ .72f,.59f,.39f,1 },{ 0,0,0,.62f },{ .52f,.34f,.16f,1 } },
		{ { .015f,.015f,.015f,1 },{ .88f,.88f,.88f,1 },{ .60f,.60f,.60f,1 },{ 1,1,1,1 },{ .43f,.43f,.43f,1 },{ .74f,.74f,.74f,1 },{ 1,1,1,1 },{ .07f,.07f,.07f,1 },{ .16f,.16f,.16f,1 },{ .08f,.08f,.08f,1 },{ .11f,.11f,.11f,1 },{ .42f,.42f,.42f,1 },{ .28f,.28f,.28f,1 },{ .22f,.22f,.22f,1 },{ .07f,.07f,.07f,1 },{ .95f,.95f,.95f,1 },{ .25f,.25f,.25f,1 },{ 1,1,1,1 },{ .70f,.70f,.70f,1 },{ 0,0,0,.68f },{ .63f,.63f,.63f,1 } },
		{ { .91f,.93f,.94f,1 },{ .08f,.47f,.63f,1 },{ .86f,.37f,.09f,1 },{ .82f,.61f,.06f,1 },{ .48f,.31f,.75f,1 },{ .12f,.55f,.32f,1 },{ .80f,.12f,.26f,1 },{ .82f,.85f,.86f,1 },{ .72f,.79f,.81f,1 },{ .79f,.82f,.83f,1 },{ .85f,.87f,.88f,1 },{ .09f,.43f,.51f,1 },{ .78f,.35f,.08f,1 },{ .15f,.48f,.33f,1 },{ .78f,.82f,.83f,1 },{ .76f,.55f,.04f,1 },{ .43f,.27f,.51f,1 },{ .07f,.10f,.12f,1 },{ .31f,.37f,.39f,1 },{ .12f,.16f,.18f,.50f },{ .35f,.43f,.45f,1 } },
		{ { .035f,.040f,.052f,1 },{ 0,.79f,.86f,1 },{ 1,.55f,.05f,1 },{ .12f,.92f,.22f,1 },{ .72f,.30f,.88f,1 },{ .98f,.62f,.05f,1 },{ .10f,.13f,.18f,1 },{ .08f,.14f,.25f,1 },{ .13f,.20f,.32f,1 },{ .07f,.11f,.18f,1 },{ .08f,.14f,.23f,1 },{ .05f,.66f,.77f,1 },{ .13f,.20f,.32f,1 },{ .06f,.55f,.18f,1 },{ .25f,.32f,.43f,1 },{ .84f,.89f,.95f,1 },{ .14f,.20f,.33f,1 },{ .90f,.94f,.99f,1 },{ .64f,.73f,.84f,1 },{ 0,0,0,.68f },{ .60f,.68f,.79f,1 } },
		{ { .025f,.035f,.055f,1 },{ .08f,.48f,.62f,1 },{ 1,.58f,.04f,1 },{ .15f,.92f,.24f,1 },{ .75f,.35f,.85f,1 },{ .98f,.62f,.02f,1 },{ .77f,.77f,.77f,1 },{ .06f,.13f,.25f,1 },{ .75f,.75f,.75f,1 },{ .38f,.38f,.38f,1 },{ .75f,.75f,.75f,1 },{ .08f,.26f,.62f,1 },{ .55f,.55f,.55f,1 },{ .12f,.47f,.18f,1 },{ .30f,.30f,.30f,1 },{ .86f,.86f,.86f,1 },{ .32f,.32f,.32f,1 },{ .05f,.07f,.10f,1 },{ .73f,.73f,.73f,1 },{ 0,0,0,.55f },{ .28f,.28f,.28f,1 } }
	};
	std::string paletteStableId(PaletteId id)
	{
		switch (id) { case PaletteId::Amber:return "amber"; case PaletteId::Monochrome:return "monochrome";
		case PaletteId::Light:return "light"; case PaletteId::VgaClassic:return "vga-classic";
		case PaletteId::Windows95:return "windows-95"; case PaletteId::Customized:return "customized"; default:return "cyan"; }
	}
	bool parsePaletteStableId(const std::string& id, PaletteId& result)
	{
		if (id == "cyan") result=PaletteId::Cyan; else if(id=="amber") result=PaletteId::Amber;
		else if(id=="monochrome") result=PaletteId::Monochrome; else if(id=="light") result=PaletteId::Light;
		else if(id=="vga-classic") result=PaletteId::VgaClassic; else if(id=="windows-95") result=PaletteId::Windows95;
		else if(id=="customized") result=PaletteId::Customized; else return false; return true;
	}
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
			{ "color.border", UiColor(0.25f,0.38f,0.43f,1), "Control borders" },
			{ "color.chart_background", UiColor(0.10f,0.10f,0.10f,1), "Main array chart background" },
			{ "color.workspace_background", UiColor(0.10f,0.10f,0.10f,1), "Auxiliary workspace lane backgrounds" }
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
	case PaletteId::Customized: return "Customized";
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
		PaletteId::Windows95,
		PaletteId::Customized
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
	value.customPaletteBase = PaletteId::Cyan;
	return value;
}

UiColor uiPaletteColor(PaletteId palette, const std::string& tokenId)
{
	std::size_t paletteIndex = static_cast<std::size_t>(palette);
	if (paletteIndex >= sizeof(builtInColors) / sizeof(builtInColors[0])) paletteIndex = 0;
	// New chart surfaces inherit the prior frame-clear color by default. This
	// keeps old palettes visually unchanged while allowing the two surfaces to
	// be customized independently.
	if (tokenId == "color.chart_background" || tokenId == "color.workspace_background")
		return builtInColors[paletteIndex][0];
	for (std::size_t i = 0; i < sizeof(colorTokenIds) / sizeof(colorTokenIds[0]); ++i)
	{
		if (i >= sizeof(builtInColors[0]) / sizeof(builtInColors[0][0])) break;
		if (tokenId == colorTokenIds[i]) return builtInColors[paletteIndex][i];
	}
	return UiColor();
}

UiColor uiPresetPaletteColor(UiPresetId preset, PaletteId palette, const std::string& tokenId)
{
	UiColor color = uiPaletteColor(palette == PaletteId::Customized ? PaletteId::Cyan : palette, tokenId);
	if (preset == UiPresetId::ClassicWindows95 && palette == PaletteId::Windows95)
	{
		if (tokenId == "color.panel") color = uiPaletteColor(palette, "color.button");
		else if (tokenId == "color.background") color = uiPaletteColor(palette, "color.button");
		else if (tokenId == "color.chart_background" || tokenId == "color.workspace_background")
			color = uiPaletteColor(palette, "color.button");
		else if (tokenId == "color.muted_text") color = UiColor(.32f,.32f,.32f,1.0f);
	}
	return color;
}

std::map<std::string, UiColor> effectiveUiPalette(UiPresetId preset, const UiThemeCustomization& value)
{
	std::map<std::string, UiColor> colors;
	for (std::size_t i = 0; i < sizeof(colorTokenIds) / sizeof(colorTokenIds[0]); ++i)
	{
		const std::string id(colorTokenIds[i]);
		UiColor color = uiPresetPaletteColor(preset, value.palette == PaletteId::Customized ? value.customPaletteBase : value.palette, id);
		if (value.palette == PaletteId::Customized)
		{
			std::map<std::string, UiColor>::const_iterator custom = value.customColors.find(id);
			if (custom != value.customColors.end()) color = custom->second;
		}
		std::map<std::string, UiTokenValue>::const_iterator legacy = value.overrides.find(id);
		if (legacy != value.overrides.end() && legacy->second.type == UiTokenType::Color) color = legacy->second.color;
		colors[id] = color;
	}
	if (preset == UiPresetId::ClassicWindows95 && value.palette == PaletteId::Windows95)
	{
		if (value.overrides.count("color.panel") == 0) colors["color.panel"] = colors["color.button"];
		if (value.overrides.count("color.background") == 0) colors["color.background"] = colors["color.panel"];
		if (value.overrides.count("color.muted_text") == 0) colors["color.muted_text"] = UiColor(.32f,.32f,.32f,1.0f);
	}
	const char* inheritedSurfaces[] = { "color.chart_background", "color.workspace_background" };
	for (std::size_t i = 0; i < sizeof(inheritedSurfaces) / sizeof(inheritedSurfaces[0]); ++i)
	{
		const std::string id(inheritedSurfaces[i]);
		const bool customValueExists = value.palette == PaletteId::Customized && value.customColors.count(id) != 0;
		if (!customValueExists && value.overrides.count(id) == 0) colors[id] = colors["color.background"];
	}
	return colors;
}

bool copyEffectiveUiPalette(UiPresetId sourcePreset, const UiThemeCustomization& source,
	UiPresetId destinationPreset, UiThemeCustomization& destination)
{
	const std::map<std::string, UiColor> colors = effectiveUiPalette(sourcePreset, source);
	for (std::size_t i = 0; i < sizeof(colorTokenIds) / sizeof(colorTokenIds[0]); ++i)
	{
		const std::string id(colorTokenIds[i]);
		if (!findUiToken(uiPresetDefinition(destinationPreset), id)) return false;
	}
	destination.customColors = colors;
	destination.customPaletteBase = source.palette == PaletteId::Customized ? source.customPaletteBase : source.palette;
	destination.palette = PaletteId::Customized;
	for (std::map<std::string, UiTokenValue>::iterator i = destination.overrides.begin(); i != destination.overrides.end(); )
		if (i->second.type == UiTokenType::Color) destination.overrides.erase(i++); else ++i;
	return true;
}

bool validateUiThemeCustomization(UiPresetId preset, const UiThemeCustomization& value, std::string* error)
{
	if (static_cast<int>(value.palette) < static_cast<int>(PaletteId::Cyan) || static_cast<int>(value.palette) > static_cast<int>(PaletteId::Customized) ||
		static_cast<int>(value.customPaletteBase) < static_cast<int>(PaletteId::Cyan) || static_cast<int>(value.customPaletteBase) > static_cast<int>(PaletteId::Windows95))
	{
		if (error) *error = "The selected palette is not registered.";
		return false;
	}
	const UiPresetDefinition& definition = uiPresetDefinition(preset);
	for (std::map<std::string, UiColor>::const_iterator item = value.customColors.begin(); item != value.customColors.end(); ++item)
		if (!findUiToken(definition, item->first) || item->first.compare(0, 6, "color.") != 0 || !validColor(item->second))
		{
			if (error) *error = "Custom palette contains an invalid color token: " + item->first;
			return false;
		}
	// Older saved custom palettes are allowed to omit newly registered tokens.
	// Effective palette resolution supplies their inherited defaults, and the
	// next explicit color edit materializes the complete current token set.
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
	if (descriptor->category == UiTokenCategory::Colors && tokenValue.type == UiTokenType::Color)
	{
		if (candidate.palette != PaletteId::Customized)
		{
			candidate.customPaletteBase = candidate.palette;
			candidate.customColors = effectiveUiPalette(preset, candidate);
			candidate.palette = PaletteId::Customized;
		}
		else if (candidate.customColors.size() < sizeof(colorTokenIds) / sizeof(colorTokenIds[0]))
		{
			const std::map<std::string, UiColor> base = effectiveUiPalette(preset,
				defaultUiThemeCustomization(preset, candidate.customPaletteBase));
			for (std::map<std::string, UiColor>::const_iterator color = base.begin(); color != base.end(); ++color)
				if (candidate.customColors.find(color->first) == candidate.customColors.end()) candidate.customColors[color->first] = color->second;
		}
		candidate.customColors[tokenId] = tokenValue.color;
		candidate.overrides.erase(tokenId);
	}
	else candidate.overrides[tokenId] = tokenValue;
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

void selectUiPalette(UiPresetId preset, UiThemeCustomization& value, PaletteId palette)
{
	if (palette == PaletteId::Customized && value.customColors.empty())
	{
		value.customPaletteBase = value.palette;
		value.customColors = effectiveUiPalette(preset, value);
	}
	selectUiPalette(value, palette);
}

UiTokenValue effectiveUiTokenValue(UiPresetId preset, const UiThemeCustomization& value, const std::string& tokenId)
{
	const std::map<std::string, UiTokenValue>::const_iterator overrideValue = value.overrides.find(tokenId);
	if (overrideValue != value.overrides.end()) return overrideValue->second;
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), tokenId);
	if (descriptor && descriptor->type == UiTokenType::Color)
	{
		const std::map<std::string, UiColor> colors = effectiveUiPalette(preset, value);
		const std::map<std::string, UiColor>::const_iterator found = colors.find(tokenId);
		if (found != colors.end()) return UiTokenValue::fromColor(found->second);
	}
	return descriptor ? descriptor->defaultValue : UiTokenValue();
}

bool sameUiAppearancePreferences(const UiAppearancePreferences& left, const UiAppearancePreferences& right)
{
	if (left.activePreset != right.activePreset || left.themes.size() != right.themes.size()) return false;
	std::map<UiPresetId, UiThemeCustomization>::const_iterator a = left.themes.begin();
	std::map<UiPresetId, UiThemeCustomization>::const_iterator b = right.themes.begin();
	for (; a != left.themes.end(); ++a, ++b)
	{
		if (a->first != b->first || a->second.palette != b->second.palette || a->second.customPaletteBase != b->second.customPaletteBase ||
			a->second.customColors.size() != b->second.customColors.size() || a->second.overrides.size() != b->second.overrides.size()) return false;
		std::map<std::string, UiColor>::const_iterator ca = a->second.customColors.begin();
		std::map<std::string, UiColor>::const_iterator cb = b->second.customColors.begin();
		for (; ca != a->second.customColors.end(); ++ca, ++cb)
			if (ca->first != cb->first || ca->second.r != cb->second.r || ca->second.g != cb->second.g ||
				ca->second.b != cb->second.b || ca->second.a != cb->second.a) return false;
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
	out << "format=agss-ui\nversion=2\npreset=" << uiPresetStableId(preset) << "\npalette=" << paletteStableId(theme.palette) << '\n';
	if (theme.palette == PaletteId::Customized)
		out << "custom_base=" << paletteStableId(theme.customPaletteBase) << '\n';
	for (std::map<std::string, UiColor>::const_iterator item = theme.customColors.begin(); item != theme.customColors.end(); ++item)
		out << "custom." << item->first << "=color:" << std::setprecision(9) << item->second.r << ',' << item->second.g << ',' << item->second.b << ',' << item->second.a << '\n';
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
	const bool version1 = entries["version"] == "1";
	const bool version2 = entries["version"] == "2";
	if (!version1 && !version2)
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
	if (!parsePaletteStableId(entries["palette"], candidate.theme.palette))
	{
		if (error) *error = "The profile contains an unavailable palette.";
		return false;
	}
	if (candidate.theme.palette == PaletteId::Customized)
	{
		if (!version2 || !parsePaletteStableId(entries["custom_base"], candidate.theme.customPaletteBase) ||
			candidate.theme.customPaletteBase == PaletteId::Customized)
		{
			if (error) *error = "The profile contains an invalid customized palette base.";
			return false;
		}
	}
	for (std::map<std::string, std::string>::const_iterator item = entries.begin(); item != entries.end(); ++item)
	{
		if (item->first.compare(0, 7, "custom.") == 0)
		{
			const std::string id = item->first.substr(7);
			const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(candidate.preset), id);
			UiTokenValue parsed;
			if (!descriptor) { candidate.ignoredTokens.push_back(id); continue; }
			if (descriptor->type != UiTokenType::Color || !decodeValue(item->second, UiTokenType::Color, parsed) || !validColor(parsed.color))
			{
				if (error) *error = "The profile contains an invalid custom color for " + id + ".";
				return false;
			}
			candidate.theme.customColors[id] = parsed.color;
			continue;
		}
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
