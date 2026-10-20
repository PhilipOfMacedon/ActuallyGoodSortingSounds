#pragma once

#include <string>

#include "UiArchitecture.h"

struct UserPreferences;

// Owns the retained main-control tree and the typed actions emitted by it.
// SDL input translation, responsive placement, and renderer resources stay in
// the application adapter.
class MainControlsView
{
public:
	void initializeControls(const UserPreferences& preferences, const std::string& algorithmLabel);

	UIRoot& controls() { return controls_; }
	const UIRoot& controls() const { return controls_; }

private:
	UIRoot controls_;
};
