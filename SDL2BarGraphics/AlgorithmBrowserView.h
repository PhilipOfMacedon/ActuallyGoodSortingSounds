#pragma once

#include "AlgorithmBrowserModel.h"
#include "UiArchitecture.h"

struct AlgorithmBrowserPalette
{
	UiColor background;
	UiColor panel;
	UiColor button;
	UiColor disabledButton;
	UiColor valueButton;
	UiColor accent;
	UiColor text;
	UiColor mutedText;
	UiColor overlayDim;
};

// Composes browser geometry and labels into the shared UI paint contract.
// Production rendering and headless composition tests consume these same commands.
class AlgorithmBrowserView
{
public:
	void compose(const AlgorithmBrowserModel& browser, const AlgorithmBrowserPalette& palette,
		bool setupEnabled, int selectedAlgorithm, int logicalWidth, int logicalHeight,
		UiPaintContext& paintContext) const;
};
