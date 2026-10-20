#pragma once

#include "UiLayer.h"

enum class SettingsPanelPage { Appearance, Algorithm, GeneralAudio };
enum class SettingsAudioGroup { Basic, Advanced };

// Session-only navigation state for the settings panel. Persisted preferences
// remain owned by UserSettings and are never modified here.
class SettingsPanelModel
{
public:
	SettingsPanelModel();

	SettingsPanelPage page() const { return page_; }
	int pageIndex() const { return static_cast<int>(page_); }
	SettingsAudioGroup audioGroup() const { return audioGroup_; }
	float scrollOffset() const { return scrollViewport_.offset(); }
	UiScrollViewport& scrollViewport() { return scrollViewport_; }
	const UiScrollViewport& scrollViewport() const { return scrollViewport_; }
	int selectedAudioBand() const { return selectedAudioBand_; }
	bool isPage(SettingsPanelPage value) const { return page_ == value; }

	void showPage(SettingsPanelPage page);
	void showAudioGroup(SettingsAudioGroup group);
	void setScrollOffset(float value);
	void selectAudioBand(int index, int bandCount);

private:
	SettingsPanelPage page_;
	SettingsAudioGroup audioGroup_;
	UiScrollViewport scrollViewport_;
	int selectedAudioBand_;
};
