#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "WavWriter.h"
#include "SoundEngine.h"
#include "SortingLogic.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace
{
	const std::uint32_t SampleRate = 44100;
	const std::size_t ClipSeconds = 5;
	const std::size_t TraceItemCount = 80;
	const std::uint32_t TraceSeed = 0x4A475353u;

	struct TauProfile
	{
		const char* name;
		float belowBins;
		float aboveBins;
	};

	const TauProfile TauProfiles[] = {
		{ "tau_0_5", 0.5f, 0.5f }
	};

	struct ComparisonRecord
	{
		float firstPosition;
		float secondPosition;
		SoundEngine::HeldKeyParticipant heldKey;
	};

	struct PreviewPattern
	{
		const char* name;
		std::vector<ComparisonRecord> events;
	};

	struct PreviewSettings
	{
		float masterLevel;
		float bedLevel;
		float bedHarmonicExponent;
		SoundEngine::ComparisonSpreadMode spreadMode;
	};

	bool ensureDirectory(const std::string& path)
	{
		if (CreateDirectoryA(path.c_str(), nullptr))
			return true;
		return GetLastError() == ERROR_ALREADY_EXISTS;
	}

	bool findTraceComparisons(std::vector<ComparisonRecord>& comparisons)
	{
		std::vector<sortinglogic::SortItem> input;
		input.reserve(TraceItemCount);
		for (std::size_t i = 0; i < TraceItemCount; ++i)
			input.push_back(sortinglogic::SortItem(static_cast<std::uint64_t>(i),
				static_cast<float>(i + 1) / static_cast<float>(TraceItemCount)));

		std::mt19937 random(TraceSeed);
		std::shuffle(input.begin(), input.end(), random);

		sortinglogic::SortTrace trace = sortinglogic::SortTraceBuilder::build(
			input, sortinglogic::SortDirection::Ascending, TraceSeed);
		std::vector<float> valuesById(TraceItemCount, 0.0f);
		float minimum = input.front().value;
		float maximum = input.front().value;
		for (std::size_t i = 0; i < input.size(); ++i)
		{
			valuesById[static_cast<std::size_t>(input[i].stableId)] = input[i].value;
			minimum = std::min(minimum, input[i].value);
			maximum = std::max(maximum, input[i].value);
		}

		std::uint64_t heldItemId = sortinglogic::NoItemId;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const sortinglogic::SemanticEvent& event = trace.events[i];
			if (event.type == sortinglogic::SemanticEventType::HoldKey)
			{
				heldItemId = event.item.stableId;
			}
			else if (event.type == sortinglogic::SemanticEventType::Compare)
			{
				SoundEngine::HeldKeyParticipant held = SoundEngine::HeldKeyParticipant::None;
				if (event.firstItemId == heldItemId)
					held = SoundEngine::HeldKeyParticipant::First;
				else if (event.secondItemId == heldItemId)
					held = SoundEngine::HeldKeyParticipant::Second;

				comparisons.push_back(ComparisonRecord{
					SoundEngine::normalizeValue(valuesById[static_cast<std::size_t>(event.firstItemId)], minimum, maximum),
					SoundEngine::normalizeValue(valuesById[static_cast<std::size_t>(event.secondItemId)], minimum, maximum),
					held });
			}
			else if (event.type == sortinglogic::SemanticEventType::PlaceKey)
			{
				heldItemId = sortinglogic::NoItemId;
			}
		}
		return !comparisons.empty();
	}

	bool findOrderedTraceComparisons(std::vector<ComparisonRecord>& comparisons)
	{
		std::vector<sortinglogic::SortItem> input;
		input.reserve(TraceItemCount);
		for (std::size_t i = 0; i < TraceItemCount; ++i)
			input.push_back(sortinglogic::SortItem(static_cast<std::uint64_t>(i), static_cast<float>(i + 1)));

		const sortinglogic::SortTrace trace = sortinglogic::SortTraceBuilder::build(
			input, sortinglogic::SortDirection::Ascending, TraceSeed);
		std::vector<float> valuesById(TraceItemCount, 0.0f);
		for (std::size_t i = 0; i < input.size(); ++i)
			valuesById[static_cast<std::size_t>(input[i].stableId)] = input[i].value;

		std::uint64_t heldItemId = sortinglogic::NoItemId;
		for (std::size_t i = 0; i < trace.events.size(); ++i)
		{
			const sortinglogic::SemanticEvent& event = trace.events[i];
			if (event.type == sortinglogic::SemanticEventType::HoldKey)
				heldItemId = event.item.stableId;
			else if (event.type == sortinglogic::SemanticEventType::Compare)
			{
				if (event.secondItemId != heldItemId)
					return false;
				comparisons.push_back(ComparisonRecord{
					SoundEngine::normalizeValue(valuesById[static_cast<std::size_t>(event.firstItemId)], 1.0f, static_cast<float>(TraceItemCount)),
					SoundEngine::normalizeValue(valuesById[static_cast<std::size_t>(event.secondItemId)], 1.0f, static_cast<float>(TraceItemCount)),
					SoundEngine::HeldKeyParticipant::Second });
			}
			else if (event.type == sortinglogic::SemanticEventType::PlaceKey)
				heldItemId = sortinglogic::NoItemId;
		}

		if (comparisons.size() != TraceItemCount - 1)
			return false;
		for (std::size_t i = 0; i < comparisons.size(); ++i)
			if (comparisons[i].firstPosition >= comparisons[i].secondPosition ||
				comparisons[i].heldKey != SoundEngine::HeldKeyParticipant::Second)
				return false;
		return true;
	}

	std::vector<ComparisonRecord> sampleTraceComparisons(
		const std::vector<ComparisonRecord>& allComparisons,
		std::size_t sampleCount)
	{
		std::vector<ComparisonRecord> sample;
		if (allComparisons.empty() || sampleCount == 0)
			return sample;
		sample.reserve(sampleCount);
		for (std::size_t i = 0; i < sampleCount; ++i)
		{
			const std::size_t index = sampleCount == 1
				? allComparisons.size() / 2
				: i * (allComparisons.size() - 1) / (sampleCount - 1);
			sample.push_back(allComparisons[index]);
		}
		return sample;
	}

	std::vector<ComparisonRecord> makeLowHighPattern(std::size_t eventCount)
	{
		const float lowFirst = SoundEngine::normalizeValue(300.0f, 50.0f, 4200.0f);
		const float lowSecond = SoundEngine::normalizeValue(600.0f, 50.0f, 4200.0f);
		const float highFirst = SoundEngine::normalizeValue(3900.0f, 50.0f, 4200.0f);
		const float highSecond = SoundEngine::normalizeValue(4200.0f, 50.0f, 4200.0f);
		std::vector<ComparisonRecord> events;
		events.reserve(eventCount);
		for (std::size_t i = 0; i < eventCount; ++i)
		{
			const bool high = (i & 1u) != 0;
			events.push_back(ComparisonRecord{
				high ? highFirst : lowFirst,
				high ? highSecond : lowSecond,
				high ? SoundEngine::HeldKeyParticipant::Second : SoundEngine::HeldKeyParticipant::First });
		}
		return events;
	}

	bool writeComparisonClip(
		const std::string& path,
		const std::vector<ComparisonRecord>& events,
		const TauProfile& profile,
		float chorusMix,
		SoundEngine::ChorusWeighting chorusWeighting,
		std::size_t clipSeconds,
		const SoundEngine::BedCombFilterParameters* bedComb = nullptr,
		const PreviewSettings* previewSettings = nullptr)
	{
		if (events.empty())
			return false;

		SoundEngine::Synthesizer synthesizer;
		SoundEngine::Parameters parameters = synthesizer.parameters();
		parameters.mappingMode = SoundEngine::MappingMode::LinearHz;
		parameters.bedLevel = 0.15f;
		parameters.includeSub300HzBed = false;
		parameters.tauBelowBins = profile.belowBins;
		parameters.tauAboveBins = profile.aboveBins;
		parameters.attackMs = 8.0f;
		parameters.releaseMs = 8.0f;
		parameters.chorusMix = chorusMix;
		parameters.chorusWeighting = chorusWeighting;
		parameters.chorusRateHz = 1.0f;
		parameters.chorusDepthMs = 2.0f;
		if (bedComb != nullptr)
			parameters.bedComb = *bedComb;
		if (previewSettings != nullptr)
		{
			parameters.masterLevel = previewSettings->masterLevel;
			parameters.bedLevel = previewSettings->bedLevel;
			parameters.bedHarmonicExponent = previewSettings->bedHarmonicExponent;
			parameters.comparisonSpreadMode = previewSettings->spreadMode;
		}
		synthesizer.setParameters(parameters);
		synthesizer.setTraceLoaded(true);

		const std::size_t totalFrames = static_cast<std::size_t>(SampleRate) * clipSeconds;
		std::vector<float> samples(totalFrames * 2);
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			const std::size_t begin = i * totalFrames / events.size();
			const std::size_t end = (i + 1) * totalFrames / events.size();
			const ComparisonRecord& event = events[i];
			synthesizer.setComparison(SoundEngine::AudioComparison(
				event.firstPosition, event.secondPosition, event.heldKey));
			synthesizer.render(samples.data() + begin * 2, end - begin, SampleRate);
		}
		return writeWav16Stereo(path, samples, SampleRate);
	}

	bool writeManifest(const std::string& directory, const std::vector<std::string>& fileNames)
	{
		std::ofstream file((directory + "\\README.txt").c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
		if (!file)
			return false;
		file << "Actually Good Sorting Sounds - fast comparison SoundEngine listening previews\r\n"
			<< "Format: deterministic 44.1 kHz stereo, 16-bit PCM WAV; every clip is five seconds.\r\n"
			<< "Focus centers map only from 50 Hz to 4,200 Hz. The 60-band bank continues to 17,100 Hz,\r\n"
			<< "where higher harmonics receive bed energy and comparison-envelope tails, never focus peaks.\r\n"
			<< "The 50, 100, and 150 Hz bands remain in the bank for low-valued comparisons, but are\r\n"
			<< "excluded from the continuous bed.\r\n"
			<< "All clips use a 15% harmonic bed, +6 dB comparison gain, +6 dB held-key gain,\r\n"
			<< "8 ms attack, 8 ms release, and dry output (chorus off). The comparison envelope is\r\n"
			<< "symmetric tau=0.5 bin steps, about 13.5% of peak one bin away.\r\n\r\n"
			<< "Each five-second clip has 8 or 16 comparison changes per second. low_high clips alternate\r\n"
			<< "low-register (300/600 Hz) and high-register (3,900/4,200 Hz) pairs every 125 or 62.5 ms.\r\n"
			<< "trace clips use 40 or 80 comparisons evenly sampled from the full trace of a fixed seeded\r\n"
			<< "80-item ascending Insertion Sort run. Each trace event keeps its actual held-key participant.\r\n\r\n"
			<< "Files use <profile>_<pattern>.wav naming. The earlier 22-clip refined pack is superseded\r\n"
			<< "by this focused timing and envelope comparison. Chorus remains off by default.\r\n\r\nFiles:\r\n";
		for (std::size_t i = 0; i < fileNames.size(); ++i)
			file << fileNames[i] << "\r\n";
		return file.good();
	}

	bool writeOrderedSweepManifest(const std::string& directory, const std::vector<std::string>& fileNames)
	{
		std::ofstream file((directory + "\\README.txt").c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
		if (!file)
			return false;
		file << "Actually Good Sorting Sounds - ordered comparison chorus A/B previews, 300 Hz+ bed\r\n"
			<< "Format: deterministic 44.1 kHz stereo, 16-bit PCM WAV; every clip is two seconds.\r\n"
			<< "The source is an ascending, already-ordered 80-item array passed through the real\r\n"
			<< "Insertion Sort trace builder. It produces 79 comparisons: each held item is compared\r\n"
			<< "with its immediately preceding item, sweeping from the low end to the high end.\r\n"
			<< "The comparisons are spread evenly across the clip (about 25.3 ms per comparison).\r\n\r\n"
			<< "The 50, 100, and 150 Hz bands stay in the comparison bank but are excluded from the\r\n"
			<< "continuous bed; low-valued comparisons can still emphasize them. The bed contains\r\n"
			<< "300 Hz and higher. Each weighting mode is rendered at 0%, 10%, and 20% chorus mix.\r\n"
			<< "Uniform sends the entire harmonic sum through the existing modulated-delay mix.\r\n"
			<< "Envelope-weighted chorus shapes the delayed signal by the current combined comparison\r\n"
			<< "envelope across the frequency bands, normalized to that frame's strongest comparison band.\r\n"
			<< "The 0% pair is identical. Each clip uses tau=0.5 bins, a 15% 300 Hz+ bed,\r\n"
			<< "8 ms attack/release, 1 Hz chorus rate, and 2 ms chorus depth.\r\n"
			<< "The supplied Andrut reference is 44.1 kHz mono IEEE-float32 and about 2.004 seconds;\r\n"
			<< "these previews are exact 2.000-second 44.1 kHz stereo PCM16 files.\r\n\r\nFiles:\r\n";
		for (std::size_t i = 0; i < fileNames.size(); ++i)
			file << fileNames[i] << "\r\n";
		return file.good();
	}

	bool generateOrderedSweepPreviews(const std::string& outputDirectory)
	{
		if (!ensureDirectory(outputDirectory))
			return false;
		std::vector<ComparisonRecord> events;
		if (!findOrderedTraceComparisons(events))
		{
			std::cerr << "The ordered input did not produce the expected 79 adjacent-key comparisons.\n";
			return false;
		}

		struct ChorusVariant
		{
			const char* name;
			SoundEngine::ChorusWeighting weighting;
		};
		const ChorusVariant weightingModes[] = {
			{ "uniform", SoundEngine::ChorusWeighting::Uniform },
			{ "envelope_weighted", SoundEngine::ChorusWeighting::ComparisonEnvelope }
		};
		struct ChorusLevel
		{
			const char* name;
			float mix;
		};
		const ChorusLevel levels[] = {
			{ "0pct", 0.0f }, { "10pct", 0.10f }, { "20pct", 0.20f }
		};
		const TauProfile profile = { "tau_0_5", 0.5f, 0.5f };
		std::vector<std::string> generatedFiles;
		generatedFiles.reserve(6);
		for (std::size_t mode = 0; mode < sizeof(weightingModes) / sizeof(weightingModes[0]); ++mode)
		{
			for (std::size_t level = 0; level < sizeof(levels) / sizeof(levels[0]); ++level)
			{
				const std::string fileName = std::string("ordered_sweep_") + weightingModes[mode].name +
					"_chorus_" + levels[level].name + ".wav";
				if (!writeComparisonClip(outputDirectory + "\\" + fileName, events, profile,
					levels[level].mix, weightingModes[mode].weighting, 2))
				{
					std::cerr << "Failed to write ordered sweep preview " << fileName << "\n";
					return false;
				}
				generatedFiles.push_back(fileName);
				std::cout << "Wrote " << outputDirectory << "\\" << fileName << "\n";
			}
		}
		return writeOrderedSweepManifest(outputDirectory, generatedFiles);
	}

	bool writeCombBedManifest(const std::string& directory, const std::vector<std::string>& fileNames)
	{
		std::ofstream file((directory + "\\README.txt").c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
		if (!file)
			return false;
		file << "Actually Good Sorting Sounds - experimental comb-shaped bed A/B previews\r\n"
			<< "Format: deterministic 44.1 kHz stereo, 16-bit PCM WAV; every clip is exactly two seconds.\r\n"
			<< "Each clip uses the same 79 adjacent comparisons from Insertion Sort on an already ordered\r\n"
			<< "80-item array, evenly swept across two seconds, with the held-key role preserved.\r\n\r\n"
			<< "The continuous bed already has phase-locked 300 Hz harmonics with 1/k amplitudes: this is\r\n"
			<< "the magnitude profile of a truncated sawtooth Fourier series. The comparison tones are\r\n"
			<< "summed separately and are not comb-filtered. The 50/100/150 Hz bands stay available to\r\n"
			<< "comparisons and remain outside the bed.\r\n\r\n"
			<< "The experimental response is the normalized magnitude of a dry-plus-delayed two-path comb:\r\n"
			<< "H(f)=(1-w/2)+(w/2)*exp(-j*(2*pi*f*tau+phase)). It uses tau=222.222 us (4.5 kHz\r\n"
			<< "spacing), phase=12 degrees, wet=95% through 6.6 kHz fading cubically to 90% at 15.6 kHz.\r\n"
			<< "This places deep nulls near 2.1, 6.6, and 11.1 kHz, then a softer null near 15.6 kHz.\r\n"
			<< "Per-partial magnitudes are precomputed; there is no runtime delay line for this filter. The\r\n"
			<< "15.3 kHz anomaly seen in the reference is not separately fitted by this comb family.\r\n\r\n"
			<< "Flat and comb variants are paired at 0% and 20% envelope-weighted chorus. Other settings\r\n"
			<< "match: 15% bed, tau=0.5 band steps, +6 dB comparison and held-key gains, 8 ms attack/release.\r\n"
			<< "This is a listening hypothesis, not a claim that the original synthesizer uses this circuit.\r\n\r\nFiles:\r\n";
		for (std::size_t i = 0; i < fileNames.size(); ++i)
			file << fileNames[i] << "\r\n";
		return file.good();
	}

	bool generateCombBedPreviews(const std::string& outputDirectory)
	{
		if (!ensureDirectory(outputDirectory))
			return false;
		std::vector<ComparisonRecord> events;
		if (!findOrderedTraceComparisons(events))
		{
			std::cerr << "The ordered input did not produce the expected 79 adjacent-key comparisons.\n";
			return false;
		}

		const TauProfile profile = { "tau_0_5", 0.5f, 0.5f };
		struct Variant
		{
			const char* name;
			bool comb;
			float chorusMix;
		};
		const Variant variants[] = {
			{ "flat_bed_0pct_chorus", false, 0.0f },
			{ "comb_bed_0pct_chorus", true, 0.0f },
			{ "flat_bed_20pct_chorus", false, 0.20f },
			{ "comb_bed_20pct_chorus", true, 0.20f }
		};
		std::vector<std::string> generatedFiles;
		generatedFiles.reserve(sizeof(variants) / sizeof(variants[0]));
		for (std::size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i)
		{
			SoundEngine::BedCombFilterParameters comb;
			comb.enabled = variants[i].comb;
			const std::string fileName = std::string("ordered_sweep_") + variants[i].name + ".wav";
			if (!writeComparisonClip(outputDirectory + "\\" + fileName, events, profile,
				variants[i].chorusMix, SoundEngine::ChorusWeighting::ComparisonEnvelope, 2, &comb))
			{
				std::cerr << "Failed to write comb-bed preview " << fileName << "\n";
				return false;
			}
			generatedFiles.push_back(fileName);
			std::cout << "Wrote " << outputDirectory << "\\" << fileName << "\n";
		}
		return writeCombBedManifest(outputDirectory, generatedFiles);
	}

	bool writeModelSweepManifest(const std::string& directory, const std::vector<std::string>& fileNames)
	{
		std::ofstream file((directory + "\\README.txt").c_str(), std::ios::binary | std::ios::out | std::ios::trunc);
		if (!file)
			return false;
		file << "Actually Good Sorting Sounds - Andrut model comparison previews\r\n"
			<< "Format: deterministic 44.1 kHz stereo, 16-bit PCM WAV; each clip is two seconds.\r\n"
			<< "Every file uses the same 79 adjacent comparisons from Insertion Sort on an ordered\r\n"
			<< "80-item input, evenly spread over the clip. Chorus is off to keep the comparisons dry.\r\n\r\n"
			<< "The matrix separates four hypotheses:\r\n"
			<< "- Complex comb phase rotates each bed partial by arg(H(f)); magnitude-only preserves\r\n"
			<< "  the 300 Hz harmonic phase alignment. Both use the same fitted comb magnitude.\r\n"
			<< "- Bed exponent p changes 1/k to 1/k^p without changing comparison tones.\r\n"
			<< "- Master 75 means a 25% amplitude reduction (master 0.375 from 0.5). Bed 75 means\r\n"
			<< "  bed level 0.1125; bed 25 means 0.0375. These are independent controls.\r\n"
			<< "- Position-scaled tau interpolates comparison spread from 0.1 bins at normalized value 0\r\n"
			<< "  to 0.5 bins at normalized value 1. Fixed tau clips keep the same width everywhere.\r\n\r\n"
			<< "The comb uses 222.222 us delay, 12 degree phase offset, 95% wet through 6.6 kHz, then\r\n"
			<< "a cubic fade to 90% at 15.6 kHz. It is still a hypothesis; the 15.3 kHz dip is not\r\n"
			<< "separately fitted. Existing engine defaults are unchanged pending listening review.\r\n\r\nFiles:\r\n";
		for (std::size_t i = 0; i < fileNames.size(); ++i)
			file << fileNames[i] << "\r\n";
		return file.good();
	}

	bool generateModelSweepPreviews(const std::string& outputDirectory)
	{
		if (!ensureDirectory(outputDirectory))
			return false;
		std::vector<ComparisonRecord> events;
		if (!findOrderedTraceComparisons(events))
		{
			std::cerr << "The ordered input did not produce the expected 79 adjacent-key comparisons.\n";
			return false;
		}

		struct Variant
		{
			const char* name;
			bool phaseAware;
			float exponent;
			float master;
			float bed;
			float tau;
			SoundEngine::ComparisonSpreadMode spread;
		};
		const Variant variants[] = {
			{ "magnitude_only_p1_master100_bed100_tau05", false, 1.0f, 0.50f, 0.1500f, 0.50f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_phase_p1_master100_bed100_tau05", true, 1.0f, 0.50f, 0.1500f, 0.50f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_phase_p15_master100_bed100_tau05", true, 1.5f, 0.50f, 0.1500f, 0.50f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_p15_master75_bed75_tau05", true, 1.5f, 0.375f, 0.1125f, 0.50f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_p15_master75_bed25_tau05", true, 1.5f, 0.375f, 0.0375f, 0.50f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_p15_master75_bed25_tau_position_01_to_05", true, 1.5f, 0.375f, 0.0375f, 0.50f, SoundEngine::ComparisonSpreadMode::PositionScaled },
			{ "complex_p2_master75_bed25_tau_position_01_to_05", true, 2.0f, 0.375f, 0.0375f, 0.50f, SoundEngine::ComparisonSpreadMode::PositionScaled },
			{ "complex_p15_master75_bed25_tau_fixed_01", true, 1.5f, 0.375f, 0.0375f, 0.10f, SoundEngine::ComparisonSpreadMode::FixedBins },
			{ "complex_p15_master75_bed25_tau_fixed_025", true, 1.5f, 0.375f, 0.0375f, 0.25f, SoundEngine::ComparisonSpreadMode::FixedBins }
		};
		std::vector<std::string> generatedFiles;
		generatedFiles.reserve(sizeof(variants) / sizeof(variants[0]));
		for (std::size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i)
		{
			const TauProfile profile = { "model", variants[i].tau, variants[i].tau };
			SoundEngine::BedCombFilterParameters comb;
			comb.enabled = true;
			comb.applyPhase = variants[i].phaseAware;
			const PreviewSettings settings = {
				variants[i].master, variants[i].bed, variants[i].exponent, variants[i].spread };
			const std::string fileName = std::string("ordered_sweep_") + variants[i].name + ".wav";
			if (!writeComparisonClip(outputDirectory + "\\" + fileName, events, profile, 0.0f,
				SoundEngine::ChorusWeighting::Uniform, 2, &comb, &settings))
			{
				std::cerr << "Failed to write model comparison preview " << fileName << "\n";
				return false;
			}
			generatedFiles.push_back(fileName);
			std::cout << "Wrote " << outputDirectory << "\\" << fileName << "\n";
		}
		return writeModelSweepManifest(outputDirectory, generatedFiles);
	}
}

int main(int argc, char** argv)
{
	if (argc > 1 && std::string(argv[1]) == "--model-sweep")
	{
		const std::string outputDirectory = argc > 2 ? argv[2] : "AudioPreviewModelComparisons";
		if (!generateModelSweepPreviews(outputDirectory))
			return 1;
		std::cout << "9 model-comparison previews are ready under " << outputDirectory << "\n";
		return 0;
	}
	if (argc > 1 && std::string(argv[1]) == "--comb-bed-sweep")
	{
		const std::string outputDirectory = argc > 2 ? argv[2] : "AudioPreviewCombBed";
		if (!generateCombBedPreviews(outputDirectory))
			return 1;
		std::cout << "4 comb-bed A/B previews are ready under " << outputDirectory << "\n";
		return 0;
	}
	if (argc > 1 && std::string(argv[1]) == "--ordered-sweep")
	{
		const std::string outputDirectory = argc > 2 ? argv[2] : "AudioPreviewOrderedSweepBed300";
		if (!generateOrderedSweepPreviews(outputDirectory))
			return 1;
		std::cout << "6 ordered-sweep chorus A/B previews are ready under " << outputDirectory << "\n";
		return 0;
	}

	const std::string outputDirectory = argc > 1 ? argv[1] : "AudioPreviewFast";
	if (!ensureDirectory(outputDirectory))
	{
		std::cerr << "Could not create preview folder: " << outputDirectory << "\n";
		return 1;
	}

	std::vector<ComparisonRecord> allTraceComparisons;
	if (!findTraceComparisons(allTraceComparisons))
	{
		std::cerr << "Could not build the deterministic Insertion Sort comparison trace.\n";
		return 1;
	}
	const PreviewPattern patterns[] = {
		{ "low_high_8changes", makeLowHighPattern(40) },
		{ "low_high_16changes", makeLowHighPattern(80) },
		{ "trace_8changes", sampleTraceComparisons(allTraceComparisons, 40) },
		{ "trace_16changes", sampleTraceComparisons(allTraceComparisons, 80) }
	};
	std::vector<std::string> generatedFiles;
	generatedFiles.reserve(4);
	for (std::size_t profileIndex = 0; profileIndex < sizeof(TauProfiles) / sizeof(TauProfiles[0]); ++profileIndex)
	{
		for (std::size_t patternIndex = 0; patternIndex < sizeof(patterns) / sizeof(patterns[0]); ++patternIndex)
		{
			const std::string fileName = std::string(TauProfiles[profileIndex].name) + "_" + patterns[patternIndex].name + ".wav";
			const std::string path = outputDirectory + "\\" + fileName;
			if (!writeComparisonClip(path, patterns[patternIndex].events, TauProfiles[profileIndex], 0.0f,
				SoundEngine::ChorusWeighting::Uniform, ClipSeconds))
			{
				std::cerr << "Failed to write " << path << "\n";
				return 1;
			}
			generatedFiles.push_back(fileName);
			std::cout << "Wrote " << path << "\n";
		}
	}

	if (!writeManifest(outputDirectory, generatedFiles))
	{
		std::cerr << "Could not write preview README.\n";
		return 1;
	}
	std::cout << "4 fast comparison previews are ready under " << outputDirectory << "\n";
	return 0;
}
