#include "ThemePalette.h"

namespace
{
	const ThemePalette palettes[] =
	{
		{
			glm::vec4(0.10f, 0.10f, 0.10f, 1.0f),
			glm::vec4(0.22f, 0.71f, 0.84f, 1.0f),
			glm::vec4(0.98f, 0.62f, 0.26f, 1.0f),
			glm::vec4(1.0f, 0.83f, 0.28f, 1.0f),
			glm::vec4(0.67f, 0.46f, 0.96f, 1.0f),
			glm::vec4(0.35f, 0.84f, 0.58f, 1.0f),
			glm::vec4(0.96f, 0.28f, 0.54f, 1.0f),
			glm::vec4(0.10f, 0.16f, 0.20f, 1.0f),
			glm::vec4(0.13f, 0.24f, 0.31f, 1.0f),
			glm::vec4(0.10f, 0.16f, 0.19f, 1.0f),
			glm::vec4(0.10f, 0.16f, 0.20f, 1.0f),
			glm::vec4(0.29f, 0.72f, 0.64f, 1.0f),
			glm::vec4(0.48f, 0.28f, 0.12f, 1.0f),
			glm::vec4(0.18f, 0.42f, 0.34f, 1.0f),
			glm::vec4(0.09f, 0.14f, 0.17f, 1.0f),
			glm::vec4(0.91f, 0.78f, 0.34f, 1.0f),
			glm::vec4(0.30f, 0.21f, 0.35f, 1.0f),
			glm::vec4(0.93f, 0.96f, 0.98f, 1.0f),
			glm::vec4(0.68f, 0.76f, 0.79f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.58f),
			glm::vec4(0.25f, 0.38f, 0.43f, 1.0f)
		},
		{
			glm::vec4(0.075f, 0.055f, 0.035f, 1.0f),
			glm::vec4(0.90f, 0.48f, 0.16f, 1.0f),
			glm::vec4(1.0f, 0.66f, 0.23f, 1.0f),
			glm::vec4(1.0f, 0.86f, 0.48f, 1.0f),
			glm::vec4(0.79f, 0.43f, 0.73f, 1.0f),
			glm::vec4(0.41f, 0.76f, 0.43f, 1.0f),
			glm::vec4(0.98f, 0.31f, 0.18f, 1.0f),
			glm::vec4(0.15f, 0.11f, 0.07f, 1.0f),
			glm::vec4(0.27f, 0.18f, 0.09f, 1.0f),
			glm::vec4(0.14f, 0.11f, 0.08f, 1.0f),
			glm::vec4(0.17f, 0.12f, 0.07f, 1.0f),
			glm::vec4(0.86f, 0.54f, 0.19f, 1.0f),
			glm::vec4(0.48f, 0.23f, 0.07f, 1.0f),
			glm::vec4(0.20f, 0.39f, 0.24f, 1.0f),
			glm::vec4(0.13f, 0.10f, 0.07f, 1.0f),
			glm::vec4(0.97f, 0.74f, 0.30f, 1.0f),
			glm::vec4(0.36f, 0.19f, 0.08f, 1.0f),
			glm::vec4(1.0f, 0.89f, 0.69f, 1.0f),
			glm::vec4(0.72f, 0.59f, 0.39f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.62f),
			glm::vec4(0.52f, 0.34f, 0.16f, 1.0f)
		},
		{
			glm::vec4(0.015f, 0.015f, 0.015f, 1.0f),
			glm::vec4(0.88f, 0.88f, 0.88f, 1.0f),
			glm::vec4(0.60f, 0.60f, 0.60f, 1.0f),
			glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
			glm::vec4(0.43f, 0.43f, 0.43f, 1.0f),
			glm::vec4(0.74f, 0.74f, 0.74f, 1.0f),
			glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
			glm::vec4(0.07f, 0.07f, 0.07f, 1.0f),
			glm::vec4(0.16f, 0.16f, 0.16f, 1.0f),
			glm::vec4(0.08f, 0.08f, 0.08f, 1.0f),
			glm::vec4(0.11f, 0.11f, 0.11f, 1.0f),
			glm::vec4(0.42f, 0.42f, 0.42f, 1.0f),
			glm::vec4(0.28f, 0.28f, 0.28f, 1.0f),
			glm::vec4(0.22f, 0.22f, 0.22f, 1.0f),
			glm::vec4(0.07f, 0.07f, 0.07f, 1.0f),
			glm::vec4(0.95f, 0.95f, 0.95f, 1.0f),
			glm::vec4(0.25f, 0.25f, 0.25f, 1.0f),
			glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
			glm::vec4(0.70f, 0.70f, 0.70f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.68f),
			glm::vec4(0.63f, 0.63f, 0.63f, 1.0f)
		},
		{
			glm::vec4(0.91f, 0.93f, 0.94f, 1.0f),
			glm::vec4(0.08f, 0.47f, 0.63f, 1.0f),
			glm::vec4(0.86f, 0.37f, 0.09f, 1.0f),
			glm::vec4(0.82f, 0.61f, 0.06f, 1.0f),
			glm::vec4(0.48f, 0.31f, 0.75f, 1.0f),
			glm::vec4(0.12f, 0.55f, 0.32f, 1.0f),
			glm::vec4(0.80f, 0.12f, 0.26f, 1.0f),
			glm::vec4(0.82f, 0.85f, 0.86f, 1.0f),
			glm::vec4(0.72f, 0.79f, 0.81f, 1.0f),
			glm::vec4(0.79f, 0.82f, 0.83f, 1.0f),
			glm::vec4(0.85f, 0.87f, 0.88f, 1.0f),
			glm::vec4(0.09f, 0.43f, 0.51f, 1.0f),
			glm::vec4(0.78f, 0.35f, 0.08f, 1.0f),
			glm::vec4(0.15f, 0.48f, 0.33f, 1.0f),
			glm::vec4(0.78f, 0.82f, 0.83f, 1.0f),
			glm::vec4(0.76f, 0.55f, 0.04f, 1.0f),
			glm::vec4(0.43f, 0.27f, 0.51f, 1.0f),
			glm::vec4(0.07f, 0.10f, 0.12f, 1.0f),
			glm::vec4(0.31f, 0.37f, 0.39f, 1.0f),
			glm::vec4(0.12f, 0.16f, 0.18f, 0.50f),
			glm::vec4(0.35f, 0.43f, 0.45f, 1.0f)
		},
		{
			glm::vec4(0.035f, 0.040f, 0.052f, 1.0f),
			glm::vec4(0.00f, 0.79f, 0.86f, 1.0f),
			glm::vec4(1.00f, 0.55f, 0.05f, 1.0f),
			glm::vec4(0.12f, 0.92f, 0.22f, 1.0f),
			glm::vec4(0.72f, 0.30f, 0.88f, 1.0f),
			glm::vec4(0.98f, 0.62f, 0.05f, 1.0f),
			glm::vec4(0.10f, 0.13f, 0.18f, 1.0f),
			glm::vec4(0.08f, 0.14f, 0.25f, 1.0f),
			glm::vec4(0.13f, 0.20f, 0.32f, 1.0f),
			glm::vec4(0.07f, 0.11f, 0.18f, 1.0f),
			glm::vec4(0.08f, 0.14f, 0.23f, 1.0f),
			glm::vec4(0.05f, 0.66f, 0.77f, 1.0f),
			glm::vec4(0.13f, 0.20f, 0.32f, 1.0f),
			glm::vec4(0.06f, 0.55f, 0.18f, 1.0f),
			glm::vec4(0.25f, 0.32f, 0.43f, 1.0f),
			glm::vec4(0.84f, 0.89f, 0.95f, 1.0f),
			glm::vec4(0.14f, 0.20f, 0.33f, 1.0f),
			glm::vec4(0.90f, 0.94f, 0.99f, 1.0f),
			glm::vec4(0.64f, 0.73f, 0.84f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.68f),
			glm::vec4(0.60f, 0.68f, 0.79f, 1.0f)
		},
		{
			glm::vec4(0.025f, 0.035f, 0.055f, 1.0f),
			glm::vec4(0.00f, 0.88f, 0.90f, 1.0f),
			glm::vec4(1.00f, 0.58f, 0.04f, 1.0f),
			glm::vec4(0.15f, 0.92f, 0.24f, 1.0f),
			glm::vec4(0.75f, 0.35f, 0.85f, 1.0f),
			glm::vec4(0.98f, 0.62f, 0.02f, 1.0f),
			glm::vec4(0.77f, 0.77f, 0.77f, 1.0f),
			glm::vec4(0.06f, 0.13f, 0.25f, 1.0f),
			glm::vec4(0.75f, 0.75f, 0.75f, 1.0f),
			glm::vec4(0.38f, 0.38f, 0.38f, 1.0f),
			glm::vec4(0.75f, 0.75f, 0.75f, 1.0f),
			glm::vec4(0.08f, 0.26f, 0.62f, 1.0f),
			glm::vec4(0.55f, 0.55f, 0.55f, 1.0f),
			glm::vec4(0.12f, 0.47f, 0.18f, 1.0f),
			glm::vec4(0.30f, 0.30f, 0.30f, 1.0f),
			glm::vec4(0.86f, 0.86f, 0.86f, 1.0f),
			glm::vec4(0.32f, 0.32f, 0.32f, 1.0f),
			glm::vec4(0.05f, 0.07f, 0.10f, 1.0f),
			glm::vec4(0.73f, 0.73f, 0.73f, 1.0f),
			glm::vec4(0.0f, 0.0f, 0.0f, 0.55f),
			glm::vec4(0.28f, 0.28f, 0.28f, 1.0f)
		}
	};
}

const ThemePalette& paletteFor(PaletteId id)
{
	const std::vector<PaletteId>& catalog = paletteCatalog();
	for (std::size_t i = 0; i < catalog.size(); ++i)
		if (catalog[i] == id)
			return palettes[i];
	return palettes[0];
}

ThemePalette paletteForAppearance(UiPresetId preset, const UiThemeCustomization& customization)
{
	ThemePalette result = paletteFor(customization.palette);
	const struct ColorBinding { const char* token; glm::vec4 ThemePalette::*member; } bindings[] =
	{
		{ "color.background", &ThemePalette::background }, { "color.bar", &ThemePalette::bar },
		{ "color.compared", &ThemePalette::compared }, { "color.held", &ThemePalette::held },
		{ "color.shifted", &ThemePalette::shifted }, { "color.write_target", &ThemePalette::writeTarget },
		{ "color.gap", &ThemePalette::gap }, { "color.panel", &ThemePalette::panel },
		{ "color.button", &ThemePalette::button }, { "color.disabled_button", &ThemePalette::disabledButton },
		{ "color.value_button", &ThemePalette::valueButton }, { "color.accent", &ThemePalette::accent },
		{ "color.run", &ThemePalette::run }, { "color.play_active", &ThemePalette::playActive },
		{ "color.timeline_rail", &ThemePalette::timelineRail }, { "color.timeline_thumb", &ThemePalette::timelineThumb },
		{ "color.reset", &ThemePalette::reset }, { "color.text", &ThemePalette::text },
		{ "color.muted_text", &ThemePalette::mutedText }, { "color.overlay_dim", &ThemePalette::overlayDim },
		{ "color.border", &ThemePalette::border }
	};
	for (std::size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
	{
		const std::map<std::string, UiTokenValue>::const_iterator value = customization.overrides.find(bindings[i].token);
		if (value == customization.overrides.end() || value->second.type != UiTokenType::Color) continue;
		const UiColor& c = value->second.color;
		result.*(bindings[i].member) = glm::vec4(c.r, c.g, c.b, c.a);
	}
	(void)preset;
	return result;
}
