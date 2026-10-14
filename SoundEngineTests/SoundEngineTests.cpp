#include "SoundEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	const std::uint32_t SampleRate = 44100;
	const double Pi = 3.1415926535897932384626433832795;
	const double TwoPi = Pi * 2.0;
	int assertions = 0;

	void require(bool condition, const std::string& message)
	{
		++assertions;
		if (!condition)
			throw std::runtime_error(message);
	}

	bool near(double actual, double expected, double tolerance)
	{
		return std::fabs(actual - expected) <= tolerance;
	}

	double sineAmplitude(const std::vector<float>& stereo, std::size_t startFrame, std::size_t frameCount, double frequency)
	{
		double cosine = 0.0;
		double sine = 0.0;
		for (std::size_t i = 0; i < frameCount; ++i)
		{
			const double phase = TwoPi * frequency * static_cast<double>(startFrame + i) / SampleRate;
			const double sample = stereo[(startFrame + i) * 2];
			cosine += sample * std::cos(phase);
			sine += sample * std::sin(phase);
		}
		return 2.0 * std::sqrt(cosine * cosine + sine * sine) / frameCount;
	}

	double normalizedBandAmplitude(const std::vector<float>& samples, std::size_t startFrame, std::size_t frameCount, std::size_t bandIndex)
	{
		const SoundEngine::FrequencyBand& band = SoundEngine::frequencyBank()[bandIndex];
		return sineAmplitude(samples, startFrame, frameCount, band.frequencyHz) / band.baseAmplitude;
	}

	double maxAbs(const std::vector<float>& samples)
	{
		double peak = 0.0;
		for (std::size_t i = 0; i < samples.size(); ++i)
			peak = std::max(peak, std::fabs(static_cast<double>(samples[i])));
		return peak;
	}

	void testCombFilterBedShaping()
	{
		SoundEngine::BedCombFilterParameters defaults;
		require(!defaults.enabled, "experimental bed comb should be opt-in");
		require(near(SoundEngine::bedCombMagnitude(2100.0, defaults), 1.0, 1e-7),
			"a disabled bed comb should have unity response");

		SoundEngine::BedCombFilterParameters comb = defaults;
		comb.enabled = true;
		require(near(SoundEngine::bedCombMagnitude(150.0, comb), 1.0, 1e-7),
			"comb shaping must leave sub-300 Hz comparison components untouched");
		require(SoundEngine::bedCombMagnitude(2100.0, comb) < 0.06f,
			"the fitted 2.1 kHz comb null was not deep enough");
		require(SoundEngine::bedCombMagnitude(6600.0, comb) < 0.06f,
			"the fitted 6.6 kHz comb null was not deep enough");
		require(near(SoundEngine::bedCombMagnitude(11100.0, comb), 0.05625, 0.002),
			"the high-frequency fade did not produce the expected 11.1 kHz comb depth");
		require(near(SoundEngine::bedCombMagnitude(15600.0, comb), 0.10, 0.002),
			"the high-frequency comb tail did not fade to the expected 15.6 kHz depth");
		require(SoundEngine::bedCombMagnitude(2400.0, comb) > SoundEngine::bedCombMagnitude(2100.0, comb),
			"the response did not rise away from the 2.1 kHz null");

		SoundEngine::Synthesizer flat;
		SoundEngine::Synthesizer filtered;
		SoundEngine::Parameters flatParameters = flat.parameters();
		flatParameters.masterLevel = 0.1f;
		flatParameters.bedLevel = 0.15f;
		flatParameters.bedComb.enabled = false;
		flatParameters.bedComb.applyPhase = false;
		flatParameters.comparisonBoostDb = 0.0f;
		flatParameters.heldKeyBoostDb = 0.0f;
		flatParameters.attackMs = 1.0f;
		flatParameters.releaseMs = 1.0f;
		SoundEngine::Parameters filteredParameters = flatParameters;
		filteredParameters.bedComb.enabled = true;
		flat.setParameters(flatParameters);
		filtered.setParameters(filteredParameters);
		flat.setTraceLoaded(true);
		filtered.setTraceLoaded(true);
		std::vector<float> flatSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> filteredSamples(flatSamples.size());
		flat.render(flatSamples.data(), flatSamples.size() / 2, SampleRate);
		filtered.render(filteredSamples.data(), filteredSamples.size() / 2, SampleRate);
		const std::size_t start = SampleRate;
		const std::size_t frames = SampleRate;
		const double flat2100 = sineAmplitude(flatSamples, start, frames, 2100.0);
		const double filtered2100 = sineAmplitude(filteredSamples, start, frames, 2100.0);
		const double flat2400 = sineAmplitude(flatSamples, start, frames, 2400.0);
		const double filtered2400 = sineAmplitude(filteredSamples, start, frames, 2400.0);
		require(flat2100 > 0.0 && near(filtered2100 / flat2100, SoundEngine::bedCombMagnitude(2100.0, comb), 0.003),
			"rendered bed did not match the precomputed 2.1 kHz comb response");
		require(flat2400 > 0.0 && near(filtered2400 / flat2400, SoundEngine::bedCombMagnitude(2400.0, comb), 0.003),
			"rendered bed did not match the precomputed off-null comb response");

		SoundEngine::Synthesizer unfilteredComparison;
		SoundEngine::Synthesizer combComparison;
		flatParameters.bedLevel = 0.0f;
		filteredParameters = flatParameters;
		filteredParameters.bedComb.enabled = true;
		unfilteredComparison.setParameters(flatParameters);
		combComparison.setParameters(filteredParameters);
		unfilteredComparison.setTraceLoaded(true);
		combComparison.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.23f, 0.67f, SoundEngine::HeldKeyParticipant::Second);
		unfilteredComparison.setComparison(comparison);
		combComparison.setComparison(comparison);
		std::vector<float> bedFade(static_cast<std::size_t>(SampleRate) * 2 * 2);
		unfilteredComparison.render(bedFade.data(), bedFade.size() / 2, SampleRate);
		combComparison.render(bedFade.data(), bedFade.size() / 2, SampleRate);
		std::vector<float> comparisonA(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> comparisonB(comparisonA.size());
		unfilteredComparison.render(comparisonA.data(), comparisonA.size() / 2, SampleRate);
		combComparison.render(comparisonB.data(), comparisonB.size() / 2, SampleRate);
		double comparisonDifference = 0.0;
		for (std::size_t i = 0; i < comparisonA.size(); ++i)
			comparisonDifference = std::max(comparisonDifference,
				std::fabs(static_cast<double>(comparisonA[i] - comparisonB[i])));
		require(comparisonDifference < 1e-7,
			"bed comb shaping changed comparison tones when the bed was silent (max delta=" +
			std::to_string(comparisonDifference) + ")");
	}

	void testPhaseAwareBedAndHarmonicRolloff()
	{
		SoundEngine::BedCombFilterParameters comb;
		comb.enabled = true;
		require(near(SoundEngine::bedCombPhase(150.0, comb), 0.0, 1e-8),
			"comb phase must leave sub-300 Hz comparison bands untouched");
		require(near(SoundEngine::bedCombPhase(2100.0, comb), 0.0, 1e-8),
			"magnitude-only comb mode unexpectedly changed partial phase");
		comb.applyPhase = true;
		require(std::fabs(SoundEngine::bedCombPhase(1500.0, comb)) > 0.1f,
			"phase-aware comb mode did not produce a phase rotation");

		SoundEngine::Synthesizer magnitudeOnly;
		SoundEngine::Synthesizer complexResponse;
		SoundEngine::Parameters magnitudeParameters = magnitudeOnly.parameters();
		magnitudeParameters.masterLevel = 0.1f;
		magnitudeParameters.bedLevel = 0.15f;
		magnitudeParameters.bedComb.enabled = true;
		magnitudeParameters.bedComb.applyPhase = false;
		magnitudeParameters.attackMs = 1.0f;
		magnitudeParameters.releaseMs = 1.0f;
		SoundEngine::Parameters phaseParameters = magnitudeParameters;
		phaseParameters.bedComb.applyPhase = true;
		magnitudeOnly.setParameters(magnitudeParameters);
		complexResponse.setParameters(phaseParameters);
		magnitudeOnly.setTraceLoaded(true);
		complexResponse.setTraceLoaded(true);
		std::vector<float> magnitudeSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> phaseSamples(magnitudeSamples.size());
		magnitudeOnly.render(magnitudeSamples.data(), magnitudeSamples.size() / 2, SampleRate);
		complexResponse.render(phaseSamples.data(), phaseSamples.size() / 2, SampleRate);
		const std::size_t start = SampleRate;
		const std::size_t frames = SampleRate;
		const double magnitude2100 = sineAmplitude(magnitudeSamples, start, frames, 2100.0);
		const double phase2100 = sineAmplitude(phaseSamples, start, frames, 2100.0);
		require(near(phase2100 / magnitude2100, 1.0, 0.003),
			"phase-aware comb changed the fitted magnitude response");
		double maxPhaseDifference = 0.0;
		for (std::size_t i = 0; i < magnitudeSamples.size(); ++i)
			maxPhaseDifference = std::max(maxPhaseDifference,
				std::fabs(static_cast<double>(magnitudeSamples[i] - phaseSamples[i])));
		require(maxPhaseDifference > 0.005,
			"phase-aware comb did not alter the bed's time-domain waveform");

		SoundEngine::Synthesizer fundamentalBed;
		SoundEngine::Synthesizer rolledBed;
		SoundEngine::Parameters base = fundamentalBed.parameters();
		base.masterLevel = 0.1f;
		base.bedLevel = 0.15f;
		base.bedHarmonicExponent = 1.0f;
		base.attackMs = 1.0f;
		base.releaseMs = 1.0f;
		SoundEngine::Parameters rolled = base;
		rolled.bedHarmonicExponent = 1.5f;
		fundamentalBed.setParameters(base);
		rolledBed.setParameters(rolled);
		fundamentalBed.setTraceLoaded(true);
		rolledBed.setTraceLoaded(true);
		std::vector<float> baseSamples(static_cast<std::size_t>(SampleRate) * 2 * 2);
		std::vector<float> rolledSamples(baseSamples.size());
		fundamentalBed.render(baseSamples.data(), baseSamples.size() / 2, SampleRate);
		rolledBed.render(rolledSamples.data(), rolledSamples.size() / 2, SampleRate);
		const double base300 = sineAmplitude(baseSamples, start, frames, 300.0);
		const double base1200 = sineAmplitude(baseSamples, start, frames, 1200.0);
		const double rolled300 = sineAmplitude(rolledSamples, start, frames, 300.0);
		const double rolled1200 = sineAmplitude(rolledSamples, start, frames, 1200.0);
		require(near(rolled300 / base300, 1.0, 0.003), "harmonic rolloff changed the 300 Hz fundamental");
		require(near((rolled1200 / base1200), 0.5, 0.004),
			"p=1.5 bed rolloff did not attenuate the fourth harmonic by 1/sqrt(4)");
	}

	void testPositionScaledComparisonSpread()
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters parameters = synthesizer.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		parameters.comparisonSpreadMode = SoundEngine::ComparisonSpreadMode::PositionScaled;
		parameters.attackMs = 1.0f;
		synthesizer.setParameters(parameters);
		synthesizer.setTraceLoaded(true);

		const SoundEngine::AudioComparison low(0.0f, 0.0f, SoundEngine::HeldKeyParticipant::None);
		synthesizer.setComparison(low);
		std::vector<float> lowSamples(static_cast<std::size_t>(SampleRate) * 2);
		synthesizer.render(lowSamples.data(), SampleRate, SampleRate);
		const double lowCenter = normalizedBandAmplitude(lowSamples, SampleRate / 2, SampleRate / 2, 0);
		const double lowNeighbor = normalizedBandAmplitude(lowSamples, SampleRate / 2, SampleRate / 2, 1);
		require(near(lowNeighbor / lowCenter, std::exp(-10.0), 2e-5),
			"low-value position-scaled spread did not use tau=0.1 bins");

		const SoundEngine::AudioComparison high(1.0f, 1.0f, SoundEngine::HeldKeyParticipant::None);
		synthesizer.setComparison(high);
		std::vector<float> highSamples(static_cast<std::size_t>(SampleRate) * 2);
		synthesizer.render(highSamples.data(), SampleRate, SampleRate);
		const double highCenter = normalizedBandAmplitude(highSamples, SampleRate / 2, SampleRate / 2, 16);
		const double highNeighbor = normalizedBandAmplitude(highSamples, SampleRate / 2, SampleRate / 2, 17);
		require(near(highNeighbor / highCenter, std::exp(-2.0), 0.01),
			"high-value position-scaled spread did not widen to tau=0.5 bins");
	}

	void testMasterGainRemainsLinearUnderHeadroomProtection()
	{
		SoundEngine::Synthesizer full;
		SoundEngine::Synthesizer reduced;
		SoundEngine::Parameters fullParameters = full.parameters();
		fullParameters.masterLevel = 0.5f;
		fullParameters.bedLevel = 1.0f;
		fullParameters.comparisonBoostDb = 18.0f;
		fullParameters.heldKeyBoostDb = 18.0f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			fullParameters.bandTrimDb[i] = 12.0f;
		SoundEngine::Parameters reducedParameters = fullParameters;
		reducedParameters.masterLevel = 0.375f;
		full.setParameters(fullParameters);
		reduced.setParameters(reducedParameters);
		full.setTraceLoaded(true);
		reduced.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.37f, 0.84f, SoundEngine::HeldKeyParticipant::Second);
		full.setComparison(comparison);
		reduced.setComparison(comparison);
		std::vector<float> warm(static_cast<std::size_t>(SampleRate) * 2 * 2);
		full.render(warm.data(), warm.size() / 2, SampleRate);
		reduced.render(warm.data(), warm.size() / 2, SampleRate);
		std::vector<float> fullOutput(static_cast<std::size_t>(SampleRate) * 2 / 2);
		std::vector<float> reducedOutput(fullOutput.size());
		full.render(fullOutput.data(), fullOutput.size() / 2, SampleRate);
		reduced.render(reducedOutput.data(), reducedOutput.size() / 2, SampleRate);
		double fullPower = 0.0;
		double reducedPower = 0.0;
		for (std::size_t i = 0; i < fullOutput.size(); ++i)
		{
			fullPower += static_cast<double>(fullOutput[i]) * fullOutput[i];
			reducedPower += static_cast<double>(reducedOutput[i]) * reducedOutput[i];
		}
		const double measuredGainRatio = std::sqrt(reducedPower / fullPower);
		require(near(measuredGainRatio, 0.75, 1e-3),
			"master gain was compensated away by the headroom limiter");
	}

	void testFrequencyBankAndMapping()
	{
		const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount>& bank = SoundEngine::frequencyBank();
		require(SoundEngine::BandCount == 60 && SoundEngine::FocusBandCount == 17, "band or focus-band count changed unexpectedly");
		require(bank[0].frequencyHz == 50.0 && bank[1].frequencyHz == 100.0 && bank[2].frequencyHz == 150.0,
			"low-frequency bank centers are incorrect");
		require(bank[0].frequencyHz != 75.0, "the old 75 Hz bin remains in the bank");
		for (std::size_t harmonic = 1; harmonic <= 57; ++harmonic)
		{
			const std::size_t index = harmonic + 2;
			require(bank[index].frequencyHz == 300.0 * harmonic, "harmonic bank has an unexpected center");
			require(near(bank[index].baseAmplitude, 1.0 / harmonic, 1e-6), "harmonic base amplitude is not 1/k");
		}
		require(bank[SoundEngine::BandCount - 1].frequencyHz == 17100.0, "upper super-harmonic endpoint is incorrect");
		require(near(bank[0].baseAmplitude, std::pow(10.0, -12.0 / 20.0), 1e-6), "low-band profile is not -12 dB");
		require(near(bank[3].baseAmplitude, 1.0, 1e-6), "300 Hz fundamental amplitude should be unity");
		require(near(bank[4].baseAmplitude, 0.5, 1e-6), "second harmonic amplitude should be one half");

		const SoundEngine::MappingMode modes[] = {
			SoundEngine::MappingMode::LinearHz,
			SoundEngine::MappingMode::LogFrequency,
			SoundEngine::MappingMode::EqualBandSlots
		};
		for (std::size_t i = 0; i < 3; ++i)
		{
			const SoundEngine::BandBlend first = SoundEngine::mapNormalizedPosition(0.0f, modes[i]);
			const SoundEngine::BandBlend last = SoundEngine::mapNormalizedPosition(1.0f, modes[i]);
			require(first.lowerIndex == 0 && first.upperIndex == 0 && first.frequencyHz == 50.0, "mapping start endpoint is incorrect");
			require(last.lowerIndex == SoundEngine::FocusBandCount - 1 && last.upperIndex == last.lowerIndex && last.frequencyHz == 4200.0,
				"mapping end endpoint is incorrect");
			for (int step = 0; step <= 100; ++step)
			{
				const SoundEngine::BandBlend mapped = SoundEngine::mapNormalizedPosition(static_cast<float>(step) / 100.0f, modes[i]);
				require(mapped.frequencyHz >= 50.0 && mapped.frequencyHz <= 4200.0, "mapping focus escaped the 50 Hz–4.2 kHz range");
				require(mapped.lowerIndex < SoundEngine::FocusBandCount && mapped.upperIndex < SoundEngine::FocusBandCount,
					"mapping selected a super-harmonic as a focus center");
			}
		}

		const SoundEngine::BandBlend linearMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::LinearHz);
		require(near(linearMid.frequencyHz, 2125.0, 1e-8), "linear-Hz midpoint is incorrect");
		require(linearMid.lowerIndex == 9 && linearMid.upperIndex == 10 && near(linearMid.upperWeight, 1.0 / 12.0, 1e-6), "linear-Hz midpoint blend is incorrect");
		const SoundEngine::BandBlend logMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::LogFrequency);
		require(near(logMid.frequencyHz, std::sqrt(50.0 * 4200.0), 1e-8) && logMid.lowerIndex == 3 && logMid.upperIndex == 4, "log-frequency midpoint is incorrect");
		const SoundEngine::BandBlend slotMid = SoundEngine::mapNormalizedPosition(0.5f, SoundEngine::MappingMode::EqualBandSlots);
		require(near(slotMid.frequencyHz, 1800.0, 1e-8) && slotMid.lowerIndex == 8 && slotMid.upperIndex == 8 && near(slotMid.upperWeight, 0.0, 1e-8), "equal-slot midpoint is incorrect");

		require(near(SoundEngine::normalizeValue(5.0f, 5.0f, 5.0f), 0.5, 1e-7), "zero-width ranges should map to the midpoint");
		require(near(SoundEngine::normalizeValue(0.0f, 0.0f, 10.0f), 0.0, 1e-7), "minimum value did not normalize to zero");
		require(near(SoundEngine::normalizeValue(10.0f, 0.0f, 10.0f), 1.0, 1e-7), "maximum value did not normalize to one");
		require(near(SoundEngine::normalizeValue(7.5f, 10.0f, 0.0f), 0.25, 1e-7), "reversed value ranges were not normalized consistently");
	}

	std::vector<float> renderComparison(SoundEngine::HeldKeyParticipant held)
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters parameters = synthesizer.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.attackMs = 10.0f;
		parameters.tauBelowBins = 0.25f;
		parameters.tauAboveBins = 0.25f;
		synthesizer.setParameters(parameters);
		synthesizer.setTraceLoaded(true);
		synthesizer.setComparison(SoundEngine::AudioComparison(
			SoundEngine::normalizeValue(300.0f, 50.0f, 4200.0f),
			SoundEngine::normalizeValue(600.0f, 50.0f, 4200.0f), held));
		std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		synthesizer.render(samples.data(), samples.size() / 2, SampleRate);
		return samples;
	}

	void testHeldKeyWeighting()
	{
		const std::vector<float> equal = renderComparison(SoundEngine::HeldKeyParticipant::None);
		const std::vector<float> held = renderComparison(SoundEngine::HeldKeyParticipant::Second);
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 2;
		const std::size_t frames = SampleRate;
		const double equal300 = sineAmplitude(equal, start, frames, 300.0);
		const double equal600 = sineAmplitude(equal, start, frames, 600.0);
		const double held300 = sineAmplitude(held, start, frames, 300.0);
		const double held600 = sineAmplitude(held, start, frames, 600.0);
		require(equal300 > 0.01 && equal600 > 0.001, "comparison did not produce the expected test tones");
		require(near((held600 / held300) / (equal600 / equal300), SoundEngine::decibelsToGain(6.0), 0.06), "held participant was not boosted by approximately +6 dB");
	}

	void testGeneratedCarrierCenters()
	{
		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters bedParameters = synthesizer.parameters();
		bedParameters.bedComb.enabled = false;
		bedParameters.bedComb.applyPhase = false;
		synthesizer.setParameters(bedParameters);
		synthesizer.setTraceLoaded(true);
		std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2 * 6);
		synthesizer.render(samples.data(), samples.size() / 2, SampleRate);
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 4;
		const std::size_t frames = static_cast<std::size_t>(SampleRate) * 2;
		const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount>& bank = SoundEngine::frequencyBank();
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
		{
			const double amplitude = sineAmplitude(samples, start, frames, bank[i].frequencyHz);
			if (bank[i].frequencyHz < 300.0)
				require(amplitude < 1e-5, "a sub-300 Hz component leaked into the default bed");
			else
				require(amplitude > 1e-4, "a 300 Hz-or-higher bed carrier was missing");
		}
		SoundEngine::Synthesizer lowComparison;
		lowComparison.setTraceLoaded(true);
		lowComparison.setComparison(SoundEngine::AudioComparison(0.0f, 0.0f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> lowSamples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		lowComparison.render(lowSamples.data(), lowSamples.size() / 2, SampleRate);
		require(sineAmplitude(lowSamples, SampleRate * 2, SampleRate, 50.0) > 1e-4,
			"the muted low-frequency bed also muted a low-valued comparison");
		SoundEngine::Synthesizer lowBed;
		SoundEngine::Parameters lowBedParameters = lowBed.parameters();
		lowBedParameters.includeSub300HzBed = true;
		lowBed.setParameters(lowBedParameters);
		lowBed.setTraceLoaded(true);
		std::vector<float> lowBedSamples(static_cast<std::size_t>(SampleRate) * 2 * 3);
		lowBed.render(lowBedSamples.data(), lowBedSamples.size() / 2, SampleRate);
		require(sineAmplitude(lowBedSamples, SampleRate * 2, SampleRate, 50.0) > 1e-4,
			"the optional sub-300 Hz bed setting did not restore the low bands");
		require(sineAmplitude(samples, start, frames, 4500.0) > 1e-4, "4,500 Hz super-harmonic was missing from the bed");
		require(sineAmplitude(samples, start, frames, 17100.0) > 1e-4, "17,100 Hz super-harmonic was missing from the bed");
		require(sineAmplitude(samples, start, frames, 17400.0) < 1e-5, "unexpected 17,400 Hz carrier was generated");
		require(sineAmplitude(samples, start, frames, 18000.0) < 1e-5, "unexpected 18,000 Hz carrier was generated");
	}

	void testExponentialComparisonProfiles()
	{
		struct Profile
		{
			float below;
			float above;
		};
		const Profile profiles[] = {
			{ 0.25f, 0.25f }, { 0.5f, 0.5f }, { 0.75f, 0.75f },
			{ 0.25f, 0.75f }
		};
		const std::size_t centerIndex = 12; // 3,000 Hz, an exact equal-slot focus center.
		const std::size_t lowerIndex = centerIndex - 1;
		const std::size_t upperIndex = centerIndex + 1;

		for (std::size_t profile = 0; profile < sizeof(profiles) / sizeof(profiles[0]); ++profile)
		{
			SoundEngine::Synthesizer synthesizer;
			SoundEngine::Parameters parameters = synthesizer.parameters();
			parameters.masterLevel = 0.1f;
			parameters.bedLevel = 0.0f;
			parameters.comparisonBoostDb = 0.0f;
			parameters.heldKeyBoostDb = 0.0f;
			parameters.attackMs = 1.0f;
			parameters.releaseMs = 1.0f;
			parameters.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
			parameters.tauBelowBins = profiles[profile].below;
			parameters.tauAboveBins = profiles[profile].above;
			synthesizer.setParameters(parameters);
			synthesizer.setTraceLoaded(true);
			synthesizer.setComparison(SoundEngine::AudioComparison(0.75f, 0.75f, SoundEngine::HeldKeyParticipant::None));

			std::vector<float> samples(static_cast<std::size_t>(SampleRate) * 2);
			synthesizer.render(samples.data(), SampleRate, SampleRate);
			const std::size_t start = SampleRate / 2;
			const std::size_t frames = SampleRate / 2;
			const double center = normalizedBandAmplitude(samples, start, frames, centerIndex);
			const double lower = normalizedBandAmplitude(samples, start, frames, lowerIndex) / center;
			const double upper = normalizedBandAmplitude(samples, start, frames, upperIndex) / center;
			require(center > 0.01, "comparison envelope did not peak at its mapped band");
			require(near(lower, std::exp(-1.0 / profiles[profile].below), 0.025), "lower-side exponential envelope does not match its tau");
			require(near(upper, std::exp(-1.0 / profiles[profile].above), 0.025), "upper-side exponential envelope does not match its tau");
		}

		SoundEngine::Synthesizer upperTail;
		SoundEngine::Parameters parameters = upperTail.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.attackMs = 1.0f;
		parameters.mappingMode = SoundEngine::MappingMode::LinearHz;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		upperTail.setParameters(parameters);
		upperTail.setTraceLoaded(true);
		upperTail.setComparison(SoundEngine::AudioComparison(1.0f, 1.0f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> tailSamples(static_cast<std::size_t>(SampleRate) * 2);
		upperTail.render(tailSamples.data(), SampleRate, SampleRate);
		const std::size_t tailStart = SampleRate / 2;
		const std::size_t tailFrames = SampleRate / 2;
		const double peak = normalizedBandAmplitude(tailSamples, tailStart, tailFrames, 16);
		const double superHarmonic = normalizedBandAmplitude(tailSamples, tailStart, tailFrames, 17);
		require(peak > 0.01 && near(superHarmonic / peak, std::exp(-1.0 / 0.5), 0.025),
			"focus at 4,200 Hz did not produce the expected 4,500 Hz tail");

		SoundEngine::Synthesizer betweenBands;
		parameters = betweenBands.parameters();
		parameters.masterLevel = 0.1f;
		parameters.bedLevel = 0.0f;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
		parameters.tauBelowBins = 0.5f;
		parameters.tauAboveBins = 0.5f;
		betweenBands.setParameters(parameters);
		betweenBands.setTraceLoaded(true);
		const float halfwayPosition = 12.5f / static_cast<float>(SoundEngine::FocusBandCount - 1);
		betweenBands.setComparison(SoundEngine::AudioComparison(halfwayPosition, halfwayPosition, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> halfwaySamples(static_cast<std::size_t>(SampleRate) * 2);
		betweenBands.render(halfwaySamples.data(), SampleRate, SampleRate);
		const double lowerPeak = normalizedBandAmplitude(halfwaySamples, tailStart, tailFrames, 12);
		const double upperPeak = normalizedBandAmplitude(halfwaySamples, tailStart, tailFrames, 13);
		require(lowerPeak > 0.01 && near(lowerPeak, upperPeak, 0.025),
			"an inter-band focus did not share its peak smoothly between the nearest bins");
	}

	void testPhaseContinuityAndSmoothing()
	{
		SoundEngine::Synthesizer whole;
		SoundEngine::Synthesizer chunked;
		whole.setTraceLoaded(true);
		chunked.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.22f, 0.83f, SoundEngine::HeldKeyParticipant::Second);
		whole.setComparison(comparison);
		chunked.setComparison(comparison);
		const std::size_t totalFrames = 12000;
		std::vector<float> wholeOutput(totalFrames * 2);
		std::vector<float> chunkedOutput(totalFrames * 2);
		whole.render(wholeOutput.data(), totalFrames, SampleRate);
		chunked.render(chunkedOutput.data(), 3500, SampleRate);
		chunked.render(chunkedOutput.data() + 3500 * 2, totalFrames - 3500, SampleRate);
		require(wholeOutput == chunkedOutput, "render block boundary changed phase or envelope continuity");

		SoundEngine::Synthesizer attack;
		SoundEngine::Synthesizer withoutComparison;
		SoundEngine::Parameters attackParameters = attack.parameters();
		attackParameters.masterLevel = 0.1f;
		attackParameters.tauBelowBins = 0.25f;
		attackParameters.tauAboveBins = 0.25f;
		attack.setParameters(attackParameters);
		withoutComparison.setParameters(attackParameters);
		attack.setTraceLoaded(true);
		withoutComparison.setTraceLoaded(true);
		std::vector<float> warm(static_cast<std::size_t>(SampleRate) * 2);
		attack.render(warm.data(), SampleRate, SampleRate);
		withoutComparison.render(warm.data(), SampleRate, SampleRate);
		attack.setComparison(SoundEngine::AudioComparison(0.18f, 0.69f, SoundEngine::HeldKeyParticipant::None));
		std::vector<float> onset(16000 * 2);
		std::vector<float> baseline(16000 * 2);
		attack.render(onset.data(), 16000, SampleRate);
		withoutComparison.render(baseline.data(), 16000, SampleRate);
		double earlyEnergy = 0.0;
		double settledEnergy = 0.0;
		for (std::size_t i = 0; i < 300; ++i)
		{
			const double delta = onset[i * 2] - baseline[i * 2];
			earlyEnergy += delta * delta;
		}
		for (std::size_t i = 15000; i < 16000; ++i)
		{
			const double delta = onset[i * 2] - baseline[i * 2];
			settledEnergy += delta * delta;
		}
		const double earlyRms = std::sqrt(earlyEnergy / 300.0);
		const double settledRms = std::sqrt(settledEnergy / 1000.0);
		require(earlyRms < settledRms * 0.65, "comparison attack was not smoothed: early=" + std::to_string(earlyRms) + ", settled=" + std::to_string(settledRms));

		SoundEngine::Synthesizer changingSettings;
		SoundEngine::Synthesizer fixedSettings;
		changingSettings.setTraceLoaded(true);
		fixedSettings.setTraceLoaded(true);
		changingSettings.render(warm.data(), SampleRate, SampleRate);
		fixedSettings.render(warm.data(), SampleRate, SampleRate);
		SoundEngine::Parameters louder = changingSettings.parameters();
		louder.masterLevel = 1.0f;
		changingSettings.setParameters(louder);
		std::vector<float> gainRamp(16000 * 2);
		std::vector<float> gainReference(16000 * 2);
		changingSettings.render(gainRamp.data(), 16000, SampleRate);
		fixedSettings.render(gainReference.data(), 16000, SampleRate);
		earlyEnergy = 0.0;
		settledEnergy = 0.0;
		for (std::size_t i = 0; i < 300; ++i)
		{
			const double delta = gainRamp[i * 2] - gainReference[i * 2];
			earlyEnergy += delta * delta;
		}
		for (std::size_t i = 15000; i < 16000; ++i)
		{
			const double delta = gainRamp[i * 2] - gainReference[i * 2];
			settledEnergy += delta * delta;
		}
		const double earlyGainDelta = std::sqrt(earlyEnergy / 300.0);
		const double settledGainDelta = std::sqrt(settledEnergy / 1000.0);
		require(earlyGainDelta < settledGainDelta * 0.65, "master-level changes were not smoothed");
	}

	void testChorusSidebands()
	{
		std::vector<float> outputs[2];
		for (int variant = 0; variant < 2; ++variant)
		{
			SoundEngine::Synthesizer synthesizer;
			SoundEngine::Parameters parameters = synthesizer.parameters();
			parameters.chorusMix = variant == 0 ? 0.0f : 0.35f;
			parameters.chorusRateHz = 1.0f;
			parameters.chorusDepthMs = 2.0f;
			synthesizer.setParameters(parameters);
			synthesizer.setTraceLoaded(true);
			outputs[variant].resize(static_cast<std::size_t>(SampleRate) * 2 * 6);
			synthesizer.render(outputs[variant].data(), outputs[variant].size() / 2, SampleRate);
		}
		const std::size_t start = static_cast<std::size_t>(SampleRate) * 2;
		const std::size_t frames = static_cast<std::size_t>(SampleRate) * 4;
		const double drySideband = sineAmplitude(outputs[0], start, frames, 299.0) + sineAmplitude(outputs[0], start, frames, 301.0);
		const double chorusSideband = sineAmplitude(outputs[1], start, frames, 299.0) + sineAmplitude(outputs[1], start, frames, 301.0);
		require(drySideband < 1e-5, "chorus-off output contains unexpected 1 Hz sidebands");
		require(chorusSideband > 1e-4, "chorus did not create measurable modulation sidebands");
	}

	void testChorusWeightingModes()
	{
		SoundEngine::Synthesizer uniform;
		SoundEngine::Synthesizer envelopeWeighted;
		SoundEngine::Parameters uniformParameters = uniform.parameters();
		SoundEngine::Parameters weightedParameters = envelopeWeighted.parameters();
		weightedParameters.chorusWeighting = SoundEngine::ChorusWeighting::ComparisonEnvelope;
		uniformParameters.chorusMix = 0.0f;
		weightedParameters.chorusMix = 0.0f;
		uniform.setParameters(uniformParameters);
		envelopeWeighted.setParameters(weightedParameters);
		uniform.setTraceLoaded(true);
		envelopeWeighted.setTraceLoaded(true);
		const SoundEngine::AudioComparison comparison(0.35f, 0.38f, SoundEngine::HeldKeyParticipant::Second);
		uniform.setComparison(comparison);
		envelopeWeighted.setComparison(comparison);
		std::vector<float> warmUniform(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> warmWeighted(static_cast<std::size_t>(SampleRate) * 2);
		uniform.render(warmUniform.data(), SampleRate, SampleRate);
		envelopeWeighted.render(warmWeighted.data(), SampleRate, SampleRate);
		std::vector<float> dryUniform(4096 * 2);
		std::vector<float> dryWeighted(4096 * 2);
		uniform.render(dryUniform.data(), 4096, SampleRate);
		envelopeWeighted.render(dryWeighted.data(), 4096, SampleRate);
		require(dryUniform == dryWeighted, "chorus weighting changed the signal while chorus was disabled");

		uniformParameters.chorusMix = 0.1f;
		weightedParameters.chorusMix = 0.1f;
		uniform.setParameters(uniformParameters);
		envelopeWeighted.setParameters(weightedParameters);
		std::vector<float> uniformOutput(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> weightedOutput(static_cast<std::size_t>(SampleRate) * 2);
		uniform.render(uniformOutput.data(), SampleRate, SampleRate);
		envelopeWeighted.render(weightedOutput.data(), SampleRate, SampleRate);
		double maximumDifference = 0.0;
		for (std::size_t i = 0; i < uniformOutput.size(); ++i)
			maximumDifference = std::max(maximumDifference,
				static_cast<double>(std::fabs(uniformOutput[i] - weightedOutput[i])));
		require(maximumDifference > 1e-4, "comparison-envelope chorus weighting did not alter wet output");
	}

	void testHeadroomAndSilence()
	{
		SoundEngine::Synthesizer silent;
		std::vector<float> silence(4096 * 2, 1.0f);
		silent.render(silence.data(), 4096, SampleRate);
		require(maxAbs(silence) == 0.0, "new engine should be silent without a loaded trace");

		SoundEngine::Parameters parameters = silent.parameters();
		parameters.masterLevel = 1.0f;
		parameters.bedLevel = 1.0f;
		parameters.comparisonBoostDb = 18.0f;
		parameters.heldKeyBoostDb = 18.0f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			parameters.bandTrimDb[i] = 12.0f;
		silent.setParameters(parameters);
		silent.setTraceLoaded(true);
		silent.setComparison(SoundEngine::AudioComparison(0.5f, 0.5f, SoundEngine::HeldKeyParticipant::Second));
		std::vector<float> loud(static_cast<std::size_t>(SampleRate) * 2);
		silent.render(loud.data(), loud.size() / 2, SampleRate);
		require(maxAbs(loud) <= 0.950001, "maximum gain exceeded the soft headroom ceiling");

		parameters.enabled = false;
		silent.setParameters(parameters);
		std::fill(silence.begin(), silence.end(), 1.0f);
		silent.render(silence.data(), 4096, SampleRate);
		require(maxAbs(silence) == 0.0, "disabled engine did not return exact silence");

		SoundEngine::Synthesizer fading;
		fading.setTraceLoaded(true);
		std::vector<float> audible(static_cast<std::size_t>(SampleRate) * 2 / 4);
		fading.render(audible.data(), audible.size() / 2, SampleRate);
		require(maxAbs(audible) > 0.001, "loaded trace should make the harmonic bed audible");
		fading.setTraceLoaded(false);
		std::vector<float> tail(static_cast<std::size_t>(SampleRate) * 2 * 2);
		fading.render(tail.data(), tail.size() / 2, SampleRate);
		std::vector<float> finalSecond(tail.begin() + static_cast<std::size_t>(SampleRate) * 2, tail.end());
		require(maxAbs(std::vector<float>(tail.begin(), tail.begin() + static_cast<std::size_t>(SampleRate) / 5 * 2)) > 0.001,
			"clearing a trace should fade the bed instead of cutting it immediately");
		require(maxAbs(finalSecond) < 1e-5, "cleared trace bed did not fade to silence");
	}

	void testComparisonEnvelopeClearsToZeroWithBedRunning()
	{
		SoundEngine::Synthesizer played;
		SoundEngine::Synthesizer bedOnly;
		played.setTraceLoaded(true);
		bedOnly.setTraceLoaded(true);

		std::vector<float> warmPlayed(4096 * 2);
		std::vector<float> warmBed(4096 * 2);
		played.render(warmPlayed.data(), 4096, SampleRate);
		bedOnly.render(warmBed.data(), 4096, SampleRate);

		const SoundEngine::AudioComparison comparisons[] =
		{
			SoundEngine::AudioComparison(0.12f, 0.16f, SoundEngine::HeldKeyParticipant::Second),
			SoundEngine::AudioComparison(0.54f, 0.58f, SoundEngine::HeldKeyParticipant::Second),
			SoundEngine::AudioComparison(0.88f, 0.93f, SoundEngine::HeldKeyParticipant::Second)
		};
		double comparisonDifference = 0.0;
		for (std::size_t i = 0; i < sizeof(comparisons) / sizeof(comparisons[0]); ++i)
		{
			played.setComparison(comparisons[i]);
			std::vector<float> comparisonOutput(4096 * 2);
			std::vector<float> bedOutput(4096 * 2);
			played.render(comparisonOutput.data(), 4096, SampleRate);
			bedOnly.render(bedOutput.data(), 4096, SampleRate);
			for (std::size_t sample = 0; sample < comparisonOutput.size(); ++sample)
				comparisonDifference = std::max(comparisonDifference,
					static_cast<double>(std::fabs(comparisonOutput[sample] - bedOutput[sample])));
		}
		require(comparisonDifference > 1.0e-5, "played comparisons did not produce a signal above the harmonic bed");

		played.clearComparison();
		std::vector<float> releasedOutput(static_cast<std::size_t>(SampleRate) * 2);
		std::vector<float> uninterruptedBed(releasedOutput.size());
		played.render(releasedOutput.data(), releasedOutput.size() / 2, SampleRate);
		bedOnly.render(uninterruptedBed.data(), uninterruptedBed.size() / 2, SampleRate);

		require(played.traceLoaded(), "clearing comparison focus unexpectedly stopped the loaded trace bed");
		require(maxAbs(releasedOutput) > 1.0e-4, "harmonic bed stopped while comparison focus was cleared");
		double maximumResidual = 0.0;
		const std::size_t settledStart = static_cast<std::size_t>(SampleRate) * 2 / 2;
		for (std::size_t sample = settledStart; sample < releasedOutput.size(); ++sample)
			maximumResidual = std::max(maximumResidual,
				static_cast<double>(std::fabs(releasedOutput[sample] - uninterruptedBed[sample])));
		require(maximumResidual == 0.0,
			"comparison envelopes left residual output after their release settled; max delta=" +
			std::to_string(maximumResidual));
	}

	void testDenseRetargeting()
	{
		SoundEngine::Synthesizer synthesizer;
		synthesizer.setTraceLoaded(true);
		const std::size_t itemCount = 800;
		std::size_t expectedUpdates = 0;
		for (std::size_t outer = 1; outer < itemCount; ++outer)
		{
			for (std::size_t inner = outer; inner > 0; --inner)
			{
				const float first = static_cast<float>(inner) / static_cast<float>(itemCount - 1);
				const float second = static_cast<float>(inner - 1) / static_cast<float>(itemCount - 1);
				const SoundEngine::HeldKeyParticipant held = (inner & 1) ? SoundEngine::HeldKeyParticipant::Second : SoundEngine::HeldKeyParticipant::None;
				synthesizer.setComparison(SoundEngine::AudioComparison(first, second, held));
				++expectedUpdates;
			}
		}
		require(expectedUpdates == 319600, "dense trace test did not cover the 800-item comparison count");
		require(synthesizer.comparisonUpdateCount() == expectedUpdates, "dense events were not applied immediately in order");
		std::vector<float> finalState(static_cast<std::size_t>(SampleRate) * 2 / 2);
		synthesizer.render(finalState.data(), finalState.size() / 2, SampleRate);
		require(maxAbs(finalState) > 0.001, "latest dense comparison did not reach the output");
	}

	void testDefaultsAndCommandDispatch()
	{
		SoundEngine::Parameters defaults;
		require(defaults.masterLevel == 0.5f && defaults.bedLevel == 0.15f, "default master/bed levels changed");
		require(defaults.bedHarmonicExponent == 1.5f, "default bed spectrum should match the selected preview rolloff");
		require(!defaults.includeSub300HzBed, "sub-300 Hz bed components should be quiet by default");
		require(defaults.bedComb.enabled && defaults.bedComb.applyPhase, "default comb profile should match the selected preview");
		require(near(defaults.bedComb.delayMicroseconds, 222.222, 0.001) && defaults.bedComb.phaseOffsetDegrees == 12.0f &&
			defaults.bedComb.wetAtLowFrequency == 0.95f && defaults.bedComb.wetAtHighFrequency == 0.90f &&
			defaults.bedComb.fadeStartHz == 6600.0f && defaults.bedComb.fadeEndHz == 15600.0f && defaults.bedComb.fadeExponent == 3.0f,
			"default comb values should match the selected preview");
		require(defaults.comparisonSpreadMode == SoundEngine::ComparisonSpreadMode::FixedBins,
			"default comparison spread should preserve fixed-bin behavior");
		require(defaults.comparisonBoostDb == 6.0f && defaults.heldKeyBoostDb == 6.0f, "default comparison gains changed");
		require(defaults.tauBelowBins == 0.5f && defaults.tauAboveBins == 0.5f, "default comparison spread profile changed");
		require(defaults.chorusMix == 0.0f && defaults.chorusRateHz == 1.0f && defaults.chorusDepthMs == 2.0f, "default chorus parameters changed");
		require(defaults.chorusWeighting == SoundEngine::ChorusWeighting::Uniform, "default chorus weighting changed");
		require(defaults.attackMs == 8.0f && defaults.releaseMs == 8.0f && defaults.mappingMode == SoundEngine::MappingMode::LinearHz, "default mapping or envelopes changed");
		require(defaults.enabled && !defaults.includeSub300HzBed, "default synthesis enable or low-bed state changed");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			require(defaults.bandTrimDb[i] == 0.0f, "default per-band trim should be zero");

		SoundEngine::Synthesizer synthesizer;
		SoundEngine::AudioCommand command;
		command.kind = SoundEngine::AudioCommandKind::TraceLoaded;
		command.traceLoaded = true;
		synthesizer.applyCommand(command);
		require(synthesizer.traceLoaded(), "trace-loaded command was ignored");
		command.kind = SoundEngine::AudioCommandKind::Comparison;
		command.comparison = SoundEngine::AudioComparison(0.1f, 0.9f, SoundEngine::HeldKeyParticipant::First);
		synthesizer.applyCommand(command);
		require(synthesizer.comparisonUpdateCount() == 1, "comparison command was not applied");
		command.kind = SoundEngine::AudioCommandKind::ClearComparison;
		synthesizer.applyCommand(command);
		require(synthesizer.traceLoaded(), "clearing a comparison should preserve the trace bed");
	}

	void testSemanticEventAccents()
	{
		SoundEngine::Synthesizer active;
		SoundEngine::Synthesizer muted;
		SoundEngine::Parameters parameters = active.parameters();
		parameters.masterLevel = 0.7f;
		parameters.bedLevel = 0.0f;
		parameters.enabled = true;
		parameters.comparisonBoostDb = 0.0f;
		parameters.heldKeyBoostDb = 0.0f;
		parameters.accentRouting.fill(false);
		parameters.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = true;
		parameters.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 0.2f;
		active.setParameters(parameters);
		SoundEngine::Parameters mutedParameters = parameters;
		mutedParameters.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = false;
		muted.setParameters(mutedParameters);
		active.setTraceLoaded(true);
		muted.setTraceLoaded(true);
		std::vector<float> warmup(static_cast<std::size_t>(SampleRate) * 2 / 10);
		active.render(warmup.data(), warmup.size() / 2, SampleRate);
		muted.render(warmup.data(), warmup.size() / 2, SampleRate);
		SoundEngine::EventAccentSummary summary;
		summary.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 3;
		summary.representativePositions[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 0.7f;
		active.setEventAccentSummary(summary);
		muted.setEventAccentSummary(summary);
		std::vector<float> activeSamples(static_cast<std::size_t>(SampleRate) * 2 / 5);
		std::vector<float> mutedSamples(activeSamples.size());
		active.render(activeSamples.data(), activeSamples.size() / 2, SampleRate);
		muted.render(mutedSamples.data(), mutedSamples.size() / 2, SampleRate);
		auto maxDifference = [](const std::vector<float>& left, const std::vector<float>& right)
		{
			double peak = 0.0;
			for (std::size_t i = 0; i < left.size(); ++i)
				peak = std::max(peak, std::fabs(static_cast<double>(left[i] - right[i])));
			return peak;
		};
		const double accentDifference = maxDifference(activeSamples, mutedSamples);
		require(accentDifference > 0.01, "routed probe accents should produce an audible transient");
		active.setEventAccentSummary(SoundEngine::EventAccentSummary());
		std::vector<float> released(static_cast<std::size_t>(SampleRate) * 2 / 5);
		std::vector<float> mutedReleased(released.size());
		active.render(released.data(), released.size() / 2, SampleRate);
		muted.render(mutedReleased.data(), mutedReleased.size() / 2, SampleRate);
		require(maxDifference(released, mutedReleased) < accentDifference,
			"clearing a frame summary should release the accent without a stale event queue");
	}
}

int main()
{
	try
	{
		testFrequencyBankAndMapping();
		testCombFilterBedShaping();
		testPhaseAwareBedAndHarmonicRolloff();
		testPositionScaledComparisonSpread();
		testMasterGainRemainsLinearUnderHeadroomProtection();
		testHeldKeyWeighting();
		testGeneratedCarrierCenters();
		testExponentialComparisonProfiles();
		testPhaseContinuityAndSmoothing();
		testChorusSidebands();
		testChorusWeightingModes();
		testHeadroomAndSilence();
		testComparisonEnvelopeClearsToZeroWithBedRunning();
		testDenseRetargeting();
		testDefaultsAndCommandDispatch();
		testSemanticEventAccents();
		std::cout << "SoundEngine tests passed (" << assertions << " assertions).\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "SoundEngine test failure: " << error.what() << "\n";
		return 1;
	}
}
