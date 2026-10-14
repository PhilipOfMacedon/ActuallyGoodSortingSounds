#include "ThemePalette.h"

namespace
{
	const ThemePalette palettes[] =
	{
		{
			"cyan", "Current Cyan",
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
			"amber", "Amber Terminal",
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
			"monochrome", "High Contrast",
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
			"light", "Paper Light",
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

const char* paletteDisplayName(PaletteId id)
{
	return paletteFor(id).displayName;
}

const std::vector<PaletteId>& paletteCatalog()
{
	static const std::vector<PaletteId> catalog =
	{
		PaletteId::Cyan,
		PaletteId::Amber,
		PaletteId::Monochrome,
		PaletteId::Light
	};
	return catalog;
}
