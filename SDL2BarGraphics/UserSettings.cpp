#include "UserSettings.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>

namespace
{
	const int CurrentVersion = 8;
	const wchar_t* SettingsDirectory = L"\\ActuallyGoodSortingSounds";
	const wchar_t* SettingsFile = L"\\settings.ini";
	const char* audioChannelId(AlgorithmAudioChannel channel);

	std::wstring settingsPath()
	{
		DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
		if (size == 0) return std::wstring();
		std::wstring base(size, L'\0');
		const DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", &base[0], size);
		if (copied == 0 || copied >= size) return std::wstring();
		base.resize(copied);
		return base + SettingsDirectory + SettingsFile;
	}

	std::wstring directoryOf(const std::wstring& path)
	{
		const std::wstring::size_type split = path.find_last_of(L"\\/");
		return split == std::wstring::npos ? std::wstring() : path.substr(0, split);
	}

	std::string paletteName(PaletteId id)
	{
		switch (id)
		{
		case PaletteId::Cyan: return "cyan";
		case PaletteId::Amber: return "amber";
		case PaletteId::Monochrome: return "monochrome";
		case PaletteId::Light: return "light";
		case PaletteId::VgaClassic: return "vga-classic";
		case PaletteId::Windows95: return "windows-95";
		}
		return "cyan";
	}

	bool parsePalette(const std::string& value, PaletteId& result)
	{
		if (value == "cyan") result = PaletteId::Cyan;
		else if (value == "amber") result = PaletteId::Amber;
		else if (value == "monochrome") result = PaletteId::Monochrome;
		else if (value == "light") result = PaletteId::Light;
		else if (value == "vga-classic") result = PaletteId::VgaClassic;
		else if (value == "windows-95") result = PaletteId::Windows95;
		else return false;
		return true;
	}

	void readAppearance(const std::map<std::string, std::string>& values, int version, UserPreferences& preferences)
	{
		preferences.appearance = UiAppearancePreferences();
		preferences.appearance.themes[UiPresetId::Current] = defaultUiThemeCustomization(UiPresetId::Current, preferences.visual.palette);
		preferences.appearance.themes[UiPresetId::FlatVgaDos] = defaultUiThemeCustomization(UiPresetId::FlatVgaDos, preferences.visual.palette);
		preferences.appearance.themes[UiPresetId::ClassicWindows95] = defaultUiThemeCustomization(UiPresetId::ClassicWindows95);
		if (version < 7) return;

		std::map<std::string, std::string>::const_iterator found = values.find("ui_active_preset");
		if (found != values.end() && !parseUiPresetId(found->second, preferences.appearance.activePreset))
			preferences.appearance.activePreset = UiPresetId::Current;

		const std::vector<UiPresetId>& presets = uiPresetCatalog();
		for (std::size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
		{
			const UiPresetId preset = presets[presetIndex];
			const std::string base = "ui." + uiPresetStableId(preset);
			UiThemeCustomization candidate = defaultUiThemeCustomization(preset,
				preset == UiPresetId::ClassicWindows95 ? PaletteId::Windows95 : preferences.visual.palette);
			bool valid = true;
			found = values.find(base + ".palette");
			if (found != values.end() && !parsePalette(found->second, candidate.palette)) valid = false;
			const std::string overridePrefix = base + ".override.";
			for (std::map<std::string, std::string>::const_iterator item = values.begin(); item != values.end(); ++item)
			{
				if (item->first.compare(0, overridePrefix.size(), overridePrefix) != 0) continue;
				const std::string tokenId = item->first.substr(overridePrefix.size());
				const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), tokenId);
				if (!descriptor) continue;
				UiTokenValue parsed;
				if (!decodeUiTokenValue(item->second, descriptor->type, parsed) ||
					!setUiTokenValue(preset, candidate, tokenId, parsed))
					valid = false;
			}
			if (!valid || !validateUiThemeCustomization(preset, candidate))
				candidate = defaultUiThemeCustomization(preset,
					preset == UiPresetId::ClassicWindows95 ? PaletteId::Windows95 : preferences.visual.palette);
			preferences.appearance.themes[preset] = candidate;
		}
		preferences.visual.palette = preferences.appearance.themes[UiPresetId::Current].palette;
	}

	std::string spacingName(BarSpacingMode mode)
	{
		switch (mode)
		{
		case BarSpacingMode::None: return "none";
		case BarSpacingMode::Compact: return "compact";
		case BarSpacingMode::Comfortable: return "comfortable";
		}
		return "compact";
	}

	bool parseSpacing(const std::string& value, BarSpacingMode& result)
	{
		if (value == "none") result = BarSpacingMode::None;
		else if (value == "compact") result = BarSpacingMode::Compact;
		else if (value == "comfortable") result = BarSpacingMode::Comfortable;
		else return false;
		return true;
	}

	const char* profileName(sortinglogic::InputProfile profile)
	{
		return profile == sortinglogic::InputProfile::IntegerKeys ? "integer_keys" : "normalized_float";
	}

	bool parseProfile(const std::string& value, sortinglogic::InputProfile& result)
	{
		if (value == "normalized_float") result = sortinglogic::InputProfile::NormalizedFloat;
		else if (value == "integer_keys") result = sortinglogic::InputProfile::IntegerKeys;
		else return false;
		return true;
	}

	const char* domainName(sortinglogic::InputDomain domain)
	{
		switch (domain)
		{
		case sortinglogic::InputDomain::NormalizedFloats: return "normalized_floats";
		case sortinglogic::InputDomain::NonnegativeIntegerKeys: return "nonnegative_integer_keys";
		case sortinglogic::InputDomain::SignedIntegerKeys: return "signed_integer_keys";
		case sortinglogic::InputDomain::IntegerPermutation: return "integer_permutation";
		}
		return "normalized_floats";
	}

	bool parseDomain(const std::string& value, sortinglogic::InputDomain& result)
	{
		if (value == "normalized_floats") result = sortinglogic::InputDomain::NormalizedFloats;
		else if (value == "nonnegative_integer_keys") result = sortinglogic::InputDomain::NonnegativeIntegerKeys;
		else if (value == "signed_integer_keys") result = sortinglogic::InputDomain::SignedIntegerKeys;
		else if (value == "integer_permutation") result = sortinglogic::InputDomain::IntegerPermutation;
		else return false;
		return true;
	}

	const char* patternName(sortinglogic::InputPattern pattern)
	{
		switch (pattern)
		{
		case sortinglogic::InputPattern::Random: return "random";
		case sortinglogic::InputPattern::Sorted: return "sorted";
		case sortinglogic::InputPattern::Reversed: return "reversed";
		case sortinglogic::InputPattern::NearlySorted: return "nearly_sorted";
		case sortinglogic::InputPattern::FewUnique: return "few_unique";
		case sortinglogic::InputPattern::OrganPipe: return "organ_pipe";
		case sortinglogic::InputPattern::AndrutReference: return "andrut_reference_100";
		}
		return "random";
	}

	bool parsePattern(const std::string& value, sortinglogic::InputPattern& result)
	{
		if (value == "random") result = sortinglogic::InputPattern::Random;
		else if (value == "sorted") result = sortinglogic::InputPattern::Sorted;
		else if (value == "reversed") result = sortinglogic::InputPattern::Reversed;
		else if (value == "nearly_sorted") result = sortinglogic::InputPattern::NearlySorted;
		else if (value == "few_unique") result = sortinglogic::InputPattern::FewUnique;
		else if (value == "organ_pipe") result = sortinglogic::InputPattern::OrganPipe;
		else if (value == "andrut_reference_100") result = sortinglogic::InputPattern::AndrutReference;
		else return false;
		return true;
	}

	const char* gapSequenceName(sortinglogic::ShellGapSequence sequence)
	{
		switch (sequence)
		{
		case sortinglogic::ShellGapSequence::Halving: return "halving";
		case sortinglogic::ShellGapSequence::Knuth: return "knuth";
		case sortinglogic::ShellGapSequence::Ciura: return "ciura";
		case sortinglogic::ShellGapSequence::Tokuda: return "tokuda";
		}
		return "halving";
	}

	bool parseGapSequence(const std::string& value, sortinglogic::ShellGapSequence& result)
	{
		if (value == "halving") result = sortinglogic::ShellGapSequence::Halving;
		else if (value == "knuth") result = sortinglogic::ShellGapSequence::Knuth;
		else if (value == "ciura") result = sortinglogic::ShellGapSequence::Ciura;
		else if (value == "tokuda") result = sortinglogic::ShellGapSequence::Tokuda;
		else return false;
		return true;
	}

	const char* mergeWritebackModeName(sortinglogic::MergeWritebackMode mode)
	{
		return mode == sortinglogic::MergeWritebackMode::AnimatedWrites ? "animated_writes" : "atomic_range_write";
	}

	bool parseMergeWritebackMode(const std::string& value, sortinglogic::MergeWritebackMode& result)
	{
		if (value == "animated_writes") result = sortinglogic::MergeWritebackMode::AnimatedWrites;
		else if (value == "atomic_range_write") result = sortinglogic::MergeWritebackMode::AtomicRangeWrite;
		else return false;
		return true;
	}

	const char* mergeExecutionModeName(sortinglogic::MergeExecutionMode mode)
	{
		return mode == sortinglogic::MergeExecutionMode::TopDown ? "top_down" : "bottom_up";
	}

	bool parseMergeExecutionMode(const std::string& value, sortinglogic::MergeExecutionMode& result)
	{
		if (value == "bottom_up") result = sortinglogic::MergeExecutionMode::BottomUp;
		else if (value == "top_down") result = sortinglogic::MergeExecutionMode::TopDown;
		else return false;
		return true;
	}

	const char* playbackTimingModeName(SoundEngine::PlaybackTimingMode mode)
	{
		return mode == SoundEngine::PlaybackTimingMode::ReferenceCadence ? "reference_cadence" : "fixed_duration";
	}

	SoundEngine::PlaybackTimingMode parsePlaybackTimingMode(const std::string& value)
	{
		return value == "reference_cadence" ? SoundEngine::PlaybackTimingMode::ReferenceCadence :
			SoundEngine::PlaybackTimingMode::FixedDuration;
	}

	bool parseInt(const std::map<std::string, std::string>& values, const std::string& key, int& output)
	{
		std::map<std::string, std::string>::const_iterator found = values.find(key);
		if (found == values.end() || found->second.empty()) return false;
		char* end = nullptr;
		const long parsed = std::strtol(found->second.c_str(), &end, 10);
		if (!end || *end != '\0' || parsed < -2147483647L || parsed > 2147483647L) return false;
		output = static_cast<int>(parsed);
		return true;
	}

	bool parseFloat(const std::map<std::string, std::string>& values, const std::string& key, float& output)
	{
		std::map<std::string, std::string>::const_iterator found = values.find(key);
		if (found == values.end() || found->second.empty()) return false;
		char* end = nullptr;
		const float parsed = std::strtof(found->second.c_str(), &end);
		if (!end || *end != '\0' || !std::isfinite(parsed)) return false;
		output = parsed;
		return true;
	}

	bool parseDouble(const std::map<std::string, std::string>& values, const std::string& key, double& output)
	{
		std::map<std::string, std::string>::const_iterator found = values.find(key);
		if (found == values.end() || found->second.empty()) return false;
		char* end = nullptr;
		const double parsed = std::strtod(found->second.c_str(), &end);
		if (!end || *end != '\0' || !std::isfinite(parsed)) return false;
		output = parsed;
		return true;
	}

	bool parseBool(const std::map<std::string, std::string>& values, const std::string& key, bool& output)
	{
		std::map<std::string, std::string>::const_iterator found = values.find(key);
		if (found == values.end()) return false;
		if (found->second == "1" || found->second == "true") output = true;
		else if (found->second == "0" || found->second == "false") output = false;
		else return false;
		return true;
	}

	bool optionalBool(const std::map<std::string, std::string>& values, const std::string& key, bool& field)
	{
		return values.find(key) == values.end() || parseBool(values, key, field);
	}

	bool optionalFloat(const std::map<std::string, std::string>& values, const std::string& key, float& field)
	{
		return values.find(key) == values.end() || parseFloat(values, key, field);
	}

	bool optionalDouble(const std::map<std::string, std::string>& values, const std::string& key, double& field)
	{
		return values.find(key) == values.end() || parseDouble(values, key, field);
	}

	bool optionalInt(const std::map<std::string, std::string>& values, const std::string& key, int& field)
	{
		return values.find(key) == values.end() || parseInt(values, key, field);
	}

	bool readAudio(const std::map<std::string, std::string>& values, SoundEngine::Parameters& a)
	{
		bool ok = true;
		int integer = 0;
#define READ_AF(key, field) ok = optionalFloat(values, "audio_" key, a.field) && ok
#define READ_AB(key, field) ok = optionalBool(values, "audio_" key, a.field) && ok
#define READ_AI(key, field) integer = static_cast<int>(a.field); if (optionalInt(values, "audio_" key, integer)) a.field = static_cast<decltype(a.field)>(integer); else if (values.find("audio_" key) != values.end()) ok = false
		READ_AF("master", masterLevel);
		READ_AF("bed", bedLevel);
		READ_AF("harmonic_exponent", bedHarmonicExponent);
		READ_AB("sub300_bed", includeSub300HzBed);
		READ_AB("comb_enabled", bedComb.enabled);
		READ_AB("comb_phase", bedComb.applyPhase);
		READ_AF("comb_delay_us", bedComb.delayMicroseconds);
		READ_AF("comb_phase_offset_degrees", bedComb.phaseOffsetDegrees);
		READ_AF("comb_wet_low", bedComb.wetAtLowFrequency);
		READ_AF("comb_wet_high", bedComb.wetAtHighFrequency);
		READ_AF("comb_fade_start_hz", bedComb.fadeStartHz);
		READ_AF("comb_fade_end_hz", bedComb.fadeEndHz);
		READ_AF("comb_exponent", bedComb.fadeExponent);
		READ_AF("comparison_boost_db", comparisonBoostDb);
		READ_AF("held_key_boost_db", heldKeyBoostDb);
		READ_AF("tau_below", tauBelowBins);
		READ_AF("tau_above", tauAboveBins);
		READ_AF("chorus_mix", chorusMix);
		READ_AF("chorus_rate_hz", chorusRateHz);
		READ_AF("chorus_depth_ms", chorusDepthMs);
		READ_AI("chorus_weighting", chorusWeighting);
		READ_AF("attack_ms", attackMs);
		READ_AF("release_ms", releaseMs);
		READ_AI("mapping_mode", mappingMode);
		READ_AI("spread_mode", comparisonSpreadMode);
		READ_AB("enabled", enabled);
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
		{
			std::ostringstream key;
			key << "audio_band_trim_" << std::setw(2) << std::setfill('0') << i;
			ok = optionalFloat(values, key.str(), a.bandTrimDb[i]) && ok;
		}
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
		{
			std::ostringstream key;
			key << "audio_role_gain_" << std::setw(2) << std::setfill('0') << i;
			ok = optionalFloat(values, key.str(), a.roleBoostDb[i]) && ok;
		}
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
		{
			std::ostringstream routeKey, gainKey;
			routeKey << "audio_accent_route_" << std::setw(2) << std::setfill('0') << i;
			gainKey << "audio_accent_gain_" << std::setw(2) << std::setfill('0') << i;
			ok = optionalBool(values, routeKey.str(), a.accentRouting[i]) && ok;
			ok = optionalFloat(values, gainKey.str(), a.accentGain[i]) && ok;
		}
#undef READ_AF
#undef READ_AB
#undef READ_AI
		return ok;
	}

	bool validReconstructedAudio(const SoundEngine::ReconstructedParameters& a)
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

	bool readReconstructedAudio(const std::map<std::string, std::string>& values, SoundEngine::ReconstructedParameters& a)
	{
		bool ok = true;
		int integer = 0;
#define READ_RF(key, field) ok = optionalDouble(values, "reconstructed_" key, a.field) && ok
#define READ_RB(key, field) ok = optionalBool(values, "reconstructed_" key, a.field) && ok
#define READ_RI(key, field) integer = static_cast<int>(a.field); if (optionalInt(values, "reconstructed_" key, integer)) a.field = static_cast<decltype(a.field)>(integer); else if (values.find("reconstructed_" key) != values.end()) ok = false
		READ_RF("minimum_hz", minimumFrequencyHz);
		READ_RF("maximum_hz", maximumFrequencyHz);
		READ_RF("clock_hz", excitationClockHz);
		READ_RF("carrier_cycles", carrierCycles);
		READ_RF("tukey_fraction", tukeyFraction);
		READ_RF("operand_delay_us", secondOperandDelayMicroseconds);
		READ_RF("master", masterLevel);
		READ_RF("special_release_ms", specialReleaseMilliseconds);
		READ_RF("lowpass_cutoff_hz", lowPassCutoffHz);
		READ_RB("enabled", audioEnabled);
		READ_RB("lowpass_enabled", lowPassEnabled);
		READ_RI("mapping", mapping);
		READ_RI("idle_policy", idlePolicy);
#undef READ_RF
#undef READ_RB
#undef READ_RI
		return ok;
	}

	void writeReconstructedAudio(std::ostringstream& out, const SoundEngine::ReconstructedParameters& a)
	{
		out << std::setprecision(17)
			<< "reconstructed_minimum_hz=" << a.minimumFrequencyHz << "\n"
			<< "reconstructed_maximum_hz=" << a.maximumFrequencyHz << "\n"
			<< "reconstructed_clock_hz=" << a.excitationClockHz << "\n"
			<< "reconstructed_carrier_cycles=" << a.carrierCycles << "\n"
			<< "reconstructed_tukey_fraction=" << a.tukeyFraction << "\n"
			<< "reconstructed_operand_delay_us=" << a.secondOperandDelayMicroseconds << "\n"
			<< "reconstructed_master=" << a.masterLevel << "\n"
			<< "reconstructed_special_release_ms=" << a.specialReleaseMilliseconds << "\n"
			<< "reconstructed_lowpass_cutoff_hz=" << a.lowPassCutoffHz << "\n"
			<< "reconstructed_enabled=" << (a.audioEnabled ? 1 : 0) << "\n"
			<< "reconstructed_lowpass_enabled=" << (a.lowPassEnabled ? 1 : 0) << "\n"
			<< "reconstructed_mapping=" << static_cast<int>(a.mapping) << "\n"
			<< "reconstructed_idle_policy=" << static_cast<int>(a.idlePolicy) << "\n";
	}

	void writeAudio(std::ostringstream& out, const SoundEngine::Parameters& a)
	{
		out << std::setprecision(9);
#define WRITE_F(key, field) out << "audio_" key "=" << a.field << "\n"
#define WRITE_B(key, field) out << "audio_" key "=" << (a.field ? 1 : 0) << "\n"
#define WRITE_I(key, field) out << "audio_" key "=" << static_cast<int>(a.field) << "\n"
		WRITE_F("master", masterLevel);
		WRITE_F("bed", bedLevel);
		WRITE_F("harmonic_exponent", bedHarmonicExponent);
		WRITE_B("sub300_bed", includeSub300HzBed);
		WRITE_B("comb_enabled", bedComb.enabled);
		WRITE_B("comb_phase", bedComb.applyPhase);
		WRITE_F("comb_delay_us", bedComb.delayMicroseconds);
		WRITE_F("comb_phase_offset_degrees", bedComb.phaseOffsetDegrees);
		WRITE_F("comb_wet_low", bedComb.wetAtLowFrequency);
		WRITE_F("comb_wet_high", bedComb.wetAtHighFrequency);
		WRITE_F("comb_fade_start_hz", bedComb.fadeStartHz);
		WRITE_F("comb_fade_end_hz", bedComb.fadeEndHz);
		WRITE_F("comb_exponent", bedComb.fadeExponent);
		WRITE_F("comparison_boost_db", comparisonBoostDb);
		WRITE_F("held_key_boost_db", heldKeyBoostDb);
		WRITE_F("tau_below", tauBelowBins);
		WRITE_F("tau_above", tauAboveBins);
		WRITE_F("chorus_mix", chorusMix);
		WRITE_F("chorus_rate_hz", chorusRateHz);
		WRITE_F("chorus_depth_ms", chorusDepthMs);
		WRITE_I("chorus_weighting", chorusWeighting);
		WRITE_F("attack_ms", attackMs);
		WRITE_F("release_ms", releaseMs);
		WRITE_I("mapping_mode", mappingMode);
		WRITE_I("spread_mode", comparisonSpreadMode);
		WRITE_B("enabled", enabled);
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			out << "audio_band_trim_" << std::setw(2) << std::setfill('0') << i << "=" << a.bandTrimDb[i] << "\n";
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
			out << "audio_role_gain_" << std::setw(2) << std::setfill('0') << i << "=" << a.roleBoostDb[i] << "\n";
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
		{
			out << "audio_accent_route_" << std::setw(2) << std::setfill('0') << i << "=" << (a.accentRouting[i] ? 1 : 0) << "\n";
			out << "audio_accent_gain_" << std::setw(2) << std::setfill('0') << i << "=" << a.accentGain[i] << "\n";
		}
#undef WRITE_F
#undef WRITE_B
#undef WRITE_I
	}

	void readAlgorithmAudioLevels(const std::map<std::string, std::string>& values, const std::string& prefix,
		AlgorithmAudioLevels& levels, float minimum, float maximum, bool& sawValue)
	{
		for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
		{
			const AlgorithmAudioChannel id = static_cast<AlgorithmAudioChannel>(channel);
			const char* stableId = audioChannelId(id);
			if (!stableId) continue;
			const std::map<std::string, std::string>::const_iterator found = values.find(prefix + stableId);
			if (found == values.end()) continue;
			float value = 0.0f;
			if (parseFloat(values, found->first, value) && value >= minimum && value <= maximum)
			{
				levels.decibels[static_cast<std::size_t>(id)] = value;
				sawValue = true;
			}
		}
	}

	void readAlgorithmAudio(const std::map<std::string, std::string>& values, UserPreferences& preferences)
	{
		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
		{
			const sortinglogic::AlgorithmDescriptor& algorithm = catalog[i];
			const std::string familyPrefix = "family_audio." + algorithm.familyId + ".";
			if (preferences.familyAudioDefaults.find(algorithm.familyId) == preferences.familyAudioDefaults.end())
			{
				AlgorithmAudioLevels levels = defaultFamilyAudioLevels(preferences.audio);
				bool saw = false;
				readAlgorithmAudioLevels(values, familyPrefix, levels, -60.0f, 30.0f, saw);
				if (saw) preferences.familyAudioDefaults[algorithm.familyId] = levels;
			}
			const std::string variantKey = variantAudioSettingsKey(algorithm.familyId, algorithm.variantId);
			if (preferences.variantAudioOffsets.find(variantKey) == preferences.variantAudioOffsets.end())
			{
				AlgorithmAudioLevels levels;
				bool saw = false;
				readAlgorithmAudioLevels(values, "variant_audio." + variantKey + ".", levels, -60.0f, 12.0f, saw);
				if (saw) preferences.variantAudioOffsets[variantKey] = levels;
			}
		}
	}

	void writeAlgorithmAudio(std::ostringstream& out, const UserPreferences& preferences)
	{
		out << std::setprecision(9);
		for (std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator family = preferences.familyAudioDefaults.begin();
			family != preferences.familyAudioDefaults.end(); ++family)
			for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
			{
				const AlgorithmAudioChannel id = static_cast<AlgorithmAudioChannel>(channel);
				const char* stableId = audioChannelId(id);
				if (stableId) out << "family_audio." << family->first << "." << stableId << "="
					<< family->second.decibels[static_cast<std::size_t>(id)] << "\n";
			}
		for (std::map<std::string, AlgorithmAudioLevels>::const_iterator variant = preferences.variantAudioOffsets.begin();
			variant != preferences.variantAudioOffsets.end(); ++variant)
		for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
		{
			const AlgorithmAudioChannel id = static_cast<AlgorithmAudioChannel>(channel);
			const char* stableId = audioChannelId(id);
			if (stableId) out << "variant_audio." << variant->first << "." << stableId << "="
				<< variant->second.decibels[static_cast<std::size_t>(id)] << "\n";
		}
	}

	bool inRange(float value, float minimum, float maximum)
	{
		return std::isfinite(value) && value >= minimum && value <= maximum;
	}

	bool validAudio(const SoundEngine::Parameters& a)
	{
		if (!inRange(a.masterLevel, 0.0f, 1.0f) || !inRange(a.bedLevel, 0.0f, 1.0f) ||
			!inRange(a.bedHarmonicExponent, 0.5f, 3.0f) || !inRange(a.bedComb.delayMicroseconds, 0.0f, 5000.0f) ||
			!inRange(a.bedComb.phaseOffsetDegrees, -180.0f, 180.0f) || !inRange(a.bedComb.wetAtLowFrequency, 0.0f, 1.0f) ||
			!inRange(a.bedComb.wetAtHighFrequency, 0.0f, 1.0f) || !inRange(a.bedComb.fadeStartHz, 0.0f, 17100.0f) ||
			!inRange(a.bedComb.fadeEndHz, 0.0f, 17100.0f) || !inRange(a.bedComb.fadeExponent, 0.25f, 8.0f) ||
			!inRange(a.comparisonBoostDb, -60.0f, 30.0f) || !inRange(a.heldKeyBoostDb, -60.0f, 30.0f) ||
			!inRange(a.tauBelowBins, 0.05f, static_cast<float>(SoundEngine::BandCount)) ||
			!inRange(a.tauAboveBins, 0.05f, static_cast<float>(SoundEngine::BandCount)) ||
			!inRange(a.chorusMix, 0.0f, 0.5f) || !inRange(a.chorusRateHz, 0.1f, 5.0f) ||
			!inRange(a.chorusDepthMs, 0.0f, 8.0f) || !inRange(a.attackMs, 1.0f, 200.0f) ||
			!inRange(a.releaseMs, 1.0f, 2000.0f) || static_cast<int>(a.chorusWeighting) < 0 ||
			static_cast<int>(a.chorusWeighting) > 1 || static_cast<int>(a.mappingMode) < 0 ||
			static_cast<int>(a.mappingMode) > 2 || static_cast<int>(a.comparisonSpreadMode) < 0 ||
			static_cast<int>(a.comparisonSpreadMode) > 1)
			return false;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			if (!inRange(a.bandTrimDb[i], -96.0f, 12.0f)) return false;
		for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
			if (!inRange(a.roleBoostDb[i], -60.0f, 12.0f)) return false;
		for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
			if (!inRange(a.accentGain[i], 0.0f, 0.5f)) return false;
		return true;
	}
}

namespace
{
	const char* audioChannelId(AlgorithmAudioChannel channel)
	{
		switch (channel)
		{
		case AlgorithmAudioChannel::Comparison: return "comparison";
		case AlgorithmAudioChannel::ProbeAccent: return "probe";
		case AlgorithmAudioChannel::MainChangeAccent: return "main_change";
		case AlgorithmAudioChannel::AuxiliaryAccent: return "auxiliary";
		case AlgorithmAudioChannel::DiscardAccent: return "discard";
		case AlgorithmAudioChannel::HeldKeyRole: return "role_held_key";
		case AlgorithmAudioChannel::PivotRole: return "role_pivot";
		case AlgorithmAudioChannel::CurrentMinimumRole: return "role_current_minimum";
		case AlgorithmAudioChannel::CurrentMaximumRole: return "role_current_maximum";
		case AlgorithmAudioChannel::WriteTargetRole: return "role_write_target";
		case AlgorithmAudioChannel::ProbedItemRole: return "role_probed_item";
		case AlgorithmAudioChannel::ProcessedItemRole: return "role_processed_item";
		default: return nullptr;
		}
	}

	bool parseAudioChannelId(const std::string& value, AlgorithmAudioChannel& channel)
	{
		for (int i = 0; i < static_cast<int>(AlgorithmAudioChannel::Count); ++i)
		{
			const AlgorithmAudioChannel candidate = static_cast<AlgorithmAudioChannel>(i);
			const char* id = audioChannelId(candidate);
			if (id && value == id) { channel = candidate; return true; }
		}
		return false;
	}

	float gainToDb(float gain)
	{
		return gain <= 0.000001f ? -60.0f : (std::max)(-60.0f, 20.0f * std::log10(gain));
	}

	float dbToGain(float decibels)
	{
		return decibels <= -60.0f ? 0.0f : std::pow(10.0f, decibels / 20.0f);
	}

	const AlgorithmAudioLevels* findVariantOverride(const UserPreferences& preferences,
		const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
	{
		const std::map<std::string, AlgorithmAudioLevels>::const_iterator found =
			preferences.variantAudioOffsets.find(variantAudioSettingsKey(family, variant));
		return found == preferences.variantAudioOffsets.end() ? nullptr : &found->second;
	}

	void migrateLegacyAlgorithmOption(UserPreferences& preferences, sortinglogic::SortAlgorithm algorithmId,
		const std::string& groupId, int value)
	{
		const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptor(algorithmId);
		if (!algorithm) return;
		for (std::size_t i = 0; i < algorithm->supportedOptions.size(); ++i)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm->supportedOptions[i];
			if (std::string(option.groupId) != groupId || option.value != value) continue;
			const std::string key = algorithmOptionSettingsKey(algorithm->familyId, algorithm->variantId, groupId);
			if (option.isDefault) preferences.algorithmOptions.erase(key);
			else preferences.algorithmOptions[key] = option.stableId;
			return;
		}
	}
}

std::string variantAudioSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant)
{
	return family + "." + variant;
}

std::string algorithmOptionSettingsKey(const sortinglogic::FamilyId& family, const sortinglogic::VariantId& variant,
	const std::string& groupId)
{
	return family + "." + variant + "." + groupId;
}

std::string algorithmOptionStableId(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId)
{
	const std::string key = algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, groupId);
	const std::map<std::string, std::string>::const_iterator stored = preferences.algorithmOptions.find(key);
	if (stored != preferences.algorithmOptions.end())
		for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
			if (std::string(algorithm.supportedOptions[i].groupId) == groupId &&
				stored->second == algorithm.supportedOptions[i].stableId)
				return stored->second;
	const sortinglogic::AlgorithmOptionDescriptor* defaultOption = nullptr;
	for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
	{
		const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[i];
		if (std::string(option.groupId) != groupId) continue;
		if (!defaultOption && option.isDefault) defaultOption = &option;
	}
	return defaultOption ? defaultOption->stableId : std::string();
}

int algorithmOptionValue(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId)
{
	const std::string stableId = algorithmOptionStableId(preferences, algorithm, groupId);
	for (std::size_t i = 0; i < algorithm.supportedOptions.size(); ++i)
		if (std::string(algorithm.supportedOptions[i].groupId) == groupId &&
			stableId == algorithm.supportedOptions[i].stableId)
			return algorithm.supportedOptions[i].value;
	return 0;
}

AlgorithmAudioLevels defaultFamilyAudioLevels(const SoundEngine::Parameters& audio)
{
	(void)audio;
	// The reconstructed model's calibrated comparison and role defaults are all
	// unity gain. Legacy global audio values remain stored only for migration and
	// preview compatibility; they do not silently shape this model.
	return AlgorithmAudioLevels();
}

AlgorithmAudioLevels familyAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family)
{
	const std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator found =
		preferences.familyAudioDefaults.find(family);
	return found == preferences.familyAudioDefaults.end() ? defaultFamilyAudioLevels(preferences.audio) : found->second;
}

AlgorithmAudioLevels variantAudioLevels(const UserPreferences& preferences, const sortinglogic::FamilyId& family,
	const sortinglogic::VariantId& variant)
{
	const AlgorithmAudioLevels* found = findVariantOverride(preferences, family, variant);
	return found ? *found : AlgorithmAudioLevels();
}

SoundEngine::Parameters effectiveAudioParameters(const UserPreferences& preferences,
	const sortinglogic::AlgorithmDescriptor& algorithm)
{
	SoundEngine::Parameters audio = preferences.audio;
	const AlgorithmAudioLevels family = familyAudioLevels(preferences, algorithm.familyId);
	const AlgorithmAudioLevels offsets = variantAudioLevels(preferences, algorithm.familyId, algorithm.variantId);
	const auto level = [&family, &offsets](AlgorithmAudioChannel channel)
	{
		const std::size_t index = static_cast<std::size_t>(channel);
		return (std::max)(-60.0f, (std::min)(30.0f, family.decibels[index] + offsets.decibels[index]));
	};
	audio.comparisonBoostDb = level(AlgorithmAudioChannel::Comparison);
	audio.heldKeyBoostDb = level(AlgorithmAudioChannel::HeldKeyRole);
	const AlgorithmAudioChannel accentChannels[SoundEngine::AccentCategoryCount] = {
		AlgorithmAudioChannel::ProbeAccent, AlgorithmAudioChannel::MainChangeAccent,
		AlgorithmAudioChannel::AuxiliaryAccent, AlgorithmAudioChannel::DiscardAccent
	};
	for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
	{
		const float db = level(accentChannels[i]);
		audio.accentRouting[i] = db > -60.0f;
		audio.accentGain[i] = audio.accentRouting[i] ? (std::min)(0.5f, dbToGain(db)) : 0.0f;
	}
	const struct RoleBinding { sortinglogic::SemanticRole semantic; SoundEngine::ComparisonRole sound; AlgorithmAudioChannel channel; } bindings[] = {
		{ sortinglogic::SemanticRole::Pivot, SoundEngine::ComparisonRole::Pivot, AlgorithmAudioChannel::PivotRole },
		{ sortinglogic::SemanticRole::CurrentMinimum, SoundEngine::ComparisonRole::CurrentMinimum, AlgorithmAudioChannel::CurrentMinimumRole },
		{ sortinglogic::SemanticRole::CurrentMaximum, SoundEngine::ComparisonRole::CurrentMaximum, AlgorithmAudioChannel::CurrentMaximumRole },
		{ sortinglogic::SemanticRole::WriteTarget, SoundEngine::ComparisonRole::WriteTarget, AlgorithmAudioChannel::WriteTargetRole },
		{ sortinglogic::SemanticRole::ProbedItem, SoundEngine::ComparisonRole::ProbedItem, AlgorithmAudioChannel::ProbedItemRole },
		{ sortinglogic::SemanticRole::ProcessedItem, SoundEngine::ComparisonRole::ProcessedItem, AlgorithmAudioChannel::ProcessedItemRole }
	};
	for (std::size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
		audio.roleBoostDb[static_cast<std::size_t>(bindings[i].sound)] = level(bindings[i].channel);
	return audio;
}

std::vector<AlgorithmAudioChannel> supportedAudioRoleChannels(const sortinglogic::AlgorithmDescriptor& algorithm)
{
	std::vector<AlgorithmAudioChannel> channels;
	for (std::size_t i = 0; i < algorithm.comparisonAudioRoles.size(); ++i)
	{
		switch (algorithm.comparisonAudioRoles[i])
		{
		case sortinglogic::SemanticRole::HeldKey: channels.push_back(AlgorithmAudioChannel::HeldKeyRole); break;
		case sortinglogic::SemanticRole::Pivot: channels.push_back(AlgorithmAudioChannel::PivotRole); break;
		case sortinglogic::SemanticRole::CurrentMinimum: channels.push_back(AlgorithmAudioChannel::CurrentMinimumRole); break;
		case sortinglogic::SemanticRole::CurrentMaximum: channels.push_back(AlgorithmAudioChannel::CurrentMaximumRole); break;
		case sortinglogic::SemanticRole::WriteTarget: channels.push_back(AlgorithmAudioChannel::WriteTargetRole); break;
		case sortinglogic::SemanticRole::ProbedItem: channels.push_back(AlgorithmAudioChannel::ProbedItemRole); break;
		case sortinglogic::SemanticRole::ProcessedItem: channels.push_back(AlgorithmAudioChannel::ProcessedItemRole); break;
		default: break;
		}
	}
	return channels;
}

const char* algorithmAudioChannelName(AlgorithmAudioChannel channel)
{
	switch (channel)
	{
	case AlgorithmAudioChannel::Comparison: return "Comparisons";
	case AlgorithmAudioChannel::ProbeAccent: return "Probe accents";
	case AlgorithmAudioChannel::MainChangeAccent: return "Main-array changes";
	case AlgorithmAudioChannel::AuxiliaryAccent: return "Auxiliary and counter updates";
	case AlgorithmAudioChannel::DiscardAccent: return "Discard accents";
	case AlgorithmAudioChannel::HeldKeyRole: return "Held-key emphasis";
	case AlgorithmAudioChannel::PivotRole: return "Pivot emphasis";
	case AlgorithmAudioChannel::CurrentMinimumRole: return "Current-minimum emphasis";
	case AlgorithmAudioChannel::CurrentMaximumRole: return "Current-maximum emphasis";
	case AlgorithmAudioChannel::WriteTargetRole: return "Write-target emphasis";
	case AlgorithmAudioChannel::ProbedItemRole: return "Probed-item emphasis";
	case AlgorithmAudioChannel::ProcessedItemRole: return "Processed-item emphasis";
	default: return "Audio level";
	}
}

VisualPreferences::VisualPreferences() : palette(PaletteId::Cyan), spacing(BarSpacingMode::Compact), panelExpanded(true) {}
SimulationPreferences::SimulationPreferences() : itemCount(80), durationSeconds(10),
	playbackTimingMode(SoundEngine::PlaybackTimingMode::FixedDuration), descending(false),
	algorithm(static_cast<int>(sortinglogic::SortAlgorithm::Insertion)), familyId("insertion-methods"), variantId("insertion"),
	profile(sortinglogic::InputProfile::NormalizedFloat), domain(sortinglogic::InputDomain::NormalizedFloats),
	pattern(sortinglogic::InputPattern::Random), mergeBufferVisualizationEnabled(true), completionScanEnabled(false) {}
UserPreferences::UserPreferences() : version(CurrentVersion), visual(), appearance(), simulation(), audio(), reconstructedAudio(), familyAudioDefaults(),
	variantAudioOffsets(), algorithmOptions() {}

bool UserPreferences::operator==(const UserPreferences& other) const
{
	return visual.palette == other.visual.palette && visual.spacing == other.visual.spacing &&
		visual.panelExpanded == other.visual.panelExpanded && sameUiAppearancePreferences(appearance, other.appearance) &&
		simulation.itemCount == other.simulation.itemCount && simulation.durationSeconds == other.simulation.durationSeconds &&
		simulation.playbackTimingMode == other.simulation.playbackTimingMode && simulation.descending == other.simulation.descending &&
		simulation.algorithm == other.simulation.algorithm && simulation.familyId == other.simulation.familyId &&
		simulation.variantId == other.simulation.variantId && simulation.profile == other.simulation.profile &&
		simulation.domain == other.simulation.domain && simulation.pattern == other.simulation.pattern &&
		simulation.mergeBufferVisualizationEnabled == other.simulation.mergeBufferVisualizationEnabled &&
		simulation.completionScanEnabled == other.simulation.completionScanEnabled && audio == other.audio &&
		reconstructedAudio == other.reconstructedAudio && familyAudioDefaults == other.familyAudioDefaults &&
		variantAudioOffsets == other.variantAudioOffsets && algorithmOptions == other.algorithmOptions;
}

bool UserPreferences::isValid() const
{
	return version == CurrentVersion && simulation.itemCount >= static_cast<int>(sortinglogic::MinItemCount) &&
		simulation.itemCount <= static_cast<int>(sortinglogic::MaxItemCount) &&
		std::isfinite(simulation.durationSeconds) && simulation.durationSeconds >= 1.0f && simulation.durationSeconds <= 120.0f &&
		sortinglogic::algorithmDescriptorById(simulation.familyId, simulation.variantId) != nullptr &&
		sortinglogic::algorithmDescriptorById(simulation.familyId, simulation.variantId)->id ==
			static_cast<sortinglogic::SortAlgorithm>(simulation.algorithm) &&
		(static_cast<int>(simulation.profile) == static_cast<int>(sortinglogic::InputProfile::NormalizedFloat) ||
		 static_cast<int>(simulation.profile) == static_cast<int>(sortinglogic::InputProfile::IntegerKeys)) &&
		static_cast<int>(simulation.domain) >= static_cast<int>(sortinglogic::InputDomain::NormalizedFloats) &&
		static_cast<int>(simulation.domain) <= static_cast<int>(sortinglogic::InputDomain::IntegerPermutation) &&
		simulation.profile == sortinglogic::inputProfileForDomain(simulation.domain) &&
		sortinglogic::inputPatternCompatible(simulation.domain, simulation.pattern) &&
		(static_cast<int>(simulation.playbackTimingMode) == static_cast<int>(SoundEngine::PlaybackTimingMode::FixedDuration) ||
		 static_cast<int>(simulation.playbackTimingMode) == static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence)) &&
		static_cast<int>(visual.palette) >= 0 && static_cast<int>(visual.palette) <= static_cast<int>(PaletteId::Windows95) &&
		static_cast<int>(visual.spacing) >= 0 && static_cast<int>(visual.spacing) <= static_cast<int>(BarSpacingMode::Comfortable) &&
		static_cast<int>(appearance.activePreset) >= static_cast<int>(UiPresetId::Current) &&
		static_cast<int>(appearance.activePreset) <= static_cast<int>(UiPresetId::ClassicWindows95) &&
		[&]()
		{
			const std::vector<UiPresetId>& presets = uiPresetCatalog();
			for (std::size_t i = 0; i < presets.size(); ++i)
			{
				const std::map<UiPresetId, UiThemeCustomization>::const_iterator theme = appearance.themes.find(presets[i]);
				if (theme == appearance.themes.end() || !validateUiThemeCustomization(presets[i], theme->second)) return false;
			}
			return true;
		}() &&
		validAudio(audio) && validReconstructedAudio(reconstructedAudio) && [&]()
		{
			std::map<std::string, bool> knownFamilies;
			const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
			for (std::size_t i = 0; i < catalog.size(); ++i) knownFamilies[catalog[i].familyId] = true;
			for (std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator i = familyAudioDefaults.begin();
				i != familyAudioDefaults.end(); ++i)
			{
				if (knownFamilies.find(i->first) == knownFamilies.end()) return false;
				for (std::size_t level = 0; level < i->second.decibels.size(); ++level)
					if (!inRange(i->second.decibels[level], -60.0f, 30.0f)) return false;
			}
			for (std::map<std::string, AlgorithmAudioLevels>::const_iterator i = variantAudioOffsets.begin();
				i != variantAudioOffsets.end(); ++i)
			{
				const std::string::size_type split = i->first.find('.');
				if (split == std::string::npos || !sortinglogic::algorithmDescriptorById(i->first.substr(0, split), i->first.substr(split + 1))) return false;
				for (std::size_t level = 0; level < i->second.decibels.size(); ++level)
					if (!inRange(i->second.decibels[level], -60.0f, 12.0f)) return false;
			}
			for (std::map<std::string, std::string>::const_iterator i = algorithmOptions.begin(); i != algorithmOptions.end(); ++i)
			{
				bool knownOption = false;
				for (std::size_t algorithmIndex = 0; algorithmIndex < catalog.size() && !knownOption; ++algorithmIndex)
				{
					const sortinglogic::AlgorithmDescriptor& algorithm = catalog[algorithmIndex];
					for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
					{
						const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
						if (algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, option.groupId) == i->first &&
							option.stableId == i->second)
						{
							knownOption = true;
							break;
						}
					}
				}
				if (!knownOption) return false;
			}
			return true;
		}();
}

SettingsStore::SettingsStore() : path_(settingsPath()) {}
SettingsStore::SettingsStore(const std::wstring& path) : path_(path) {}

UserPreferences SettingsStore::load() const
{
	UserPreferences defaults;
	if (path_.empty()) return defaults;
	HANDLE file = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return defaults;
	LARGE_INTEGER fileSize;
	if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > 1024 * 1024)
	{
		CloseHandle(file);
		return defaults;
	}
	std::string content(static_cast<std::size_t>(fileSize.QuadPart), '\0');
	DWORD read = 0;
	const BOOL readOk = content.empty() || ReadFile(file, &content[0], static_cast<DWORD>(content.size()), &read, nullptr);
	CloseHandle(file);
	if (!readOk || read != content.size()) return defaults;
	std::map<std::string, std::string> values;
	std::istringstream input(content);
	std::string line;
	while (std::getline(input, line))
	{
		if (line.empty() || line[0] == '#' || line[0] == ';') continue;
		const std::string::size_type equals = line.find('=');
		if (equals != std::string::npos) values[line.substr(0, equals)] = line.substr(equals + 1);
	}
	int version = 0;
	if (!parseInt(values, "version", version) || (version < 1 || version > CurrentVersion)) return defaults;
	defaults.version = CurrentVersion;
	std::map<std::string, std::string>::const_iterator found = values.find("palette");
	if (found != values.end() && !parsePalette(found->second, defaults.visual.palette))
	{
		if (version < 7) return UserPreferences();
		defaults.visual.palette = PaletteId::Cyan;
	}
	found = values.find("spacing");
	if (found != values.end() && !parseSpacing(found->second, defaults.visual.spacing))
	{
		if (version < 7) return UserPreferences();
		defaults.visual.spacing = BarSpacingMode::Compact;
	}
	readAppearance(values, version, defaults);
	if (!optionalBool(values, "panel_expanded", defaults.visual.panelExpanded) ||
		!optionalBool(values, "descending", defaults.simulation.descending) ||
		!optionalBool(values, "merge_buffer_visualization", defaults.simulation.mergeBufferVisualizationEnabled) ||
		!optionalBool(values, "completion_scan", defaults.simulation.completionScanEnabled) ||
		!optionalInt(values, "item_count", defaults.simulation.itemCount) ||
		!optionalFloat(values, "duration_seconds", defaults.simulation.durationSeconds)) return UserPreferences();
	found = values.find("playback_timing");
	if (found != values.end()) defaults.simulation.playbackTimingMode = parsePlaybackTimingMode(found->second);
	if (version <= 3 && !optionalInt(values, "algorithm", defaults.simulation.algorithm)) return UserPreferences();
	sortinglogic::ShellGapSequence legacyShellGap = sortinglogic::ShellGapSequence::Halving;
	sortinglogic::MergeWritebackMode legacyMergeWriteback = sortinglogic::MergeWritebackMode::AtomicRangeWrite;
	sortinglogic::MergeExecutionMode legacyMergeExecution = sortinglogic::MergeExecutionMode::BottomUp;
	bool hasLegacyShellGap = false;
	bool hasLegacyMergeWriteback = false;
	bool hasLegacyMergeExecution = false;
	if (version < 3)
	{
		// v1/v2 stored the sole algorithm as catalog position zero.
		defaults.simulation.algorithm = static_cast<int>(sortinglogic::SortAlgorithm::Insertion);
		defaults.simulation.profile = sortinglogic::InputProfile::NormalizedFloat;
		defaults.simulation.domain = sortinglogic::InputDomain::NormalizedFloats;
		defaults.simulation.pattern = sortinglogic::InputPattern::Random;
	}
	else if (version == 3)
	{
		found = values.find("profile");
		if (found != values.end() && !parseProfile(found->second, defaults.simulation.profile)) return UserPreferences();
		defaults.simulation.domain = defaults.simulation.profile == sortinglogic::InputProfile::NormalizedFloat
			? sortinglogic::InputDomain::NormalizedFloats : sortinglogic::InputDomain::NonnegativeIntegerKeys;
		defaults.simulation.pattern = sortinglogic::InputPattern::Random;
		found = values.find("shell_gap_sequence");
		if (found != values.end())
		{
			if (!parseGapSequence(found->second, legacyShellGap)) return UserPreferences();
			hasLegacyShellGap = true;
		}
		found = values.find("merge_writeback_mode");
		if (found != values.end())
		{
			if (!parseMergeWritebackMode(found->second, legacyMergeWriteback)) return UserPreferences();
			hasLegacyMergeWriteback = true;
		}
		found = values.find("merge_execution_mode");
		if (found != values.end())
		{
			if (!parseMergeExecutionMode(found->second, legacyMergeExecution)) return UserPreferences();
			hasLegacyMergeExecution = true;
		}
		const sortinglogic::AlgorithmDescriptor* oldDescriptor = sortinglogic::algorithmDescriptor(
			static_cast<sortinglogic::SortAlgorithm>(defaults.simulation.algorithm));
		if (!oldDescriptor) oldDescriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		defaults.simulation.algorithm = static_cast<int>(oldDescriptor->id);
		defaults.simulation.familyId = oldDescriptor->familyId;
		defaults.simulation.variantId = oldDescriptor->variantId;
	}
	else
	{
		const std::map<std::string, std::string>::const_iterator family = values.find("family_id");
		const std::map<std::string, std::string>::const_iterator variant = values.find("variant_id");
		const sortinglogic::AlgorithmDescriptor* selected = family != values.end() && variant != values.end()
			? sortinglogic::algorithmDescriptorById(family->second, variant->second) : nullptr;
		if (!selected && version == 4)
		{
			int numericAlgorithm = defaults.simulation.algorithm;
			if (optionalInt(values, "algorithm", numericAlgorithm))
				selected = sortinglogic::algorithmDescriptor(static_cast<sortinglogic::SortAlgorithm>(numericAlgorithm));
		}
		if (!selected) selected = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Insertion);
		defaults.simulation.algorithm = static_cast<int>(selected->id);
		defaults.simulation.familyId = selected->familyId;
		defaults.simulation.variantId = selected->variantId;
		found = values.find("domain");
		if (found != values.end())
		{
			if (!parseDomain(found->second, defaults.simulation.domain)) return UserPreferences();
			defaults.simulation.profile = sortinglogic::inputProfileForDomain(defaults.simulation.domain);
		}
		else
		{
			found = values.find("profile");
			if (found != values.end() && !parseProfile(found->second, defaults.simulation.profile)) return UserPreferences();
			defaults.simulation.domain = defaults.simulation.profile == sortinglogic::InputProfile::NormalizedFloat
				? sortinglogic::InputDomain::NormalizedFloats : sortinglogic::InputDomain::NonnegativeIntegerKeys;
		}
		found = values.find("pattern");
		if (found != values.end() && !parsePattern(found->second, defaults.simulation.pattern)) return UserPreferences();
		found = values.find("shell_gap_sequence");
		if (found != values.end())
		{
			if (!parseGapSequence(found->second, legacyShellGap)) return UserPreferences();
			hasLegacyShellGap = true;
		}
		found = values.find("merge_writeback_mode");
		if (found != values.end())
		{
			if (!parseMergeWritebackMode(found->second, legacyMergeWriteback)) return UserPreferences();
			hasLegacyMergeWriteback = true;
		}
		found = values.find("merge_execution_mode");
		if (found != values.end())
		{
			if (!parseMergeExecutionMode(found->second, legacyMergeExecution)) return UserPreferences();
			hasLegacyMergeExecution = true;
		}
	}
	if (hasLegacyShellGap)
		migrateLegacyAlgorithmOption(defaults, sortinglogic::SortAlgorithm::Shell, "gap_sequence", static_cast<int>(legacyShellGap));
	if (hasLegacyMergeWriteback)
		migrateLegacyAlgorithmOption(defaults, sortinglogic::SortAlgorithm::Merge, "writeback_mode", static_cast<int>(legacyMergeWriteback));
	if (hasLegacyMergeExecution)
		migrateLegacyAlgorithmOption(defaults, sortinglogic::SortAlgorithm::Merge, "execution_mode", static_cast<int>(legacyMergeExecution));
	// Stable metadata keys override the compatibility fields above. Unknown or
	// malformed option IDs are ignored so one bad option cannot discard settings.
	const std::vector<sortinglogic::AlgorithmDescriptor>& optionCatalog = sortinglogic::algorithmCatalog();
	for (std::size_t algorithmIndex = 0; algorithmIndex < optionCatalog.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithm = optionCatalog[algorithmIndex];
		for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
			const std::string iniKey = "option." + algorithm.familyId + "." + algorithm.variantId + "." + option.groupId;
			found = values.find(iniKey);
			if (found == values.end() || found->second != option.stableId) continue;
			const std::string preferenceKey = algorithmOptionSettingsKey(algorithm.familyId, algorithm.variantId, option.groupId);
			defaults.algorithmOptions[preferenceKey] = option.stableId;
		}
	}
	if (version >= 2)
	{
		SoundEngine::Parameters loaded = defaults.audio;
		if (readAudio(values, loaded) && validAudio(loaded)) defaults.audio = loaded;
		// A malformed new audio section should not discard an otherwise valid v1-era setup.
	}
	if (version >= 6)
	{
		SoundEngine::ReconstructedParameters loaded = defaults.reconstructedAudio;
		if (readReconstructedAudio(values, loaded) && validReconstructedAudio(loaded)) defaults.reconstructedAudio = loaded;
		// Malformed reconstructed values fall back independently to the calibrated profile.
	}
	if (version < 6)
	{
		// The reconstructed model starts with neutral algorithm gains. Preserve
		// the old audio block for compatibility, but do not carry its timbre or
		// inherited boosts into the new synthesis model.
		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			defaults.familyAudioDefaults[catalog[i].familyId] = AlgorithmAudioLevels();
	}
	else
		readAlgorithmAudio(values, defaults);
	if (!defaults.isValid()) return UserPreferences();
	return defaults;
}

bool SettingsStore::save(const UserPreferences& settings, std::string* error) const
{
	if (!settings.isValid() || path_.empty())
	{
		if (error) *error = "Settings are invalid or the LocalAppData path is unavailable.";
		return false;
	}
	const std::wstring directory = directoryOf(path_);
	if (directory.empty() || (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS))
	{
		if (error) *error = "Could not create the settings directory.";
		return false;
	}
	const std::wstring temporaryPath = path_ + L".tmp";
	const sortinglogic::AlgorithmDescriptor* shellDescriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
	const sortinglogic::AlgorithmDescriptor* mergeDescriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
	const sortinglogic::ShellGapSequence shellGap = shellDescriptor
		? static_cast<sortinglogic::ShellGapSequence>(algorithmOptionValue(settings, *shellDescriptor, "gap_sequence"))
		: sortinglogic::ShellGapSequence::Halving;
	const sortinglogic::MergeWritebackMode mergeWriteback = mergeDescriptor
		? static_cast<sortinglogic::MergeWritebackMode>(algorithmOptionValue(settings, *mergeDescriptor, "writeback_mode"))
		: sortinglogic::MergeWritebackMode::AtomicRangeWrite;
	const sortinglogic::MergeExecutionMode mergeExecution = mergeDescriptor
		? static_cast<sortinglogic::MergeExecutionMode>(algorithmOptionValue(settings, *mergeDescriptor, "execution_mode"))
		: sortinglogic::MergeExecutionMode::BottomUp;
	std::ostringstream contents;
	contents << "version=" << CurrentVersion << "\n"
		<< "palette=" << paletteName(settings.visual.palette) << "\n"
		<< "spacing=" << spacingName(settings.visual.spacing) << "\n"
		<< "panel_expanded=" << (settings.visual.panelExpanded ? 1 : 0) << "\n"
		<< "ui_active_preset=" << uiPresetStableId(settings.appearance.activePreset) << "\n";
	for (std::map<UiPresetId, UiThemeCustomization>::const_iterator theme = settings.appearance.themes.begin();
		theme != settings.appearance.themes.end(); ++theme)
	{
		const std::string prefix = "ui." + uiPresetStableId(theme->first);
		contents << prefix << ".palette=" << paletteName(theme->second.palette) << "\n";
		for (std::map<std::string, UiTokenValue>::const_iterator token = theme->second.overrides.begin();
			token != theme->second.overrides.end(); ++token)
			contents << prefix << ".override." << token->first << "=" << encodeUiTokenValue(token->second) << "\n";
	}
	contents
		<< "item_count=" << settings.simulation.itemCount << "\n"
		<< std::setprecision(9) << "duration_seconds=" << settings.simulation.durationSeconds << "\n"
		<< "playback_timing=" << playbackTimingModeName(settings.simulation.playbackTimingMode) << "\n"
		<< "descending=" << (settings.simulation.descending ? 1 : 0) << "\n"
		<< "merge_buffer_visualization=" << (settings.simulation.mergeBufferVisualizationEnabled ? 1 : 0) << "\n"
		<< "completion_scan=" << (settings.simulation.completionScanEnabled ? 1 : 0) << "\n"
		<< "family_id=" << settings.simulation.familyId << "\n"
		<< "variant_id=" << settings.simulation.variantId << "\n"
		<< "profile=" << profileName(settings.simulation.profile) << "\n"
		<< "domain=" << domainName(settings.simulation.domain) << "\n"
		<< "pattern=" << patternName(settings.simulation.pattern) << "\n"
		<< "shell_gap_sequence=" << gapSequenceName(shellGap) << "\n"
		<< "merge_writeback_mode=" << mergeWritebackModeName(mergeWriteback) << "\n"
		<< "merge_execution_mode=" << mergeExecutionModeName(mergeExecution) << "\n";
	const std::vector<sortinglogic::AlgorithmDescriptor>& optionCatalog = sortinglogic::algorithmCatalog();
	for (std::size_t algorithmIndex = 0; algorithmIndex < optionCatalog.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithm = optionCatalog[algorithmIndex];
		for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
			bool groupAlreadyWritten = false;
			for (std::size_t previous = 0; previous < optionIndex; ++previous)
				if (std::string(algorithm.supportedOptions[previous].groupId) == option.groupId) groupAlreadyWritten = true;
			if (groupAlreadyWritten) continue;
			const std::string selected = algorithmOptionStableId(settings, algorithm, option.groupId);
			contents << "option." << algorithm.familyId << "." << algorithm.variantId << "." << option.groupId
				<< "=" << selected << "\n";
		}
	}
	writeAudio(contents, settings.audio);
	writeReconstructedAudio(contents, settings.reconstructedAudio);
	writeAlgorithmAudio(contents, settings);
	const std::string data = contents.str();
	HANDLE file = CreateFileW(temporaryPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		if (error) *error = "Could not open the temporary settings file.";
		return false;
	}
	DWORD written = 0;
	const BOOL wrote = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
	const BOOL flushed = wrote && written == data.size() ? FlushFileBuffers(file) : FALSE;
	CloseHandle(file);
	if (!flushed)
	{
		DeleteFileW(temporaryPath.c_str());
		if (error) *error = "Could not flush the temporary settings file.";
		return false;
	}
	const DWORD existingAttributes = GetFileAttributesW(path_.c_str());
	if (existingAttributes != INVALID_FILE_ATTRIBUTES)
	{
		const std::wstring backupPath = path_ + L".pre-v8.bak";
		if (GetFileAttributesW(backupPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			HANDLE oldFile = CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			char header[128] = {};
			DWORD headerBytes = 0;
			if (oldFile != INVALID_HANDLE_VALUE)
			{
				ReadFile(oldFile, header, sizeof(header) - 1, &headerBytes, nullptr);
				CloseHandle(oldFile);
			}
			const std::string headerText(header, headerBytes);
			const std::string::size_type versionAt = headerText.find("version=");
			const int oldVersion = versionAt == std::string::npos ? CurrentVersion : std::atoi(headerText.c_str() + versionAt + 8);
			if (oldVersion > 0 && oldVersion < CurrentVersion && !CopyFileW(path_.c_str(), backupPath.c_str(), TRUE))
			{
				DeleteFileW(temporaryPath.c_str());
				if (error) *error = "Could not preserve the previous settings file before its version-8 migration.";
				return false;
			}
		}
	}
	if (!MoveFileExW(temporaryPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(temporaryPath.c_str());
		if (error) *error = "Could not atomically replace the settings file.";
		return false;
	}
	if (error) error->clear();
	return true;
}
