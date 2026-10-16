#include "LegacySoundEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
	const double Pi = 3.1415926535897932384626433832795;
	const double TwoPi = Pi * 2.0;
	const float LowBandAmplitude = 0.2511886432f; // -12 dB relative to the 300 Hz fundamental.
	const std::size_t ChorusBufferSize = 16384;
	const float ComparisonEnvelopeCutoff = 1.0e-20f;

	float clampValue(float value, float minimum, float maximum)
	{
		return std::max(minimum, std::min(maximum, value));
	}

	float smoothingAlpha(float milliseconds, float sampleRate)
	{
		if (milliseconds <= 0.0f || sampleRate <= 0.0f)
			return 1.0f;
		const double seconds = static_cast<double>(milliseconds) * 0.001;
		return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
	}

	float approach(float current, float target, float attackAlpha, float releaseAlpha)
	{
		const float alpha = target > current ? attackAlpha : releaseAlpha;
		return current + (target - current) * alpha;
	}

	float approachComparisonEnvelope(float current, float target, float attackAlpha, float releaseAlpha)
	{
		// Only comparison envelopes can decay to zero during a normal run. Drop their
		// inaudible tails before they enter the CPU's slow subnormal range, without
		// adding denormal checks to the other per-sample controls and band gains.
		if (target == 0.0f && current < ComparisonEnvelopeCutoff)
			return 0.0f;
		return approach(current, target, attackAlpha, releaseAlpha);
	}

	double combAngle(double frequencyHz, const LegacySoundEngine::BedCombFilterParameters& parameters)
	{
		return TwoPi * frequencyHz * static_cast<double>(parameters.delayMicroseconds) * 1.0e-6 +
			static_cast<double>(parameters.phaseOffsetDegrees) * Pi / 180.0;
	}

	double combWet(double frequencyHz, const LegacySoundEngine::BedCombFilterParameters& parameters)
	{
		const double start = parameters.fadeStartHz;
		const double end = parameters.fadeEndHz;
		double fade = 0.0;
		if (end <= start)
			fade = frequencyHz >= end ? 1.0 : 0.0;
		else
			fade = std::max(0.0, std::min(1.0, (frequencyHz - start) / (end - start)));
		const double exponent = std::max(0.25, static_cast<double>(parameters.fadeExponent));
		const double shapedFade = std::pow(fade, exponent);
		return std::max(0.0, std::min(1.0,
			static_cast<double>(parameters.wetAtLowFrequency) +
			(static_cast<double>(parameters.wetAtHighFrequency) - parameters.wetAtLowFrequency) * shapedFade));
	}
}

namespace LegacySoundEngine
{
	AudioComparison::AudioComparison()
		: firstPosition(0.5f), secondPosition(0.5f), heldKey(HeldKeyParticipant::None),
		firstRole(ComparisonRole::None), secondRole(ComparisonRole::None)
	{
	}

	AudioComparison::AudioComparison(float first, float second, HeldKeyParticipant held)
		: firstPosition(first), secondPosition(second), heldKey(held),
		firstRole(held == HeldKeyParticipant::First ? ComparisonRole::HeldKey : ComparisonRole::None),
		secondRole(held == HeldKeyParticipant::Second ? ComparisonRole::HeldKey : ComparisonRole::None)
	{
	}

	AudioComparison::AudioComparison(float first, float second, ComparisonRole firstSemanticRole, ComparisonRole secondSemanticRole)
		: firstPosition(first), secondPosition(second), heldKey(HeldKeyParticipant::None),
		firstRole(firstSemanticRole), secondRole(secondSemanticRole)
	{
		if (firstRole == ComparisonRole::HeldKey) heldKey = HeldKeyParticipant::First;
		else if (secondRole == ComparisonRole::HeldKey) heldKey = HeldKeyParticipant::Second;
	}

	BedCombFilterParameters::BedCombFilterParameters()
		: enabled(false), applyPhase(false), delayMicroseconds(222.222f), phaseOffsetDegrees(12.0f),
		  wetAtLowFrequency(0.95f), wetAtHighFrequency(0.90f), fadeStartHz(6600.0f),
		  fadeEndHz(15600.0f), fadeExponent(3.0f)
	{
	}

	Parameters::Parameters()
		: masterLevel(0.5f), bedLevel(0.15f), bedHarmonicExponent(1.5f),
		  includeSub300HzBed(false), bedComb(),
		  comparisonBoostDb(6.0f), heldKeyBoostDb(6.0f),
		  tauBelowBins(0.5f), tauAboveBins(0.5f),
		  chorusMix(0.0f), chorusRateHz(1.0f), chorusDepthMs(2.0f),
		  chorusWeighting(ChorusWeighting::Uniform), attackMs(8.0f),
		  releaseMs(8.0f), mappingMode(MappingMode::LinearHz),
		  comparisonSpreadMode(ComparisonSpreadMode::FixedBins), enabled(true)
	{
		// Defaults match the selected complex-phase ordered-sweep preview.
		bedComb.enabled = true;
		bedComb.applyPhase = true;
		bedComb.delayMicroseconds = 222.222f;
		bedComb.phaseOffsetDegrees = 12.0f;
		bedComb.wetAtLowFrequency = 0.95f;
		bedComb.wetAtHighFrequency = 0.90f;
		bedComb.fadeStartHz = 6600.0f;
		bedComb.fadeEndHz = 15600.0f;
		bedComb.fadeExponent = 3.0f;
		bandTrimDb.fill(0.0f);
		roleBoostDb.fill(0.0f);
		accentRouting.fill(true);
		accentGain[static_cast<std::size_t>(AccentCategory::Probe)] = 0.08f;
		accentGain[static_cast<std::size_t>(AccentCategory::Movement)] = 0.10f;
		accentGain[static_cast<std::size_t>(AccentCategory::Distribution)] = 0.07f;
		accentGain[static_cast<std::size_t>(AccentCategory::Discard)] = 0.06f;
	}

	AudioCommand::AudioCommand()
		: kind(AudioCommandKind::ClearComparison), comparison(), traceLoaded(false)
	{
	}

	const std::array<FrequencyBand, BandCount>& frequencyBank()
	{
		static const std::array<FrequencyBand, BandCount> bands = []()
		{
			std::array<FrequencyBand, BandCount> result;
			result[0] = FrequencyBand{ 50.0, LowBandAmplitude };
			result[1] = FrequencyBand{ 100.0, LowBandAmplitude };
			result[2] = FrequencyBand{ 150.0, LowBandAmplitude };
			for (std::size_t harmonic = 1; harmonic <= BandCount - 3; ++harmonic)
				result[harmonic + 2] = FrequencyBand{ 300.0 * harmonic, 1.0f / static_cast<float>(harmonic) };
			return result;
		}();
		return bands;
	}

	BandBlend mapNormalizedPosition(float position, MappingMode mode)
	{
		const std::array<FrequencyBand, BandCount>& bands = frequencyBank();
		const double p = std::isfinite(position) ? clampValue(position, 0.0f, 1.0f) : 0.5;

		if (mode == MappingMode::EqualBandSlots)
		{
			const double slot = p * static_cast<double>(FocusBandCount - 1);
			const std::size_t lower = static_cast<std::size_t>(std::floor(slot));
			const double fraction = slot - lower;
			if (fraction == 0.0)
				return BandBlend{ lower, lower, 0.0f, bands[lower].frequencyHz };
			const std::size_t upper = std::min(lower + 1, FocusBandCount - 1);
			return BandBlend{ lower, upper, static_cast<float>(fraction),
				bands[lower].frequencyHz + (bands[upper].frequencyHz - bands[lower].frequencyHz) * fraction };
		}

		const double minimum = bands.front().frequencyHz;
		const double maximum = bands[FocusBandCount - 1].frequencyHz;
		double target = minimum + (maximum - minimum) * p;
		if (mode == MappingMode::LogFrequency)
			target = minimum * std::pow(maximum / minimum, p);

		if (target <= minimum)
			return BandBlend{ 0, 0, 0.0f, minimum };
		if (target >= maximum)
			return BandBlend{ FocusBandCount - 1, FocusBandCount - 1, 0.0f, maximum };

		std::size_t upper = 1;
		while (upper < FocusBandCount && bands[upper].frequencyHz < target)
			++upper;
		if (bands[upper].frequencyHz == target)
			return BandBlend{ upper, upper, 0.0f, target };
		const std::size_t lower = upper - 1;
		const double lowHz = bands[lower].frequencyHz;
		const double highHz = bands[upper].frequencyHz;
		double weight = (target - lowHz) / (highHz - lowHz);
		if (mode == MappingMode::LogFrequency)
			weight = (std::log(target) - std::log(lowHz)) / (std::log(highHz) - std::log(lowHz));
		return BandBlend{ lower, upper, static_cast<float>(weight), target };
	}

	float normalizeValue(float value, float minimum, float maximum)
	{
		if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum))
			return 0.5f;
		const double range = static_cast<double>(maximum) - static_cast<double>(minimum);
		if (range == 0.0)
			return 0.5f;
		const double normalized = (static_cast<double>(value) - static_cast<double>(minimum)) / range;
		return clampValue(static_cast<float>(normalized), 0.0f, 1.0f);
	}

	float decibelsToGain(float decibels)
	{
		if (!std::isfinite(decibels))
			return 1.0f;
		return static_cast<float>(std::pow(10.0, static_cast<double>(decibels) / 20.0));
	}

	float bedCombMagnitude(double frequencyHz, const BedCombFilterParameters& parameters)
	{
		if (!parameters.enabled || frequencyHz < 300.0 || !std::isfinite(frequencyHz))
			return 1.0f;
		const double wet = combWet(frequencyHz, parameters);
		const double angle = combAngle(frequencyHz, parameters);
		const double direct = 1.0 - wet * 0.5;
		const double delayed = wet * 0.5;
		const double squaredMagnitude = direct * direct + delayed * delayed +
			2.0 * direct * delayed * std::cos(angle);
		return static_cast<float>(std::sqrt(std::max(0.0, squaredMagnitude)));
	}

	float bedCombPhase(double frequencyHz, const BedCombFilterParameters& parameters)
	{
		if (!parameters.enabled || !parameters.applyPhase || frequencyHz < 300.0 || !std::isfinite(frequencyHz))
			return 0.0f;
		const double wet = combWet(frequencyHz, parameters);
		const double angle = combAngle(frequencyHz, parameters);
		const double real = 1.0 - wet * 0.5 + wet * 0.5 * std::cos(angle);
		const double imaginary = -wet * 0.5 * std::sin(angle);
		return static_cast<float>(std::atan2(imaginary, real));
	}

	Synthesizer::Synthesizer()
		: targetParameters_(), activeComparison_(), hasComparison_(false), traceLoaded_(false),
		  comparisonUpdateCount_(0), phases_(), firstWeightTargets_(), secondWeightTargets_(),
		  firstWeights_(), secondWeights_(), bandTrimGains_(), targetBandTrimGains_(), eventAccentSummary_(),
		  eventAccentTargets_(), eventAccentEnvelopes_(), eventAccentAttackRemainingSeconds_(), eventAccentPhases_(),
		  bedCombGains_(), targetBedCombGains_(), bedCombPhases_(), targetBedCombPhases_(),
		  bedHarmonicGains_(), targetBedHarmonicGains_(), bedEnvelope_(0.0f),
		  masterLevel_(targetParameters_.masterLevel), bedLevel_(targetParameters_.bedLevel),
		  comparisonBoost_(decibelsToGain(targetParameters_.comparisonBoostDb)),
		  heldKeyBoost_(decibelsToGain(targetParameters_.heldKeyBoostDb)),
		  targetComparisonBoost_(decibelsToGain(targetParameters_.comparisonBoostDb)),
		  targetHeldKeyBoost_(decibelsToGain(targetParameters_.heldKeyBoostDb)), roleBoosts_(), targetRoleBoosts_(), chorusMix_(targetParameters_.chorusMix),
		  chorusRateHz_(targetParameters_.chorusRateHz), chorusDepthMs_(targetParameters_.chorusDepthMs),
		  chorusPhase_(0.0), chorusDelay_(), chorusEnvelopeDelay_(), chorusWriteIndex_(0)
	{
		phases_.fill(0.0);
		firstWeightTargets_.fill(0.0f);
		secondWeightTargets_.fill(0.0f);
		firstWeights_.fill(0.0f);
		secondWeights_.fill(0.0f);
		eventAccentTargets_.fill(0.0f);
		eventAccentEnvelopes_.fill(0.0f);
		eventAccentAttackRemainingSeconds_.fill(0.0f);
		eventAccentPhases_.fill(0.0);
		roleBoosts_.fill(1.0f);
		targetRoleBoosts_.fill(1.0f);
		bandTrimGains_.fill(1.0f);
		targetBandTrimGains_.fill(1.0f);
		bedCombGains_.fill(1.0f);
		targetBedCombGains_.fill(1.0f);
		bedCombPhases_.fill(0.0);
		targetBedCombPhases_.fill(0.0);
		bedHarmonicGains_.fill(1.0f);
		targetBedHarmonicGains_.fill(1.0f);
		chorusDelay_.fill(0.0f);
		chorusEnvelopeDelay_.fill(0.0f);
	}

	void Synthesizer::applyCommand(const AudioCommand& command)
	{
		switch (command.kind)
		{
		case AudioCommandKind::TraceLoaded:
			setTraceLoaded(command.traceLoaded);
			break;
		case AudioCommandKind::Comparison:
			setComparison(command.comparison);
			break;
		case AudioCommandKind::ClearComparison:
			clearComparison();
			break;
		case AudioCommandKind::Enable:
		{
			Parameters updated = targetParameters_;
			updated.enabled = true;
			setParameters(updated);
			break;
		}
		case AudioCommandKind::Disable:
		{
			Parameters updated = targetParameters_;
			updated.enabled = false;
			setParameters(updated);
			break;
		}
		}
	}

	void Synthesizer::setTraceLoaded(bool loaded)
	{
		traceLoaded_ = loaded;
		if (!loaded)
		{
			clearComparison();
			setEventAccentSummary(EventAccentSummary());
		}
		else
			updateComparisonTargets();
	}

	void Synthesizer::setComparison(const AudioComparison& comparison)
	{
		activeComparison_ = comparison;
		hasComparison_ = true;
		++comparisonUpdateCount_;
		updateComparisonTargets();
	}

	void Synthesizer::clearComparison()
	{
		hasComparison_ = false;
		firstWeightTargets_.fill(0.0f);
		secondWeightTargets_.fill(0.0f);
	}

	void Synthesizer::setEventAccentSummary(const EventAccentSummary& summary)
	{
		for (std::size_t i = 0; i < AccentCategoryCount; ++i)
		{
			eventAccentSummary_.counts[i] = summary.counts[i];
			if (summary.counts[i] != 0)
				eventAccentSummary_.representativePositions[i] = sanitize(summary.representativePositions[i], 0.5f, 0.0f, 1.0f);
		}
		updateEventAccentTargets();
	}

	void Synthesizer::updateEventAccentTargets()
	{
		for (std::size_t i = 0; i < AccentCategoryCount; ++i)
		{
			if (eventAccentSummary_.counts[i] == 0)
				continue;
			if (!targetParameters_.accentRouting[i])
			{
				eventAccentTargets_[i] = 0.0f;
				eventAccentSummary_.counts[i] = 0;
				continue;
			}
			const float density = 1.0f - std::exp(-static_cast<float>(eventAccentSummary_.counts[i]) / 10.0f);
			const float intensity = 0.25f + 0.75f * density;
			// Counts crossed in one frame trigger one bounded accent. The target is
			// normalized; the live category gain is applied during rendering.
			eventAccentTargets_[i] = (std::max)(eventAccentTargets_[i], intensity);
			eventAccentAttackRemainingSeconds_[i] = 0.003f;
			eventAccentSummary_.counts[i] = 0;
		}
	}

	void Synthesizer::setParameters(const Parameters& parameters)
	{
		Parameters sanitized;
		sanitized.masterLevel = sanitize(parameters.masterLevel, 0.5f, 0.0f, 1.0f);
		sanitized.bedLevel = sanitize(parameters.bedLevel, 0.15f, 0.0f, 1.0f);
		sanitized.bedHarmonicExponent = sanitize(parameters.bedHarmonicExponent, 1.5f, 0.5f, 3.0f);
		sanitized.includeSub300HzBed = parameters.includeSub300HzBed;
		sanitized.bedComb.enabled = parameters.bedComb.enabled;
		sanitized.bedComb.applyPhase = parameters.bedComb.applyPhase;
		sanitized.bedComb.delayMicroseconds = sanitize(parameters.bedComb.delayMicroseconds, 222.222f, 0.0f, 5000.0f);
		sanitized.bedComb.phaseOffsetDegrees = sanitize(parameters.bedComb.phaseOffsetDegrees, 12.0f, -180.0f, 180.0f);
		sanitized.bedComb.wetAtLowFrequency = sanitize(parameters.bedComb.wetAtLowFrequency, 0.95f, 0.0f, 1.0f);
		sanitized.bedComb.wetAtHighFrequency = sanitize(parameters.bedComb.wetAtHighFrequency, 0.90f, 0.0f, 1.0f);
		sanitized.bedComb.fadeStartHz = sanitize(parameters.bedComb.fadeStartHz, 6600.0f, 0.0f, 17100.0f);
		sanitized.bedComb.fadeEndHz = sanitize(parameters.bedComb.fadeEndHz, 15600.0f, 0.0f, 17100.0f);
		sanitized.bedComb.fadeExponent = sanitize(parameters.bedComb.fadeExponent, 3.0f, 0.25f, 8.0f);
		sanitized.comparisonBoostDb = sanitize(parameters.comparisonBoostDb, 6.0f, -60.0f, 30.0f);
		sanitized.heldKeyBoostDb = sanitize(parameters.heldKeyBoostDb, 6.0f, -60.0f, 30.0f);
		for (std::size_t i = 0; i < RoleCount; ++i)
			sanitized.roleBoostDb[i] = sanitize(parameters.roleBoostDb[i], 0.0f, -60.0f, 12.0f);
		sanitized.tauBelowBins = sanitize(parameters.tauBelowBins, 0.5f, 0.05f, static_cast<float>(BandCount));
		sanitized.tauAboveBins = sanitize(parameters.tauAboveBins, 0.5f, 0.05f, static_cast<float>(BandCount));
		sanitized.chorusMix = sanitize(parameters.chorusMix, 0.0f, 0.0f, 0.5f);
		sanitized.chorusRateHz = sanitize(parameters.chorusRateHz, 1.0f, 0.1f, 5.0f);
		sanitized.chorusDepthMs = sanitize(parameters.chorusDepthMs, 2.0f, 0.0f, 8.0f);
		sanitized.chorusWeighting = sanitizeChorusWeighting(parameters.chorusWeighting);
		sanitized.attackMs = sanitize(parameters.attackMs, 8.0f, 1.0f, 200.0f);
		sanitized.releaseMs = sanitize(parameters.releaseMs, 8.0f, 1.0f, 2000.0f);
		sanitized.mappingMode = sanitizeMappingMode(parameters.mappingMode);
		sanitized.comparisonSpreadMode = sanitizeComparisonSpreadMode(parameters.comparisonSpreadMode);
		sanitized.enabled = parameters.enabled;
		for (std::size_t i = 0; i < AccentCategoryCount; ++i)
		{
			sanitized.accentRouting[i] = parameters.accentRouting[i];
			sanitized.accentGain[i] = sanitize(parameters.accentGain[i], 0.0f, 0.0f, 0.5f);
		}
		for (std::size_t i = 0; i < BandCount; ++i)
			sanitized.bandTrimDb[i] = sanitize(parameters.bandTrimDb[i], 0.0f, -96.0f, 12.0f);

		targetParameters_ = sanitized;
		targetComparisonBoost_ = decibelsToGain(targetParameters_.comparisonBoostDb);
		targetHeldKeyBoost_ = decibelsToGain(targetParameters_.heldKeyBoostDb);
		for (std::size_t i = 0; i < RoleCount; ++i)
			targetRoleBoosts_[i] = decibelsToGain(targetParameters_.roleBoostDb[i]);
		for (std::size_t i = 0; i < BandCount; ++i)
		{
			targetBandTrimGains_[i] = decibelsToGain(targetParameters_.bandTrimDb[i]);
			targetBedCombGains_[i] = bedCombMagnitude(frequencyBank()[i].frequencyHz, targetParameters_.bedComb);
			targetBedCombPhases_[i] = bedCombPhase(frequencyBank()[i].frequencyHz, targetParameters_.bedComb);
			const double frequencyHz = frequencyBank()[i].frequencyHz;
			if (frequencyHz >= 300.0)
			{
				const double harmonic = frequencyHz / 300.0;
				targetBedHarmonicGains_[i] = static_cast<float>(std::pow(harmonic,
					1.0 - static_cast<double>(targetParameters_.bedHarmonicExponent)));
			}
			else
				targetBedHarmonicGains_[i] = 1.0f;
		}
		updateComparisonTargets();
	}

	const Parameters& Synthesizer::parameters() const
	{
		return targetParameters_;
	}

	bool Synthesizer::traceLoaded() const
	{
		return traceLoaded_;
	}

	std::uint64_t Synthesizer::comparisonUpdateCount() const
	{
		return comparisonUpdateCount_;
	}

	void Synthesizer::retargetParticipant(std::array<float, BandCount>& targets, float position)
	{
		const BandBlend blend = mapNormalizedPosition(position, targetParameters_.mappingMode);
		const float normalizedPosition = std::isfinite(position)
			? clampValue(position, 0.0f, 1.0f) : 0.5f;
		const double positionScale = targetParameters_.comparisonSpreadMode == ComparisonSpreadMode::PositionScaled
			? 0.2 + 0.8 * static_cast<double>(normalizedPosition) : 1.0;
		const double centerIndex = blend.lowerIndex == blend.upperIndex
			? static_cast<double>(blend.lowerIndex)
			: static_cast<double>(blend.lowerIndex) + blend.upperWeight;
		const double nearestIndex = std::floor(centerIndex + 0.5);
		const double nearestDistance = std::fabs(centerIndex - nearestIndex);

		for (std::size_t band = 0; band < BandCount; ++band)
		{
			const double signedDistance = static_cast<double>(band) - centerIndex;
			const float tau = signedDistance < 0.0
				? static_cast<float>(targetParameters_.tauBelowBins * positionScale)
				: static_cast<float>(targetParameters_.tauAboveBins * positionScale);
			const double distance = std::max(0.0, std::fabs(signedDistance) - nearestDistance);
			const float weight = static_cast<float>(std::exp(-distance / tau));
			if (weight >= ComparisonEnvelopeCutoff)
				targets[band] += weight;
		}
	}

	void Synthesizer::updateComparisonTargets()
	{
		firstWeightTargets_.fill(0.0f);
		secondWeightTargets_.fill(0.0f);
		if (!traceLoaded_ || !hasComparison_)
			return;
		retargetParticipant(firstWeightTargets_, activeComparison_.firstPosition);
		retargetParticipant(secondWeightTargets_, activeComparison_.secondPosition);
	}

	float Synthesizer::comparisonRoleGain(ComparisonRole role) const
	{
		if (role == ComparisonRole::None) return 1.0f;
		if (role == ComparisonRole::HeldKey) return heldKeyBoost_;
		const std::size_t index = static_cast<std::size_t>(role);
		return index < RoleCount ? roleBoosts_[index] : 1.0f;
	}

	float Synthesizer::readDelay(const std::array<float, 16384>& delayLine, double delaySamples) const
	{
		delaySamples = std::max(1.0, std::min(delaySamples, static_cast<double>(ChorusBufferSize - 2)));
		double readPosition = static_cast<double>(chorusWriteIndex_) - delaySamples;
		while (readPosition < 0.0)
			readPosition += static_cast<double>(ChorusBufferSize);
		while (readPosition >= static_cast<double>(ChorusBufferSize))
			readPosition -= static_cast<double>(ChorusBufferSize);
		const std::size_t index0 = static_cast<std::size_t>(readPosition);
		const std::size_t index1 = (index0 + 1) % ChorusBufferSize;
		const float fraction = static_cast<float>(readPosition - index0);
		return delayLine[index0] + (delayLine[index1] - delayLine[index0]) * fraction;
	}

	float Synthesizer::sanitize(float value, float fallback, float minimum, float maximum)
	{
		return std::isfinite(value) ? clampValue(value, minimum, maximum) : fallback;
	}

	MappingMode Synthesizer::sanitizeMappingMode(MappingMode mode)
	{
		switch (mode)
		{
		case MappingMode::LinearHz:
		case MappingMode::LogFrequency:
		case MappingMode::EqualBandSlots:
			return mode;
		default:
			return MappingMode::LinearHz;
		}
	}

	ComparisonSpreadMode Synthesizer::sanitizeComparisonSpreadMode(ComparisonSpreadMode mode)
	{
		switch (mode)
		{
		case ComparisonSpreadMode::FixedBins:
		case ComparisonSpreadMode::PositionScaled:
			return mode;
		default:
			return ComparisonSpreadMode::FixedBins;
		}
	}

	ChorusWeighting Synthesizer::sanitizeChorusWeighting(ChorusWeighting weighting)
	{
		switch (weighting)
		{
		case ChorusWeighting::Uniform:
		case ChorusWeighting::ComparisonEnvelope:
			return weighting;
		default:
			return ChorusWeighting::Uniform;
		}
	}

	void Synthesizer::render(float* output, std::size_t frameCount, std::uint32_t sampleRate)
	{
		if (output == nullptr)
			return;
		if (sampleRate < 8000 || sampleRate > 192000)
			sampleRate = 44100;

		const float rate = static_cast<float>(sampleRate);
		const float envelopeAttackAlpha = smoothingAlpha(targetParameters_.attackMs, rate);
		const float envelopeReleaseAlpha = smoothingAlpha(targetParameters_.releaseMs, rate);
		const float controlAttackAlpha = smoothingAlpha(10.0f, rate);
		const float controlReleaseAlpha = smoothingAlpha(80.0f, rate);
		const float accentAttackAlpha = smoothingAlpha(3.0f, rate);
		const float accentReleaseAlpha = smoothingAlpha(95.0f, rate);
		const std::array<FrequencyBand, BandCount>& bands = frequencyBank();
		const bool envelopeWeightedChorus = targetParameters_.chorusWeighting == ChorusWeighting::ComparisonEnvelope;

		for (std::size_t frame = 0; frame < frameCount; ++frame)
		{
			masterLevel_ = approach(masterLevel_, targetParameters_.masterLevel, controlAttackAlpha, controlReleaseAlpha);
			bedLevel_ = approach(bedLevel_, targetParameters_.bedLevel, controlAttackAlpha, controlReleaseAlpha);
			comparisonBoost_ = approach(comparisonBoost_, targetComparisonBoost_, controlAttackAlpha, controlReleaseAlpha);
			heldKeyBoost_ = approach(heldKeyBoost_, targetHeldKeyBoost_, controlAttackAlpha, controlReleaseAlpha);
			for (std::size_t role = 0; role < RoleCount; ++role)
				roleBoosts_[role] = approach(roleBoosts_[role], targetRoleBoosts_[role], controlAttackAlpha, controlReleaseAlpha);
			chorusMix_ = approach(chorusMix_, targetParameters_.chorusMix, controlAttackAlpha, controlReleaseAlpha);
			chorusRateHz_ = approach(chorusRateHz_, targetParameters_.chorusRateHz, controlAttackAlpha, controlReleaseAlpha);
			chorusDepthMs_ = approach(chorusDepthMs_, targetParameters_.chorusDepthMs, controlAttackAlpha, controlReleaseAlpha);
			bedEnvelope_ = approach(bedEnvelope_, traceLoaded_ ? 1.0f : 0.0f, envelopeAttackAlpha, envelopeReleaseAlpha);
			for (std::size_t i = 0; i < AccentCategoryCount; ++i)
			{
				if (eventAccentTargets_[i] > 0.0f && targetParameters_.accentRouting[i])
				{
					eventAccentEnvelopes_[i] = approach(eventAccentEnvelopes_[i], eventAccentTargets_[i], accentAttackAlpha, accentAttackAlpha);
					eventAccentAttackRemainingSeconds_[i] -= 1.0f / rate;
					if (eventAccentAttackRemainingSeconds_[i] <= 0.0f)
					{
						eventAccentAttackRemainingSeconds_[i] = 0.0f;
						eventAccentTargets_[i] = 0.0f;
					}
				}
				else
				{
					eventAccentTargets_[i] = 0.0f;
					eventAccentAttackRemainingSeconds_[i] = 0.0f;
					eventAccentEnvelopes_[i] = approach(eventAccentEnvelopes_[i], 0.0f, accentReleaseAlpha, accentReleaseAlpha);
				}
			}
			const float firstWeight = comparisonRoleGain(activeComparison_.firstRole);
			const float heldWeight = comparisonRoleGain(activeComparison_.secondRole);
			float maximumComparisonEnvelope = 0.0f;
			for (std::size_t band = 0; band < BandCount; ++band)
			{
				firstWeights_[band] = approachComparisonEnvelope(firstWeights_[band], firstWeightTargets_[band], envelopeAttackAlpha, envelopeReleaseAlpha);
				secondWeights_[band] = approachComparisonEnvelope(secondWeights_[band], secondWeightTargets_[band], envelopeAttackAlpha, envelopeReleaseAlpha);
				bandTrimGains_[band] = approach(bandTrimGains_[band], targetBandTrimGains_[band], controlAttackAlpha, controlReleaseAlpha);
				bedCombGains_[band] = approach(bedCombGains_[band], targetBedCombGains_[band], controlAttackAlpha, controlReleaseAlpha);
				bedHarmonicGains_[band] = approach(bedHarmonicGains_[band], targetBedHarmonicGains_[band], controlAttackAlpha, controlReleaseAlpha);
				const double phaseDelta = std::remainder(targetBedCombPhases_[band] - bedCombPhases_[band], TwoPi);
				bedCombPhases_[band] += phaseDelta * controlAttackAlpha;
				if (envelopeWeightedChorus)
				{
					const float comparisonEnvelope = firstWeights_[band] * firstWeight + secondWeights_[band] * heldWeight;
					maximumComparisonEnvelope = std::max(maximumComparisonEnvelope, comparisonEnvelope);
				}
			}

			const bool enabled = targetParameters_.enabled;
			if (!enabled)
			{
				output[frame * 2] = 0.0f;
				output[frame * 2 + 1] = 0.0f;
				chorusDelay_[chorusWriteIndex_] = 0.0f;
				chorusEnvelopeDelay_[chorusWriteIndex_] = 0.0f;
				chorusWriteIndex_ = (chorusWriteIndex_ + 1) % ChorusBufferSize;
				for (std::size_t band = 0; band < BandCount; ++band)
				{
					phases_[band] += TwoPi * bands[band].frequencyHz / sampleRate;
					if (phases_[band] >= TwoPi)
						phases_[band] -= TwoPi;
				}
				chorusPhase_ += TwoPi * chorusRateHz_ / sampleRate;
				if (chorusPhase_ >= TwoPi)
					chorusPhase_ -= TwoPi;
				continue;
			}

			double mixed = 0.0;
			double envelopeWeightedMixed = 0.0;
			double amplitudeBound = 0.0;
			for (std::size_t band = 0; band < BandCount; ++band)
			{
				const float comparisonEnvelope = firstWeights_[band] * firstWeight + secondWeights_[band] * heldWeight;
				const float comparison = comparisonBoost_ * comparisonEnvelope;
				const float bedForBand = targetParameters_.includeSub300HzBed || bands[band].frequencyHz >= 300.0
					? bedLevel_ * bedEnvelope_
					: 0.0f;
				const float filteredBed = bedForBand * bedCombGains_[band];
				const double bandScale = static_cast<double>(bands[band].baseAmplitude * bandTrimGains_[band]);
				const double bedAmplitude = static_cast<double>(filteredBed * bedHarmonicGains_[band]);
				const double comparisonAmplitude = static_cast<double>(comparison);
				const double bedSample = bandScale * bedAmplitude * std::sin(phases_[band] + bedCombPhases_[band]);
				const double comparisonSample = bandScale * comparisonAmplitude * std::sin(phases_[band]);
				const double bandSample = bedSample + comparisonSample;
				mixed += bandSample;
				if (envelopeWeightedChorus && maximumComparisonEnvelope > 1.0e-6f)
					envelopeWeightedMixed += bandSample * (comparisonEnvelope / maximumComparisonEnvelope);
				amplitudeBound += std::fabs(bandScale) * (std::fabs(bedAmplitude) + std::fabs(comparisonAmplitude));
				phases_[band] += TwoPi * bands[band].frequencyHz / sampleRate;
				if (phases_[band] >= TwoPi)
					phases_[band] -= TwoPi;
			}
			static const int AccentSemitones[AccentCategoryCount] = { 0, 3, 7, 12 };
			for (std::size_t accent = 0; accent < AccentCategoryCount; ++accent)
			{
				const BandBlend pitch = mapNormalizedPosition(
					eventAccentSummary_.representativePositions[accent], targetParameters_.mappingMode);
				const double ratio = std::pow(2.0, static_cast<double>(AccentSemitones[accent]) / 12.0);
				const double frequency = std::min(static_cast<double>(sampleRate) * 0.45, pitch.frequencyHz * ratio);
				const double categoryGain = targetParameters_.accentRouting[accent] ? targetParameters_.accentGain[accent] : 0.0;
				const double eventAmplitude = static_cast<double>(eventAccentEnvelopes_[accent]) * categoryGain;
				const double eventSample = eventAmplitude * std::sin(eventAccentPhases_[accent]);
				mixed += eventSample;
				amplitudeBound += std::fabs(eventAmplitude);
				eventAccentPhases_[accent] = std::remainder(eventAccentPhases_[accent] + TwoPi * frequency / sampleRate, TwoPi);
			}

			// Scale the linear sum only when the sum of all oscillator peaks could
			// exceed the headroom ceiling. This avoids clipping distortion and keeps
			// stable output confined to the generated band centers.
			const double headroomScale = amplitudeBound > 0.95 ? 0.95 / amplitudeBound : 1.0;
			const float dry = static_cast<float>(mixed * headroomScale);
			chorusDelay_[chorusWriteIndex_] = dry;
			chorusEnvelopeDelay_[chorusWriteIndex_] = static_cast<float>(envelopeWeightedMixed * headroomScale);
			const double leftPhase = chorusPhase_;
			const double rightPhase = chorusPhase_ + Pi * 0.5;
			const double centerDelay = static_cast<double>(sampleRate) * 0.018;
			const double depth = static_cast<double>(chorusDepthMs_) * sampleRate * 0.001;
			const std::array<float, 16384>& chorusInput = envelopeWeightedChorus ? chorusEnvelopeDelay_ : chorusDelay_;
			const float delayedLeft = readDelay(chorusInput, centerDelay + depth * std::sin(leftPhase));
			const float delayedRight = readDelay(chorusInput, centerDelay + depth * std::sin(rightPhase));
			const float left = (dry * (1.0f - chorusMix_) + delayedLeft * chorusMix_) * masterLevel_;
			const float right = (dry * (1.0f - chorusMix_) + delayedRight * chorusMix_) * masterLevel_;
			output[frame * 2] = left;
			output[frame * 2 + 1] = right;

			chorusWriteIndex_ = (chorusWriteIndex_ + 1) % ChorusBufferSize;
			chorusPhase_ += TwoPi * chorusRateHz_ / sampleRate;
			if (chorusPhase_ >= TwoPi)
				chorusPhase_ -= TwoPi;
		}
	}

	void Synthesizer::reset()
	{
		traceLoaded_ = false;
		hasComparison_ = false;
		activeComparison_ = AudioComparison();
		comparisonUpdateCount_ = 0;
		eventAccentSummary_ = EventAccentSummary();
		eventAccentTargets_.fill(0.0f);
		eventAccentEnvelopes_.fill(0.0f);
		eventAccentAttackRemainingSeconds_.fill(0.0f);
		eventAccentPhases_.fill(0.0);
		phases_.fill(0.0);
		firstWeightTargets_.fill(0.0f);
		secondWeightTargets_.fill(0.0f);
		firstWeights_.fill(0.0f);
		secondWeights_.fill(0.0f);
		bedEnvelope_ = 0.0f;
		chorusDelay_.fill(0.0f);
		chorusEnvelopeDelay_.fill(0.0f);
		chorusWriteIndex_ = 0;
		chorusPhase_ = 0.0;
	}
}
