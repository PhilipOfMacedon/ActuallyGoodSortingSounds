#pragma once

#include "AppActions.h"
#include "../SoundEngine/SoundEngine.h"
#include "../SoundEngine/ReconstructedSound.h"

namespace audioSettingsActions
{
	// A mismatched action kind is ignored. Valid actions are normalized through
	// the sound-engine value contract before returning.
	SoundEngine::Parameters apply(const SoundEngine::Parameters& current, const AppAction& action);
	SoundEngine::ReconstructedParameters apply(const SoundEngine::ReconstructedParameters& current,
		const AppAction& action);
}
