#include "SettingsPanelControls.h"

#include <algorithm>

std::string settingsAlgorithmOptionControlId(const std::string& familyId,
	const std::string& variantId, const std::string& groupId)
{
	return std::string("algorithm_option_") + familyId + "_" + variantId + "_" + groupId;
}

std::vector<std::string> settingsPanelControlIds(SettingsPanelPage page, SettingsAudioGroup audioGroup,
	const SettingsPanelAlgorithmPresentation* selectedAlgorithm)
{
	std::vector<std::string> active;
	if (page == SettingsPanelPage::Appearance)
	{
		active.push_back("preset");
		active.push_back("palette");
		active.push_back("spacing");
		active.push_back("appearance_customize");
	}
	else if (page == SettingsPanelPage::Algorithm)
	{
		active.push_back("domain");
		active.push_back("pattern");
		active.push_back("playback_timing");
		active.push_back("completion_scan");
		if (selectedAlgorithm)
		{
			for (std::size_t option = 0; option < selectedAlgorithm->optionGroupIds.size(); ++option)
				active.push_back(settingsAlgorithmOptionControlId(selectedAlgorithm->familyId,
					selectedAlgorithm->variantId, selectedAlgorithm->optionGroupIds[option]));
			active.insert(active.end(), selectedAlgorithm->visibleAudioControlIds.begin(),
				selectedAlgorithm->visibleAudioControlIds.end());
		}
		else active.push_back("algorithm_audio_00");
		active.push_back("algorithm_audio_reset");
	}
	else if (audioGroup == SettingsAudioGroup::Basic)
	{
		const char* controls[] = { "recon_enabled", "recon_master", "recon_idle_policy", "recon_release", "recon_restore" };
		active.assign(controls, controls + sizeof(controls) / sizeof(controls[0]));
	}
	else
	{
		const char* controls[] = { "recon_min_pitch", "recon_max_pitch", "recon_mapping", "recon_clock",
			"recon_cycles", "recon_tukey", "recon_delay", "recon_lowpass", "recon_cutoff" };
		active.assign(controls, controls + sizeof(controls) / sizeof(controls[0]));
	}
	return active;
}
