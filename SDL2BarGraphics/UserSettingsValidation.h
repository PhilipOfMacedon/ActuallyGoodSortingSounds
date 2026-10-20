#pragma once

#include "UserSettings.h"

namespace userSettingsValidation
{
	bool isFiniteInRange(float value, float minimum, float maximum);
	bool isValidAudioParameters(const SoundEngine::Parameters& parameters);
	bool isValidReconstructedAudioParameters(const SoundEngine::ReconstructedParameters& parameters);
}
