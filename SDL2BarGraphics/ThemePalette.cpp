#include "ThemePalette.h"

namespace
{
	glm::vec4 toGlm(const UiColor& color)
	{
		return glm::vec4(color.r, color.g, color.b, color.a);
	}

	ThemePalette makeThemePalette(const std::map<std::string, UiColor>& colors)
	{
		ThemePalette result;
		const char* ids[] = {
			"color.background", "color.chart_background", "color.workspace_background", "color.bar", "color.compared", "color.held", "color.shifted",
			"color.write_target", "color.gap", "color.panel", "color.button", "color.disabled_button",
			"color.value_button", "color.accent", "color.run", "color.play_active", "color.timeline_rail",
			"color.timeline_thumb", "color.reset", "color.text", "color.muted_text", "color.overlay_dim", "color.border"
		};
		glm::vec4* fields[] = {
			&result.background, &result.chartBackground, &result.workspaceBackground, &result.bar, &result.compared, &result.held, &result.shifted,
			&result.writeTarget, &result.gap, &result.panel, &result.button, &result.disabledButton,
			&result.valueButton, &result.accent, &result.run, &result.playActive, &result.timelineRail,
			&result.timelineThumb, &result.reset, &result.text, &result.mutedText, &result.overlayDim, &result.border
		};
		for (std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
		{
			const std::map<std::string, UiColor>::const_iterator color = colors.find(ids[i]);
			*fields[i] = color == colors.end() ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) : toGlm(color->second);
		}
		return result;
	}
}

const ThemePalette& paletteFor(PaletteId id)
{
	// This legacy accessor has no UI preset context, so it resolves the palette
	// against the application's standard frame. Customized colors are selected
	// through paletteForAppearance, which receives the owning preset's data.
	static ThemePalette palettes[7];
	static bool initialized = false;
	if (!initialized)
	{
		const PaletteId builtIns[] = { PaletteId::Cyan, PaletteId::Amber, PaletteId::Monochrome,
			PaletteId::Light, PaletteId::VgaClassic, PaletteId::Windows95, PaletteId::Customized };
		for (std::size_t i = 0; i < sizeof(builtIns) / sizeof(builtIns[0]); ++i)
		{
			const PaletteId palette = builtIns[i] == PaletteId::Customized ? PaletteId::Cyan : builtIns[i];
			const std::map<std::string, UiColor> colors = effectiveUiPalette(UiPresetId::Current,
				defaultUiThemeCustomization(UiPresetId::Current, palette));
			palettes[i] = makeThemePalette(colors);
		}
		initialized = true;
	}
	const int index = static_cast<int>(id);
	return palettes[index >= 0 && index < 7 ? index : 0];
}

ThemePalette paletteForAppearance(UiPresetId preset, const UiThemeCustomization& customization)
{
	return makeThemePalette(effectiveUiPalette(preset, customization));
}

ThemePalette uiPaletteForAppearance(UiPresetId preset, const UiThemeCustomization& customization)
{
	return paletteForAppearance(preset, customization);
}
