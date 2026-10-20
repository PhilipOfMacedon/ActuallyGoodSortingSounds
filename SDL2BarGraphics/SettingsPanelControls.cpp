#include "SettingsPanelControls.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "UserSettings.h"

std::string settingsAlgorithmOptionControlId(const sortinglogic::AlgorithmDescriptor& algorithm,
	const std::string& groupId)
{
	return std::string("algorithm_option_") + algorithm.familyId + "_" + algorithm.variantId + "_" + groupId;
}

std::string settingsAlgorithmOptionCaption(const std::string& groupId)
{
	if (groupId == "gap_sequence") return "Shell gap sequence";
	if (groupId == "execution_mode") return "Merge execution order";
	if (groupId == "writeback_mode") return "Merge writeback";
	if (groupId == "rotate_buffer_capacity") return "Temporary buffer capacity";
	if (groupId == "grail_buffer_mode") return "Grail buffer mode";
	std::string caption = groupId;
	std::replace(caption.begin(), caption.end(), '_', ' ');
	if (!caption.empty()) caption[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(caption[0])));
	return caption;
}

std::vector<std::string> settingsPanelControlIds(SettingsPanelPage page, SettingsAudioGroup audioGroup,
	const sortinglogic::AlgorithmDescriptor* selectedAlgorithm)
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
		if (selectedAlgorithm && selectedAlgorithm->id == sortinglogic::SortAlgorithm::Merge)
			active.push_back("merge_buffer_visualization");
		if (selectedAlgorithm)
		{
			std::set<std::string> groups;
			for (std::size_t option = 0; option < selectedAlgorithm->supportedOptions.size(); ++option)
			{
				const std::string groupId(selectedAlgorithm->supportedOptions[option].groupId);
				if (groups.insert(groupId).second)
					active.push_back(settingsAlgorithmOptionControlId(*selectedAlgorithm, groupId));
			}
		}
		const std::vector<AlgorithmAudioChannel> roleChannels = selectedAlgorithm
			? supportedAudioRoleChannels(*selectedAlgorithm) : std::vector<AlgorithmAudioChannel>();
		for (int channelIndex = 0; channelIndex < static_cast<int>(AlgorithmAudioChannel::Count); ++channelIndex)
		{
			const AlgorithmAudioChannel channel = static_cast<AlgorithmAudioChannel>(channelIndex);
			bool visible = false;
			switch (channel)
			{
			case AlgorithmAudioChannel::Comparison:
				visible = true;
				break;
			case AlgorithmAudioChannel::ProbeAccent:
				visible = selectedAlgorithm && (selectedAlgorithm->id == sortinglogic::SortAlgorithm::Counting ||
					selectedAlgorithm->id == sortinglogic::SortAlgorithm::Radix);
				break;
			case AlgorithmAudioChannel::AuxiliaryAccent:
				visible = selectedAlgorithm && (selectedAlgorithm->id == sortinglogic::SortAlgorithm::Merge ||
					selectedAlgorithm->id == sortinglogic::SortAlgorithm::Counting ||
					selectedAlgorithm->id == sortinglogic::SortAlgorithm::Radix);
				break;
			case AlgorithmAudioChannel::DiscardAccent:
				visible = selectedAlgorithm && std::string(selectedAlgorithm->behavior).find("discard") != std::string::npos;
				break;
			default:
				for (std::size_t role = 0; role < roleChannels.size(); ++role)
					visible = visible || roleChannels[role] == channel;
				break;
			}
			if (visible)
				active.push_back(std::string("algorithm_audio_") + (channelIndex < 10 ? "0" : "") +
					std::to_string(channelIndex));
		}
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
