#include "UserSettingsValidation.h"

#include <cmath>

namespace userSettingsValidation
{
	bool isFiniteInRange(float value, float minimum, float maximum)
	{
		return std::isfinite(value) && value >= minimum && value <= maximum;
	}

	bool isValidReconstructedAudioParameters(const SoundEngine::ReconstructedParameters& a)
	{
		return std::isfinite(a.minimumFrequencyHz) && a.minimumFrequencyHz >= 50.0 && a.minimumFrequencyHz <= 1000.0 &&
			std::isfinite(a.maximumFrequencyHz) && a.maximumFrequencyHz >= 1000.0 && a.maximumFrequencyHz <= 8000.0 &&
			a.minimumFrequencyHz < a.maximumFrequencyHz && std::isfinite(a.excitationClockHz) &&
			a.excitationClockHz >= 100.0 && a.excitationClockHz <= 600.0 && std::isfinite(a.carrierCycles) &&
			a.carrierCycles >= 1.0 && a.carrierCycles <= 16.0 && std::isfinite(a.tukeyFraction) &&
			a.tukeyFraction >= 0.0 && a.tukeyFraction <= 1.0 &&
			std::isfinite(a.secondOperandDelayMicroseconds) && a.secondOperandDelayMicroseconds >= 0.0 &&
			a.secondOperandDelayMicroseconds <= 2000.0 && std::isfinite(a.masterLevel) &&
			a.masterLevel >= 0.0 && a.masterLevel <= 1.0 && std::isfinite(a.specialReleaseMilliseconds) &&
			a.specialReleaseMilliseconds >= 0.0 && a.specialReleaseMilliseconds <= 2000.0 &&
			std::isfinite(a.lowPassCutoffHz) && a.lowPassCutoffHz >= 500.0 && a.lowPassCutoffHz <= 20000.0 &&
			static_cast<int>(a.mapping) >= static_cast<int>(SoundEngine::ReconstructedMapping::LinearHz) &&
			static_cast<int>(a.mapping) <= static_cast<int>(SoundEngine::ReconstructedMapping::LogFrequency) &&
			static_cast<int>(a.idlePolicy) >= static_cast<int>(SoundEngine::ReconstructedIdlePolicy::HoldSelection) &&
			static_cast<int>(a.idlePolicy) <= static_cast<int>(SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
	}

	bool isValidAudioParameters(const SoundEngine::Parameters& a)
	{
		if (!isFiniteInRange(a.masterLevel, 0.0f, 1.0f) || !isFiniteInRange(a.bedLevel, 0.0f, 1.0f) ||
			!isFiniteInRange(a.bedHarmonicExponent, 0.5f, 3.0f) || !isFiniteInRange(a.bedComb.delayMicroseconds, 0.0f, 5000.0f) ||
			!isFiniteInRange(a.bedComb.phaseOffsetDegrees, -180.0f, 180.0f) || !isFiniteInRange(a.bedComb.wetAtLowFrequency, 0.0f, 1.0f) ||
			!isFiniteInRange(a.bedComb.wetAtHighFrequency, 0.0f, 1.0f) || !isFiniteInRange(a.bedComb.fadeStartHz, 0.0f, 17100.0f) ||
			!isFiniteInRange(a.bedComb.fadeEndHz, 0.0f, 17100.0f) || !isFiniteInRange(a.bedComb.fadeExponent, 0.25f, 8.0f) ||
			!isFiniteInRange(a.comparisonBoostDb, -60.0f, 30.0f) || !isFiniteInRange(a.heldKeyBoostDb, -60.0f, 30.0f) ||
			!isFiniteInRange(a.tauBelowBins, 0.05f, static_cast<float>(SoundEngine::BandCount)) ||
			!isFiniteInRange(a.tauAboveBins, 0.05f, static_cast<float>(SoundEngine::BandCount)) ||
			!isFiniteInRange(a.chorusMix, 0.0f, 0.5f) || !isFiniteInRange(a.chorusRateHz, 0.1f, 5.0f) ||
			!isFiniteInRange(a.chorusDepthMs, 0.0f, 8.0f) || !isFiniteInRange(a.attackMs, 1.0f, 200.0f) ||
			!isFiniteInRange(a.releaseMs, 1.0f, 2000.0f) || static_cast<int>(a.chorusWeighting) < 0 ||
			static_cast<int>(a.chorusWeighting) > 1 || static_cast<int>(a.mappingMode) < 0 ||
			static_cast<int>(a.mappingMode) > 2 || static_cast<int>(a.comparisonSpreadMode) < 0 ||
			static_cast<int>(a.comparisonSpreadMode) > 1)
			return false;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			if (!isFiniteInRange(a.bandTrimDb[i], -96.0f, 12.0f)) return false;
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
			if (!isFiniteInRange(a.roleBoostDb[i], -60.0f, 12.0f)) return false;
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
			if (!isFiniteInRange(a.accentGain[i], 0.0f, 0.5f)) return false;
		return true;
	}
}
