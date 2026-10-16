#include "ReconstructedPreviews.h"

#include "WavWriter.h"
#include "../SDL2BarGraphics/AudioPresentationBridge.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	const std::uint32_t SampleRate = 44100;
	const std::uint64_t ReleaseFrames = SampleRate / 2;
	const std::uint64_t ScheduleLeadFrames = 128;

	struct Clip
	{
		std::string stem;
		std::string scenarioGroup;
		std::string eventFixture;
		std::string label;
		std::string algorithm;
		std::string timing;
		std::size_t itemCount;
		std::uint32_t seed;
		std::uint64_t startFrame;
		std::uint64_t timelineDurationFrames;
		std::vector<float> samples;
		double dc;
		double acRms;
		double peak;
		Clip() : stem(), scenarioGroup(), eventFixture(), label(), algorithm(), timing(), itemCount(0), seed(0), startFrame(0), timelineDurationFrames(0), samples(), dc(0), acRms(0), peak(0) {}
	};

	struct Scenario
	{
		std::unique_ptr<sortinglogic::Simulation> simulation;
		std::unique_ptr<AudioPresentationBridge> bridge;
		LatestAudioStateMailbox mailbox;
		AudioPresentationState audio;
		SoundEngine::ReconstructedParameters reconstructed;
		SoundEngine::Parameters legacy;
		std::string error;
		Scenario() : simulation(), bridge(), mailbox(), audio(), reconstructed(), legacy(), error()
		{
			legacy.comparisonBoostDb = 0.0f;
			legacy.heldKeyBoostDb = 0.0f;
			legacy.roleBoostDb.fill(0.0f);
			legacy.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = 1.0f;
			reconstructed.masterLevel = 1.0;
		}
	};

	const char* algorithmName(sortinglogic::SortAlgorithm algorithm)
	{
		return sortinglogic::algorithmDisplayName(algorithm);
	}

	std::string shortAlgorithmName(sortinglogic::SortAlgorithm algorithm)
	{
		switch (algorithm)
		{
		case sortinglogic::SortAlgorithm::Selection: return "selection";
		case sortinglogic::SortAlgorithm::Insertion: return "insertion";
		case sortinglogic::SortAlgorithm::Gnome: return "gnome";
		case sortinglogic::SortAlgorithm::Merge: return "merge";
		case sortinglogic::SortAlgorithm::Quick: return "quick";
		case sortinglogic::SortAlgorithm::Counting: return "counting";
		case sortinglogic::SortAlgorithm::Radix: return "radix";
		default: return "algorithm";
		}
	}

	bool makeDirectories(const std::string& path)
	{
		if (path.empty()) return false;
		std::string prefix;
		for (std::size_t i = 0; i < path.size(); ++i)
		{
			prefix.push_back(path[i]);
			if (path[i] == '\\' || path[i] == '/')
			{
				if (prefix.size() > 1 && !(prefix.size() == 3 && prefix[1] == ':'))
				{
					const std::string directory = prefix.substr(0, prefix.size() - 1);
					if (!CreateDirectoryA(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
				}
			}
		}
		return CreateDirectoryA(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
	}

	bool prepareScenario(Scenario& scenario, sortinglogic::SortAlgorithm algorithm, std::size_t itemCount,
		std::uint32_t seed, float durationSeconds, SoundEngine::PlaybackTimingMode timing,
		sortinglogic::InputPattern pattern = sortinglogic::InputPattern::Random)
	{
		try
		{
			const bool integerInput = algorithm == sortinglogic::SortAlgorithm::Counting || algorithm == sortinglogic::SortAlgorithm::Radix;
			const sortinglogic::InputDomain domain = integerInput
				? sortinglogic::InputDomain::NonnegativeIntegerKeys : sortinglogic::InputDomain::NormalizedFloats;
			scenario.simulation.reset(new sortinglogic::Simulation(itemCount, seed, domain, pattern));
			if (!scenario.simulation->selectAlgorithm(algorithm).accepted ||
				!scenario.simulation->setDuration(durationSeconds).accepted)
				throw std::runtime_error("Could not configure the requested sorting scenario.");
			if (pattern != sortinglogic::InputPattern::AndrutReference &&
				!scenario.simulation->prepareShuffle(seed).accepted)
				throw std::runtime_error("Could not prepare the seeded scenario input.");

			sortinglogic::TraceBuildRequest request;
			if (!scenario.simulation->beginTraceBuild(request).accepted)
				throw std::runtime_error("Could not start trace construction.");
			sortinglogic::TraceBuildResult result;
			result.trace = sortinglogic::SortTraceBuilder::build(request);
			result.succeeded = result.trace.complete;
			result.status = result.trace.complete ? sortinglogic::TraceBuildStatus::Complete : sortinglogic::TraceBuildStatus::Partial;
			if (!result.succeeded)
				throw std::runtime_error("The deterministic sorting trace was partial: " + result.trace.completionReason);
			scenario.simulation->completeTraceBuild(std::move(result));
			scenario.bridge.reset(new AudioPresentationBridge());
			for (unsigned int attempt = 0; attempt < 30000; ++attempt)
			{
				scenario.bridge->update(*scenario.simulation, scenario.simulation->snapshot(), scenario.legacy,
					scenario.mailbox, scenario.reconstructed, timing, false);
				if (scenario.bridge->timelineReady())
				{
					if (!scenario.mailbox.consumeLatest(scenario.audio))
					{
						scenario.bridge->update(*scenario.simulation, scenario.simulation->snapshot(), scenario.legacy,
							scenario.mailbox, scenario.reconstructed, timing, false);
						scenario.mailbox.consumeLatest(scenario.audio);
					}
					if (scenario.audio.timeline && scenario.audio.timeline->ready()) return true;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			throw std::runtime_error(scenario.bridge->timelineError().empty()
				? "Audio timeline preparation timed out." : scenario.bridge->timelineError());
		}
		catch (const std::exception& error)
		{
			scenario.error = error.what();
			return false;
		}
	}

	bool renderClip(const Scenario& scenario, Clip& clip, std::uint64_t startFrame,
		std::uint64_t requestedSoundFrames, bool fullRun)
	{
		if (!scenario.audio.timeline || !scenario.audio.timeline->ready()) return false;
		SoundEngine::ReconstructedSynthesizer renderer;
		renderer.setParameters(scenario.reconstructed);
		renderer.setTimelineGainSettings(scenario.audio.timelineGains);
		renderer.setPreparedTimeline(scenario.audio.timeline, scenario.audio.timelineGeneration);
		SoundEngine::AudioTransportState transport = scenario.audio.transport;
		transport.timelineFrame = startFrame;
		transport.sourceCursor = scenario.audio.timeline->cursorAtFrame(startFrame);
		transport.generation = scenario.audio.timelineGeneration;
		transport.revision = (std::max<std::uint64_t>)(1, transport.revision + 1);
		transport.playing = true;
		transport.holdSelection = false;
		transport.releaseSelection = false;
		transport.stopAtEnd = false;
		if (startFrame == 0)
			renderer.setTransportState(transport);
		else
		{
			SoundEngine::PreparedAudioSeekState seek;
			if (!SoundEngine::ReconstructedSynthesizer::prepareSeekState(*scenario.audio.timeline,
				scenario.reconstructed, scenario.audio.timelineGains, transport, 1, seek, nullptr))
				return false;
			renderer.installPreparedSeekState(seek);
		}

		const std::uint64_t totalFrames = (fullRun ? ScheduleLeadFrames : 0) + requestedSoundFrames + ReleaseFrames;
		clip.samples.resize(static_cast<std::size_t>(totalFrames) * 2);
		if (fullRun)
			renderer.render(clip.samples.data(), static_cast<std::size_t>(totalFrames));
		else
		{
			const std::size_t activeFrames = static_cast<std::size_t>(requestedSoundFrames);
			renderer.render(clip.samples.data(), activeFrames);
			renderer.stopBirths();
			renderer.render(clip.samples.data() + activeFrames * 2,
				static_cast<std::size_t>(ReleaseFrames));
		}
		clip.startFrame = startFrame;
		clip.timelineDurationFrames = scenario.audio.timeline->durationFrames();
		double sum = 0.0;
		for (std::size_t i = 0; i < clip.samples.size(); ++i) sum += clip.samples[i];
		clip.dc = clip.samples.empty() ? 0.0 : sum / clip.samples.size();
		double square = 0.0, acSquare = 0.0;
		for (std::size_t i = 0; i < clip.samples.size(); ++i)
		{
			const double value = clip.samples[i];
			clip.peak = (std::max)(clip.peak, std::abs(value));
			square += value * value;
			const double ac = value - clip.dc;
			acSquare += ac * ac;
		}
		clip.acRms = clip.samples.empty() ? 0.0 : std::sqrt(acSquare / clip.samples.size());
		return true;
	}

	void appendClip(std::vector<Clip>& clips, const Scenario& scenario, sortinglogic::SortAlgorithm algorithm,
		std::size_t itemCount, std::uint32_t seed, const std::string& timingName, const std::string& stem,
		const std::string& scenarioGroup, const std::string& eventFixture, const std::string& label,
		std::uint64_t startFrame, std::uint64_t activeFrames, bool fullRun)
	{
		Clip clip;
		clip.stem = stem;
		clip.scenarioGroup = scenarioGroup;
		clip.eventFixture = eventFixture;
		clip.label = label;
		clip.algorithm = algorithmName(algorithm);
		clip.timing = timingName;
		clip.itemCount = itemCount;
		clip.seed = seed;
		if (!renderClip(scenario, clip, startFrame, activeFrames, fullRun))
			throw std::runtime_error("Could not render preview " + stem + ".");
		clips.push_back(std::move(clip));
	}

	void writeTimelineEvents(const std::string& path, const SoundEngine::PreparedAudioTimeline& timeline);

	void appendScenarioClips(std::vector<Clip>& clips, sortinglogic::SortAlgorithm algorithm,
		std::size_t itemCount, std::uint32_t seed, SoundEngine::PlaybackTimingMode timing,
		const std::string& timingName, float fixedDuration, const std::string& outputDirectory)
	{
		Scenario scenario;
		if (!prepareScenario(scenario, algorithm, itemCount, seed, fixedDuration, timing))
			throw std::runtime_error(std::string("Could not prepare ") + algorithmName(algorithm) +
				" preview: " + scenario.error);
		const std::uint64_t duration = scenario.audio.timeline->durationFrames();
		const std::string prefix = shortAlgorithmName(algorithm) + "_" + timingName + "_" +
			std::to_string(itemCount) + "items_seed" + std::to_string(seed);
		const std::string eventFixture = prefix + "_events.json";
		writeTimelineEvents(outputDirectory + "\\" + eventFixture, *scenario.audio.timeline);
		if (duration > 8ull * SampleRate)
		{
			const std::uint64_t active = 2ull * SampleRate;
			const std::uint64_t starts[] = { 0, duration / 2 > SampleRate ? duration / 2 - SampleRate : 0,
				duration > active ? duration - active : 0 };
			const char* labels[] = { "beginning", "middle", "ending" };
			for (std::size_t i = 0; i < 3; ++i)
				appendClip(clips, scenario, algorithm, itemCount, seed, timingName,
					prefix + "_" + labels[i], prefix, eventFixture,
					std::string(labels[i]) + " excerpt, 2.0 s plus 0.5 s release",
					starts[i], active, starts[i] != 0);
		}
		else
			appendClip(clips, scenario, algorithm, itemCount, seed, timingName, prefix + "_full", prefix, eventFixture,
				"Full run plus 0.5 s release", 0, duration, true);
	}

	void writeTimelineEvents(const std::string& path, const SoundEngine::PreparedAudioTimeline& timeline)
	{
		std::ofstream output(path.c_str(), std::ios::trunc);
		output << "{\n  \"sample_rate\": " << timeline.sampleRate() << ",\n  \"duration_frames\": " << timeline.durationFrames()
			<< ",\n  \"events\": [\n";
		const std::vector<SoundEngine::AudioTimelineEvent>& events = timeline.events();
		for (std::size_t i = 0; i < events.size(); ++i)
		{
			const SoundEngine::AudioTimelineEvent& event = events[i];
			output << "    {\"frame\": " << event.frame << ", \"cursor\": " << event.sourceCursor
				<< ", \"kind\": " << static_cast<unsigned int>(event.kind) << ", \"first\": "
				<< std::setprecision(17) << event.firstPosition << ", \"second\": " << event.secondPosition
				<< ", \"first_role\": " << static_cast<unsigned int>(event.firstRole)
				<< ", \"second_role\": " << static_cast<unsigned int>(event.secondRole) << "}";
			if (i + 1 != events.size()) output << ',';
			output << '\n';
		}
		output << "  ]\n}\n";
	}

	void writeClipManifest(std::ofstream& output, const Clip& clip, double listeningGain)
	{
		output << "    {\"stem\": \"" << clip.stem << "\", \"scenario_group\": \"" << clip.scenarioGroup
			<< "\", \"event_fixture\": \"" << clip.eventFixture << "\", \"label\": \"" << clip.label
			<< "\", \"algorithm\": \"" << clip.algorithm << "\", \"item_count\": " << clip.itemCount
			<< ", \"seed\": " << clip.seed << ", \"timing_mode\": \"" << clip.timing
			<< "\", \"start_frame\": " << clip.startFrame << ", \"timeline_duration_frames\": "
			<< clip.timelineDurationFrames << ", \"frames\": " << clip.samples.size() / 2
			<< ", \"sample_rate\": " << SampleRate << ", \"dc\": " << std::setprecision(10) << clip.dc
			<< ", \"ac_rms\": " << clip.acRms << ", \"peak\": " << clip.peak
			<< ", \"listening_gain\": " << listeningGain
			<< ", \"raw_float32\": \"" << clip.stem << "_raw_float32.wav\", \"listening_pcm16\": \""
			<< clip.stem << "_listen_pcm16.wav\"}";
	}
}

bool generateReconstructedListeningPack(const std::string& outputDirectory)
{
	if (!makeDirectories(outputDirectory))
	{
		std::cerr << "Could not create reconstructed listening folder: " << outputDirectory << "\n";
		return false;
	}
	try
	{
		std::vector<Clip> clips;
		const sortinglogic::SortAlgorithm referenceAlgorithms[] = {
			sortinglogic::SortAlgorithm::Selection,
			sortinglogic::SortAlgorithm::Insertion,
			sortinglogic::SortAlgorithm::Gnome
		};
		for (std::size_t i = 0; i < 3; ++i)
		{
			const std::uint32_t seed = 0xA4D20000u + static_cast<std::uint32_t>(i);
			Scenario scenario;
			if (!prepareScenario(scenario, referenceAlgorithms[i], 100, seed,
				10.0f, SoundEngine::PlaybackTimingMode::ReferenceCadence, sortinglogic::InputPattern::AndrutReference))
				throw std::runtime_error(std::string("Could not prepare frozen reference rerender: ") + scenario.error);
			const std::uint64_t active = scenario.audio.timeline->durationFrames();
			const std::string stem = "reference_" + shortAlgorithmName(referenceAlgorithms[i]) + "_andrut100";
			const std::string eventFixture = stem + "_events.json";
			writeTimelineEvents(outputDirectory + "\\" + eventFixture, *scenario.audio.timeline);
			appendClip(clips, scenario, referenceAlgorithms[i], 100, seed, "reference_cadence", stem,
				"frozen_reference_" + shortAlgorithmName(referenceAlgorithms[i]), eventFixture,
				"Full 100-item Andrut reference cadence rerender", 0, active, true);
		}

		const sortinglogic::SortAlgorithm primary[] = {
			sortinglogic::SortAlgorithm::Selection,
			sortinglogic::SortAlgorithm::Insertion,
			sortinglogic::SortAlgorithm::Gnome
		};
		for (std::size_t a = 0; a < 3; ++a)
		{
			for (std::size_t sizeIndex = 0; sizeIndex < 3; ++sizeIndex)
			{
				const std::size_t count = sizeIndex == 0 ? 20 : sizeIndex == 1 ? 80 : 800;
				const std::uint32_t seed = 0x4A475300u + static_cast<std::uint32_t>(a * 10000 + count);
				appendScenarioClips(clips, primary[a], count, seed,
					SoundEngine::PlaybackTimingMode::ReferenceCadence, "reference_cadence", 10.0f, outputDirectory);
			}
			for (std::size_t durationIndex = 0; durationIndex < 3; ++durationIndex)
			{
				const float duration = durationIndex == 0 ? 2.0f : durationIndex == 1 ? 10.0f : 30.0f;
				const std::uint32_t seed = 0xFA570000u + static_cast<std::uint32_t>(a * 100 + durationIndex);
				appendScenarioClips(clips, primary[a], 80, seed,
					SoundEngine::PlaybackTimingMode::FixedDuration, "fixed_" + std::to_string(static_cast<int>(duration)) + "s", duration, outputDirectory);
			}
		}
		const sortinglogic::SortAlgorithm additional[] = {
			sortinglogic::SortAlgorithm::Merge,
			sortinglogic::SortAlgorithm::Quick,
			sortinglogic::SortAlgorithm::Counting,
			sortinglogic::SortAlgorithm::Radix
		};
		for (std::size_t i = 0; i < 4; ++i)
			appendScenarioClips(clips, additional[i], 80, 0x80A40000u + static_cast<std::uint32_t>(i),
				SoundEngine::PlaybackTimingMode::FixedDuration, "fixed_10s", 10.0f, outputDirectory);

		double maximumPeak = 0.0;
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			maximumPeak = (std::max)(maximumPeak, clips[i].peak);
			if (!std::isfinite(clips[i].peak) || clips[i].peak > 1.0)
				throw std::runtime_error("A raw reference-profile listening render exceeded full scale.");
		}
		const double listeningGain = maximumPeak > 0.891250938 ? 0.891250938 / maximumPeak : 1.0;
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			const std::string rawPath = outputDirectory + "\\" + clips[i].stem + "_raw_float32.wav";
			const std::string listenPath = outputDirectory + "\\" + clips[i].stem + "_listen_pcm16.wav";
			if (!writeWavFloat32Stereo(rawPath, clips[i].samples, SampleRate))
				throw std::runtime_error("Could not write raw listening render " + rawPath + ".");
			std::vector<float> listen(clips[i].samples);
			for (std::size_t sample = 0; sample < listen.size(); ++sample)
				listen[sample] = static_cast<float>(listen[sample] * listeningGain);
			if (!writeWav16Stereo(listenPath, listen, SampleRate))
				throw std::runtime_error("Could not write PCM16 listening copy " + listenPath + ".");
			std::cout << "Wrote " << clips[i].stem << "\n";
		}

		std::ofstream manifest((outputDirectory + "\\manifest.json").c_str(), std::ios::trunc);
		if (!manifest) throw std::runtime_error("Could not write the listening manifest.");
		manifest << "{\n  \"profile\": \"andrut_delayed_sine_bursts_v1\",\n  \"sample_rate\": " << SampleRate
			<< ",\n  \"listening_gain\": " << std::setprecision(12) << listeningGain
			<< ",\n  \"maximum_raw_peak\": " << maximumPeak
			<< ",\n  \"parameters\": {\"minimum_pitch_hz\": 104.0322699094767, \"maximum_pitch_hz\": 4054.6605862015895, \"mapping\": \"linear_hz\", \"excitation_clock_hz\": 300.0, \"carrier_cycles\": 9.302650315454065, \"tukey_fraction\": 0.025666772580559438, \"second_operand_delay_microseconds\": 225.66891905936825, \"master_level\": 1.0, \"algorithm_role_gains_db\": 0.0, \"tone_filter_enabled\": false, \"output_filter\": \"frozen 31-tap reference FIR\", \"chorus\": false},"
			<< "\n  \"reference_rerenders\": 3,\n  \"listening_matrix_groups\": 22,\n  \"total_scenario_groups\": 25,\n  \"clips\": [\n";
		for (std::size_t i = 0; i < clips.size(); ++i)
		{
			writeClipManifest(manifest, clips[i], listeningGain);
			manifest << (i + 1 == clips.size() ? "\n" : ",\n");
		}
		manifest << "  ]\n}\n";
		manifest.close();
		if (!manifest) throw std::runtime_error("The listening manifest could not be finalized.");

		std::ofstream index((outputDirectory + "\\README.md").c_str(), std::ios::trunc);
		if (!index) throw std::runtime_error("Could not write the listening index.");
		index << "# Reconstructed audio listening pack\n\n"
			<< "The application profile is the frozen delayed sine-burst reconstruction. The three full reference rerenders use the exact 100-item Andrut arrangement and reference cadence. The listening matrix has 22 scenario groups; long runs are represented by labelled two-second beginning, middle, and ending excerpts followed by a half-second natural release.\n\n"
			<< "Raw float32 exports retain calibrated model output. PCM16 copies use one shared listening gain of " << std::fixed << std::setprecision(6)
			<< listeningGain << " (" << 20.0 * std::log10((std::max)(listeningGain, 1e-12)) << " dB) so the pack stays below -1 dBFS when needed.\n\n"
			<< "See [manifest.json](manifest.json) for seeds, settings, event schedules, cursor range, duration, DC, AC RMS, peak, and applied gain. The per-scenario event JSON files keep event frames and ordered operands separate from DSP. The independent frozen-renderer check is recorded in [reference_parity.json](reference_parity.json).\n\n"
			<< "## Full reference rerenders\n\n";
		for (std::size_t i = 0; i < 3; ++i)
		{
			const std::string stem = "reference_" + shortAlgorithmName(referenceAlgorithms[i]) + "_andrut100";
			index << "- " << algorithmName(referenceAlgorithms[i]) << ": [float32](" << stem
				<< "_raw_float32.wav) · [PCM16](" << stem << "_listen_pcm16.wav) · [events](" << stem << "_events.json)\n";
		}
		index << "\n## Listening matrix\n\n";
		for (std::size_t i = 3; i < clips.size(); ++i)
			index << "- " << clips[i].label << " — " << clips[i].algorithm << ", " << clips[i].itemCount
				<< " items, " << clips[i].timing << ": [listen](" << clips[i].stem << "_listen_pcm16.wav) · [raw]("
				<< clips[i].stem << "_raw_float32.wav)\n";
		index.close();
		if (!index) throw std::runtime_error("The listening index could not be finalized.");
		std::cout << "Generated " << clips.size() << " paired audio clips for 22 scenario groups in " << outputDirectory << "\n";
		return true;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Reconstructed listening pack failed: " << error.what() << "\n";
		return false;
	}
}
