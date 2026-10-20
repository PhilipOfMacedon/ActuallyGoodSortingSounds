#include "AudioSettingsActions.h"

namespace audioSettingsActions
{
	SoundEngine::Parameters apply(const SoundEngine::Parameters& current, const AppAction& action)
	{
		if (action.kind != AppActionKind::SetAudioParameter) return current;
		SoundEngine::Parameters audio = current;
		switch (action.audioParameter)
		{
		case AudioParameterId::Enabled: audio.enabled = action.selection != 0; break;
		case AudioParameterId::MasterLevel: audio.masterLevel = action.value; break;
		case AudioParameterId::BedLevel: audio.bedLevel = action.value; break;
		case AudioParameterId::BedHarmonicExponent: audio.bedHarmonicExponent = action.value; break;
		case AudioParameterId::IncludeSub300HzBed: audio.includeSub300HzBed = action.selection != 0; break;
		case AudioParameterId::CombEnabled: audio.bedComb.enabled = action.selection != 0; break;
		case AudioParameterId::CombApplyPhase: audio.bedComb.applyPhase = action.selection != 0; break;
		case AudioParameterId::CombDelayMicroseconds: audio.bedComb.delayMicroseconds = action.value; break;
		case AudioParameterId::CombPhaseOffsetDegrees: audio.bedComb.phaseOffsetDegrees = action.value; break;
		case AudioParameterId::CombWetLow: audio.bedComb.wetAtLowFrequency = action.value; break;
		case AudioParameterId::CombWetHigh: audio.bedComb.wetAtHighFrequency = action.value; break;
		case AudioParameterId::CombFadeStartHz: audio.bedComb.fadeStartHz = action.value; break;
		case AudioParameterId::CombFadeEndHz: audio.bedComb.fadeEndHz = action.value; break;
		case AudioParameterId::CombFadeExponent: audio.bedComb.fadeExponent = action.value; break;
		case AudioParameterId::ComparisonBoostDb: audio.comparisonBoostDb = action.value; break;
		case AudioParameterId::HeldKeyBoostDb: audio.heldKeyBoostDb = action.value; break;
		case AudioParameterId::TauBelowBins: audio.tauBelowBins = action.value; break;
		case AudioParameterId::TauAboveBins: audio.tauAboveBins = action.value; break;
		case AudioParameterId::ChorusMix: audio.chorusMix = action.value; break;
		case AudioParameterId::ChorusRateHz: audio.chorusRateHz = action.value; break;
		case AudioParameterId::ChorusDepthMs: audio.chorusDepthMs = action.value; break;
		case AudioParameterId::ChorusWeighting: audio.chorusWeighting = static_cast<SoundEngine::ChorusWeighting>(action.selection); break;
		case AudioParameterId::AttackMs: audio.attackMs = action.value; break;
		case AudioParameterId::ReleaseMs: audio.releaseMs = action.value; break;
		case AudioParameterId::MappingMode: audio.mappingMode = static_cast<SoundEngine::MappingMode>(action.selection); break;
		case AudioParameterId::SpreadMode: audio.comparisonSpreadMode = static_cast<SoundEngine::ComparisonSpreadMode>(action.selection); break;
		case AudioParameterId::BandTrim:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::BandCount)
				audio.bandTrimDb[static_cast<std::size_t>(action.index)] = action.value;
			break;
		case AudioParameterId::AccentRouting:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::AccentCategoryCount)
				audio.accentRouting[static_cast<std::size_t>(action.index)] = action.selection != 0;
			break;
		case AudioParameterId::AccentGain:
			if (action.index >= 0 && static_cast<std::size_t>(action.index) < SoundEngine::AccentCategoryCount)
				audio.accentGain[static_cast<std::size_t>(action.index)] = action.value;
			break;
		}
		return SoundEngine::normalizeParameters(audio);
	}

	SoundEngine::ReconstructedParameters apply(const SoundEngine::ReconstructedParameters& current,
		const AppAction& action)
	{
		if (action.kind != AppActionKind::SetReconstructedAudioParameter) return current;
		SoundEngine::ReconstructedParameters audio = current;
		switch (action.reconstructedAudioParameter)
		{
		case ReconstructedAudioParameterId::Enabled: audio.audioEnabled = action.selection != 0; break;
		case ReconstructedAudioParameterId::MasterLevel: audio.masterLevel = action.value; break;
		case ReconstructedAudioParameterId::IdlePolicy: audio.idlePolicy = static_cast<SoundEngine::ReconstructedIdlePolicy>(action.selection); break;
		case ReconstructedAudioParameterId::SpecialReleaseMilliseconds: audio.specialReleaseMilliseconds = action.value; break;
		case ReconstructedAudioParameterId::MinimumFrequencyHz: audio.minimumFrequencyHz = action.value; break;
		case ReconstructedAudioParameterId::MaximumFrequencyHz: audio.maximumFrequencyHz = action.value; break;
		case ReconstructedAudioParameterId::Mapping: audio.mapping = static_cast<SoundEngine::ReconstructedMapping>(action.selection); break;
		case ReconstructedAudioParameterId::ExcitationClockHz: audio.excitationClockHz = action.value; break;
		case ReconstructedAudioParameterId::CarrierCycles: audio.carrierCycles = action.value; break;
		case ReconstructedAudioParameterId::TukeyFraction: audio.tukeyFraction = action.value; break;
		case ReconstructedAudioParameterId::SecondOperandDelayMicroseconds: audio.secondOperandDelayMicroseconds = action.value; break;
		case ReconstructedAudioParameterId::LowPassEnabled: audio.lowPassEnabled = action.selection != 0; break;
		case ReconstructedAudioParameterId::LowPassCutoffHz: audio.lowPassCutoffHz = action.value; break;
		}
		// The former controller path validated with a fresh synthesizer, whose
		// previous values were the factory defaults. Preserve that fallback rule.
		return SoundEngine::normalizeReconstructedParameters(audio, SoundEngine::ReconstructedParameters());
	}
}
