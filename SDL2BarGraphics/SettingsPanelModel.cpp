#include "SettingsPanelModel.h"

#include <algorithm>
#include <cmath>

SettingsPanelModel::SettingsPanelModel()
	: page_(SettingsPanelPage::Appearance), audioGroup_(SettingsAudioGroup::Basic), scrollViewport_(), selectedAudioBand_(3)
{
}

void SettingsPanelModel::showPage(SettingsPanelPage page)
{
	switch (page)
	{
	case SettingsPanelPage::Appearance:
	case SettingsPanelPage::Algorithm:
	case SettingsPanelPage::GeneralAudio:
		page_ = page;
		break;
	default:
		page_ = SettingsPanelPage::Appearance;
		break;
	}
	scrollViewport_.setOffset(0.0f);
}

void SettingsPanelModel::showAudioGroup(SettingsAudioGroup group)
{
	page_ = SettingsPanelPage::GeneralAudio;
	audioGroup_ = group == SettingsAudioGroup::Advanced ? SettingsAudioGroup::Advanced : SettingsAudioGroup::Basic;
	scrollViewport_.setOffset(0.0f);
}

void SettingsPanelModel::setScrollOffset(float value)
{
	scrollViewport_.setOffset(std::isfinite(value) && value > 0.0f ? value : 0.0f);
}

void SettingsPanelModel::selectAudioBand(int index, int bandCount)
{
	selectedAudioBand_ = bandCount <= 0 ? 0 : std::max(0, std::min(bandCount - 1, index));
}
