#include "ControlPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

#include "LayoutMetrics.h"
#include "SettingsScroll.h"

namespace
{
	const MaterialId PanelMaterial = MaterialId::Panel;
	const MaterialId ButtonMaterial = MaterialId::Button;
	const MaterialId ValueMaterial = MaterialId::ValueButton;
	const MaterialId TimelineMaterial = MaterialId::Timeline;
	const MaterialId AccentMaterial = MaterialId::Accent;
	const MaterialId PopupMaterial = MaterialId::Popup;
	const RenderLayer UiLayer = RenderLayer::MainControls;
	const RenderLayer TextLayer = RenderLayer::MainText;
	const RenderLayer DropdownLayer = RenderLayer::Dropdown;
	const RenderLayer DropdownTextLayer = RenderLayer::DropdownText;
	const RenderLayer DimLayer = RenderLayer::ModalDim;
	const RenderLayer SettingsLayer = RenderLayer::SettingsPanel;
	const RenderLayer SettingsTextLayer = RenderLayer::SettingsText;

	struct AlgorithmFamilyEntry
	{
		sortinglogic::FamilyId id;
		std::string name;
		std::vector<const sortinglogic::AlgorithmDescriptor*> variants;
	};

	std::vector<AlgorithmFamilyEntry> algorithmFamilies()
	{
		std::vector<AlgorithmFamilyEntry> families;
		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
		{
			std::size_t familyIndex = 0;
			for (; familyIndex < families.size(); ++familyIndex)
				if (families[familyIndex].id == catalog[i].familyId) break;
			if (familyIndex == families.size())
			{
				AlgorithmFamilyEntry entry;
				entry.id = catalog[i].familyId;
				entry.name = catalog[i].familyDisplayName;
				families.push_back(entry);
			}
			families[familyIndex].variants.push_back(&catalog[i]);
		}
		return families;
	}

	std::string lowerAscii(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	bool containsText(const std::string& value, const std::string& query)
	{
		return query.empty() || lowerAscii(value).find(lowerAscii(query)) != std::string::npos;
	}

	std::vector<const AlgorithmFamilyEntry*> visibleFamilies(const std::vector<AlgorithmFamilyEntry>& families, const std::string& query)
	{
		std::vector<const AlgorithmFamilyEntry*> visible;
		for (std::size_t i = 0; i < families.size(); ++i)
		{
			bool match = containsText(families[i].name, query);
			for (std::size_t j = 0; !match && j < families[i].variants.size(); ++j)
				match = containsText(families[i].variants[j]->displayName, query);
			if (match) visible.push_back(&families[i]);
		}
		return visible;
	}

	std::vector<const sortinglogic::AlgorithmDescriptor*> visibleVariants(const AlgorithmFamilyEntry* family, const std::string& query)
	{
		std::vector<const sortinglogic::AlgorithmDescriptor*> visible;
		if (!family) return visible;
		for (std::size_t i = 0; i < family->variants.size(); ++i)
			if (query.empty() || containsText(family->variants[i]->displayName, query) || containsText(family->name, query))
				visible.push_back(family->variants[i]);
		return visible;
	}

	std::vector<std::string> paletteNames()
	{
		std::vector<std::string> names;
		const std::vector<PaletteId>& catalog = paletteCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			names.push_back(paletteDisplayName(catalog[i]));
		return names;
	}

	std::vector<std::string> spacingNames()
	{
		std::vector<std::string> names;
		names.push_back("None");
		names.push_back("Compact");
		names.push_back("Comfortable");
		return names;
	}

	std::vector<std::string> domainNames()
	{
		std::vector<std::string> names;
		for (int domain = static_cast<int>(sortinglogic::InputDomain::NormalizedFloats);
			domain <= static_cast<int>(sortinglogic::InputDomain::IntegerPermutation); ++domain)
			names.push_back(sortinglogic::inputDomainName(static_cast<sortinglogic::InputDomain>(domain)));
		return names;
	}

	std::vector<std::string> inputPatternNames()
	{
		std::vector<std::string> names;
		for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
			pattern <= static_cast<int>(sortinglogic::InputPattern::OrganPipe); ++pattern)
			names.push_back(sortinglogic::inputPatternName(static_cast<sortinglogic::InputPattern>(pattern)));
		return names;
	}

	std::vector<std::string> booleanNames()
	{
		return std::vector<std::string>{ "Off", "On" };
	}

	std::vector<std::string> bandNames()
	{
		std::vector<std::string> names;
		const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount>& bands = SoundEngine::frequencyBank();
		for (std::size_t i = 0; i < bands.size(); ++i)
		{
			std::ostringstream name;
			if (bands[i].frequencyHz >= 1000.0)
				name << std::fixed << std::setprecision(bands[i].frequencyHz < 10000.0 ? 1 : 0) << bands[i].frequencyHz / 1000.0 << " kHz";
			else name << static_cast<int>(bands[i].frequencyHz) << " Hz";
			names.push_back(name.str());
		}
		return names;
	}

	AppAction audioAction(AudioParameterId parameter, int index = 0)
	{
		return AppAction(AppActionKind::SetAudioParameter, 0.0f, 0, parameter, index);
	}

	bool inRect(const UiRect& rect, int x, int y)
	{
		return rect.contains(static_cast<float>(x), static_cast<float>(y));
	}

	int paletteIndex(PaletteId id) { return static_cast<int>(id); }
	int spacingIndex(BarSpacingMode mode) { return static_cast<int>(mode); }
	std::string algorithmLabel(sortinglogic::SortAlgorithm algorithm)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(algorithm);
		return descriptor ? descriptor->displayName : "Select algorithm";
	}

	std::string compactFamilyName(const char* displayName)
	{
		std::string name = displayName ? displayName : "";
		const char* suffixes[] = { " Methods", " Sorts", " Sort" };
		for (std::size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
		{
			const std::string suffix(suffixes[i]);
			if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
			{
				name.erase(name.size() - suffix.size());
				break;
			}
		}
		return name;
	}

	std::string compactVariantName(const char* displayName)
	{
		std::string name = displayName ? displayName : "";
		const std::string longWord("Transposition");
		const std::size_t position = name.find(longWord);
		if (position != std::string::npos) name.replace(position, longWord.size(), "Trans.");
		return name;
	}

	std::string algorithmOptionWidgetId(const sortinglogic::AlgorithmDescriptor& algorithm, const std::string& groupId)
	{
		return std::string("algorithm_option_") + algorithm.familyId + "_" + algorithm.variantId + "_" + groupId;
	}

	std::string algorithmOptionGroupCaption(const std::string& groupId)
	{
		if (groupId == "gap_sequence") return "Shell gap sequence";
		if (groupId == "execution_mode") return "Merge execution order";
		if (groupId == "writeback_mode") return "Merge writeback";
		std::string caption = groupId;
		std::replace(caption.begin(), caption.end(), '_', ' ');
		if (!caption.empty()) caption[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(caption[0])));
		return caption;
	}

	std::string settingsTabTitle(int tab, const UserPreferences& settings)
	{
		if (tab == 0) return "Settings | Appearance";
		if (tab == 1)
		{
			const sortinglogic::AlgorithmDescriptor* algorithm = sortinglogic::algorithmDescriptor(
				static_cast<sortinglogic::SortAlgorithm>(settings.simulation.algorithm));
			return std::string("Settings | Algorithm | ") + (algorithm ? algorithm->displayName : "Selected");
		}
		return "Settings | General Audio";
	}

	int shellGapValue(int selectedIndex)
	{
		const sortinglogic::AlgorithmDescriptor* shell = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
		if (shell && selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < shell->supportedOptions.size())
			return shell->supportedOptions[static_cast<std::size_t>(selectedIndex)].value;
		return static_cast<int>(sortinglogic::ShellGapSequence::Halving);
	}

	glm::vec4 brighter(glm::vec4 color)
	{
		color.r = std::min(1.0f, color.r * 1.18f + 0.025f);
		color.g = std::min(1.0f, color.g * 1.18f + 0.025f);
		color.b = std::min(1.0f, color.b * 1.18f + 0.025f);
		return color;
	}
}

ControlPanel::ControlPanel(Window& window, Renderer2D& renderer, const UserPreferences& settings)
	: window_(window), renderer_(renderer), settings_(settings),
	mainUi_(), settingsUi_(), labels_(), settingsCaptions_(), paletteCaption_(new TextLabel("Palette", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	spacingCaption_(new TextLabel("Bar spacing", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	title_(new TextLabel("Sorting Simulation | deterministic event trace", "Courier New Bold", 18, { 238, 246, 249, 255 })),
	detail_(new TextLabel("No trace", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	status_(new TextLabel("Ready.", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	settingsTitle_(new TextLabel("Settings", "Courier New Bold", 17, { 238, 246, 249, 255 })),
	settingsSaveStatus_(new TextLabel("Preferences save automatically.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	audioDeviceStatus_(new TextLabel("Audio device is not initialized.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	displayedDetail_(), displayedStatus_(), displayedSaveStatus_(), lastDetailUpdate_(0),
	timelineBounds_(), headerBounds_(), gearBounds_(), settingsPanelBounds_(), settingsContentBounds_(), draggingTimeline_(false),
	settingsOpen_(false), algorithmMenuOpen_(false), algorithmMenuVariantPane_(false), algorithmSearchFocused_(false),
	algorithmSearch_(), algorithmMenuFamily_(), algorithmFamilySelection_(0), algorithmVariantSelection_(0),
	algorithmFamilyScroll_(0), algorithmVariantScroll_(0), algorithmMenuBounds_(), algorithmSearchBounds_(),
	algorithmFamilyBounds_(), algorithmVariantBounds_(), settingsTab_(0), settingsScrollOffset_(0.0f), selectedBand_(3),
	chartTop_(184.0f), width_(0), height_(0), layoutKey_(), layoutCacheValid_(false)
{
	createWidgets();
	updateLayout();
}

ControlPanel::~ControlPanel()
{
}

bool ControlPanel::isReady() const
{
	return renderer_.isReady();
}

void ControlPanel::createWidgets()
{
	mainUi_.addButton("panel_toggle", ">>", AppAction(AppActionKind::TogglePanelExpanded));
	mainUi_.addButton("settings", "[=]", AppAction(), UiAction::ViewCommand::OpenSettings);
	mainUi_.addButton("shuffle", "Shuffle", AppAction(AppActionKind::Shuffle));
	mainUi_.addButton("insta_sort", "Insta-sort", AppAction(AppActionKind::InstaSort));
	mainUi_.addButton("insta_invert", "Insta-invert", AppAction(AppActionKind::InstaInvert));
	mainUi_.addButton("algorithm", algorithmLabel(static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm)),
		AppAction(), UiAction::ViewCommand::OpenAlgorithmMenu);
	mainUi_.addComboBox("direction", std::vector<std::string>{ "Ascending", "Descending" }, settings_.simulation.descending ? 1 : 0,
		AppAction(AppActionKind::SetDirection));
	mainUi_.addButton("run", "Run", AppAction(AppActionKind::Run));
	mainUi_.addButton("play", "Play", AppAction(AppActionKind::TogglePlayback));
	mainUi_.addButton("reset", "Reset", AppAction(AppActionKind::Reset));
	mainUi_.addButton("previous", "< Event", AppAction(AppActionKind::PreviousEvent));
	mainUi_.addButton("next", "Event >", AppAction(AppActionKind::NextEvent));
	mainUi_.addButton("bars_down_coarse", "Bars --", AppAction(AppActionKind::DecreaseCountCoarse));
	mainUi_.addButton("bars_down_fine", "Bars -", AppAction(AppActionKind::DecreaseCountFine));
	mainUi_.addButton("bars_value", "80 bars");
	mainUi_.addButton("bars_up_fine", "Bars +", AppAction(AppActionKind::IncreaseCountFine));
	mainUi_.addButton("bars_up_coarse", "Bars ++", AppAction(AppActionKind::IncreaseCountCoarse));
	mainUi_.addButton("time_down", "Time -", AppAction(AppActionKind::DecreaseDuration));
	mainUi_.addButton("time_value", "10 sec");
	mainUi_.addButton("time_up", "Time +", AppAction(AppActionKind::IncreaseDuration));
	settingsUi_.addButton("appearance_tab", "Appearance", AppAction(), UiAction::ViewCommand::ShowAppearanceSettings);
	settingsUi_.addButton("algorithm_tab", "Algorithm", AppAction(), UiAction::ViewCommand::ShowAlgorithmSettings);
	settingsUi_.addButton("audio_tab", "General Audio", AppAction(), UiAction::ViewCommand::ShowGeneralAudioSettings);
	settingsUi_.addComboBox("palette", paletteNames(), paletteIndex(settings_.visual.palette), AppAction(AppActionKind::SetPalette));
	settingsUi_.addComboBox("spacing", spacingNames(), spacingIndex(settings_.visual.spacing), AppAction(AppActionKind::SetSpacing));
	settingsUi_.addComboBox("domain", domainNames(), static_cast<int>(settings_.simulation.domain), AppAction(AppActionKind::SetInputDomain));
	settingsUi_.addComboBox("pattern", inputPatternNames(), static_cast<int>(settings_.simulation.pattern), AppAction(AppActionKind::SetInputPattern));
	settingsCaptions_["palette"] = "Palette";
	settingsCaptions_["spacing"] = "Bar spacing";
	settingsCaptions_["domain"] = "Input domain";
	settingsCaptions_["pattern"] = "Input pattern";
	const std::vector<sortinglogic::AlgorithmDescriptor>& optionCatalog = sortinglogic::algorithmCatalog();
	for (std::size_t algorithmIndex = 0; algorithmIndex < optionCatalog.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithm = optionCatalog[algorithmIndex];
		std::set<std::string> groups;
		for (std::size_t optionIndex = 0; optionIndex < algorithm.supportedOptions.size(); ++optionIndex)
		{
			const sortinglogic::AlgorithmOptionDescriptor& option = algorithm.supportedOptions[optionIndex];
			const std::string groupId(option.groupId);
			if (!groups.insert(groupId).second) continue;
			std::vector<std::string> optionNames;
			int selectedIndex = 0;
			const std::string selectedId = algorithmOptionStableId(settings_, algorithm, groupId);
			for (std::size_t candidateIndex = 0; candidateIndex < algorithm.supportedOptions.size(); ++candidateIndex)
			{
				const sortinglogic::AlgorithmOptionDescriptor& candidate = algorithm.supportedOptions[candidateIndex];
				if (groupId != candidate.groupId) continue;
				if (candidate.stableId == selectedId) selectedIndex = static_cast<int>(optionNames.size());
				optionNames.push_back(candidate.displayName);
			}
			const std::string id = algorithmOptionWidgetId(algorithm, groupId);
			settingsUi_.addComboBox(id, optionNames, selectedIndex,
				AppAction(AppActionKind::SetAlgorithmOption, algorithm.familyId, algorithm.variantId, groupId));
			settingsCaptions_[id] = algorithmOptionGroupCaption(groupId);
		}
	}
	const SoundEngine::Parameters& audio = settings_.audio;
	auto slider = [this](const std::string& id, const std::string& caption, float lo, float hi, float step,
		float value, AudioParameterId parameter, int actionIndex = 0)
	{
		settingsUi_.addSlider(id, caption, lo, hi, step, value, audioAction(parameter, actionIndex));
		settingsCaptions_[id] = caption;
	};
	auto selector = [this](const std::string& id, const std::string& caption, const std::vector<std::string>& options,
		int selected, AudioParameterId parameter, int actionIndex = 0)
	{
		const AppAction action = id == "band_selector"
			? AppAction(AppActionKind::SelectAudioBand)
			: audioAction(parameter, actionIndex);
		settingsUi_.addComboBox(id, options, selected, action);
		settingsCaptions_[id] = caption;
	};
	selector("audio_enabled", "Synthesis", booleanNames(), audio.enabled ? 1 : 0, AudioParameterId::Enabled);
	slider("master", "Master level", 0.0f, 1.0f, 0.01f, audio.masterLevel, AudioParameterId::MasterLevel);
	slider("bed", "Harmonic bed", 0.0f, 1.0f, 0.01f, audio.bedLevel, AudioParameterId::BedLevel);
	slider("harmonic_exponent", "Harmonic rolloff", 0.5f, 3.0f, 0.05f, audio.bedHarmonicExponent, AudioParameterId::BedHarmonicExponent);
	selector("sub300", "Sub-300 Hz bed", booleanNames(), audio.includeSub300HzBed ? 1 : 0, AudioParameterId::IncludeSub300HzBed);
	selector("comb_enabled", "Comb bed filter", booleanNames(), audio.bedComb.enabled ? 1 : 0, AudioParameterId::CombEnabled);
	selector("comb_phase", "Comb phase response", booleanNames(), audio.bedComb.applyPhase ? 1 : 0, AudioParameterId::CombApplyPhase);
	slider("comb_delay", "Comb delay (us)", 0.0f, 5000.0f, 10.0f, audio.bedComb.delayMicroseconds, AudioParameterId::CombDelayMicroseconds);
	slider("comb_phase_offset", "Comb phase offset (deg)", -180.0f, 180.0f, 1.0f, audio.bedComb.phaseOffsetDegrees, AudioParameterId::CombPhaseOffsetDegrees);
	slider("comb_wet_low", "Comb wet at 300 Hz", 0.0f, 1.0f, 0.01f, audio.bedComb.wetAtLowFrequency, AudioParameterId::CombWetLow);
	slider("comb_wet_high", "Comb wet at 17.1 kHz", 0.0f, 1.0f, 0.01f, audio.bedComb.wetAtHighFrequency, AudioParameterId::CombWetHigh);
	slider("comb_fade_start", "Comb fade start (Hz)", 0.0f, 17100.0f, 100.0f, audio.bedComb.fadeStartHz, AudioParameterId::CombFadeStartHz);
	slider("comb_fade_end", "Comb fade end (Hz)", 0.0f, 17100.0f, 100.0f, audio.bedComb.fadeEndHz, AudioParameterId::CombFadeEndHz);
	slider("comb_exponent", "Comb fade curve", 0.25f, 8.0f, 0.05f, audio.bedComb.fadeExponent, AudioParameterId::CombFadeExponent);
	slider("tau_below", "Tau below focus (bins)", 0.05f, static_cast<float>(SoundEngine::BandCount), 0.05f, audio.tauBelowBins, AudioParameterId::TauBelowBins);
	slider("tau_above", "Tau above focus (bins)", 0.05f, static_cast<float>(SoundEngine::BandCount), 0.05f, audio.tauAboveBins, AudioParameterId::TauAboveBins);
	slider("chorus_mix", "Chorus mix", 0.0f, 0.5f, 0.005f, audio.chorusMix, AudioParameterId::ChorusMix);
	slider("chorus_rate", "Chorus rate (Hz)", 0.1f, 5.0f, 0.05f, audio.chorusRateHz, AudioParameterId::ChorusRateHz);
	slider("chorus_depth", "Chorus depth (ms)", 0.0f, 8.0f, 0.1f, audio.chorusDepthMs, AudioParameterId::ChorusDepthMs);
	selector("chorus_weighting", "Chorus weighting", std::vector<std::string>{ "Uniform", "Comparison envelope" },
		static_cast<int>(audio.chorusWeighting), AudioParameterId::ChorusWeighting);
	slider("attack", "Attack (ms)", 1.0f, 200.0f, 1.0f, audio.attackMs, AudioParameterId::AttackMs);
	slider("release", "Release (ms)", 1.0f, 200.0f, 1.0f, audio.releaseMs, AudioParameterId::ReleaseMs);
	selector("mapping", "Value-to-frequency mapping", std::vector<std::string>{ "Linear Hz", "Log frequency", "Equal band slots" },
		static_cast<int>(audio.mappingMode), AudioParameterId::MappingMode);
	selector("spread", "Comparison spread", std::vector<std::string>{ "Fixed bins", "Position scaled" },
		static_cast<int>(audio.comparisonSpreadMode), AudioParameterId::SpreadMode);
	selector("band_selector", "Trim frequency", bandNames(), selectedBand_, AudioParameterId::BandTrim);
	slider("band_trim", "Selected band trim (dB)", -96.0f, 12.0f, 0.5f, audio.bandTrimDb[selectedBand_], AudioParameterId::BandTrim);
	for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
	{
		const AlgorithmAudioChannel level = static_cast<AlgorithmAudioChannel>(channel);
		const std::string id = std::string("algorithm_audio_") + (channel < 10 ? "0" : "") + std::to_string(channel);
		settingsUi_.addSlider(id, algorithmAudioChannelName(level), -60.0f, 12.0f, 1.0f, 0.0f,
			AppAction(AppActionKind::SetVariantAudioLevel, settings_.simulation.familyId,
				settings_.simulation.variantId, 0.0f, channel));
		settingsCaptions_[id] = algorithmAudioChannelName(level);
	}
	settingsUi_.addButton("algorithm_audio_reset", "Reset to family defaults",
		AppAction(AppActionKind::ResetVariantAudioLevels, settings_.simulation.familyId, settings_.simulation.variantId));
	settingsUi_.addButton("audio_retry", "Retry audio device", AppAction(AppActionKind::RetryAudioDevice));
	settingsUi_.addButton("settings_close", "Done", AppAction(), UiAction::ViewCommand::CloseSettings);
	settingsUi_.addContainer("settings_panel", UiContainerKind::Panel);

	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 16, { 238, 246, 249, 255 }));
	}
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *settingsUi_.widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 15, { 238, 246, 249, 255 }));
		for (std::size_t option = 0; option < widget.options().size(); ++option)
			labels_[widget.id() + "#option" + std::to_string(option)].reset(
				new TextLabel(widget.options()[option], "Courier New Bold", 14, { 238, 246, 249, 255 }));
	}
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		for (std::size_t option = 0; option < widget.options().size(); ++option)
			labels_[widget.id() + "#option" + std::to_string(option)].reset(
				new TextLabel(widget.options()[option], "Courier New Bold", 14, { 238, 246, 249, 255 }));
	}
	for (std::map<std::string, std::string>::const_iterator i = settingsCaptions_.begin(); i != settingsCaptions_.end(); ++i)
		labels_[i->first + "#caption"].reset(new TextLabel(i->second, "Courier New Bold", 12, { 238, 246, 249, 255 }));
}

void ControlPanel::updateLayout()
{
	width_ = window_.getLogicalWidth();
	height_ = window_.getLogicalHeight();
	if (width_ <= 0 || height_ <= 0)
		return;
	const LayoutCacheKey currentKey(width_, height_, window_.getDrawableWidth(), window_.getDrawableHeight(),
		settings_.visual.panelExpanded);
	if (layoutCacheValid_ && sameLayoutInputs(layoutKey_, currentKey))
	{
		layoutSettingsControls();
		layoutAlgorithmMenu();
		return;
	}
	layoutKey_ = currentKey;
	layoutCacheValid_ = true;
	headerBounds_ = UiRect(0.0f, 0.0f, static_cast<float>(width_), 42.0f);
	const ResponsivePanelLayout panelLayout = calculatePanelLayout(width_, settings_.visual.panelExpanded);
	const bool wide = panelLayout.wide;
	const float margin = 16.0f;
	const float availableWidth = std::max(1.0f, static_cast<float>(width_) - 2.0f * margin);
	const float gap = width_ < 620 ? 3.0f : 6.0f;
	const float rowHeight = 30.0f;
	mainUi_.clearContainers();
	mainUi_.addContainer("main_panel", UiContainerKind::Panel);
	mainUi_.addContainer("header_overlay", UiContainerKind::Overlay, "main_panel");
	mainUi_.attachWidget("panel_toggle", "header_overlay");
	mainUi_.attachWidget("settings", "header_overlay");
	mainUi_.addContainer("main_rows", UiContainerKind::Column, "main_panel");
	std::vector<std::string> groupA;
	groupA.push_back("shuffle"); groupA.push_back("insta_sort"); groupA.push_back("insta_invert"); groupA.push_back("algorithm");
	std::vector<std::string> groupB;
	groupB.push_back("direction"); groupB.push_back("run"); groupB.push_back("play"); groupB.push_back("reset");
	std::vector<std::string> groupC;
	groupC.push_back("previous"); groupC.push_back("next"); groupC.push_back("bars_down_coarse"); groupC.push_back("bars_down_fine"); groupC.push_back("bars_value");
	std::vector<std::string> groupD;
	groupD.push_back("bars_up_fine"); groupD.push_back("bars_up_coarse"); groupD.push_back("time_down"); groupD.push_back("time_value"); groupD.push_back("time_up");
	mainUi_.layoutContainer("main_panel", headerBounds_);
	mainUi_.find("panel_toggle")->setBounds(UiRect(12.0f, 8.0f, 28.0f, 26.0f));
	mainUi_.find("settings")->setBounds(UiRect(static_cast<float>(width_ - 42), 8.0f, 30.0f, 26.0f));
	gearBounds_ = mainUi_.find("settings")->bounds();
	if (wide)
	{
		std::vector<std::string> top = groupA;
		top.insert(top.end(), groupB.begin(), groupB.end());
		std::vector<std::string> bottom = groupC;
		bottom.insert(bottom.end(), groupD.begin(), groupD.end());
		mainUi_.addContainer("row_top", UiContainerKind::Row, "main_rows");
		mainUi_.addContainer("row_bottom", UiContainerKind::Row, "main_rows");
		for (std::size_t i = 0; i < top.size(); ++i) mainUi_.attachWidget(top[i], "row_top");
		for (std::size_t i = 0; i < bottom.size(); ++i) mainUi_.attachWidget(bottom[i], "row_bottom");
		mainUi_.layoutContainer("row_top", UiRect(margin, panelLayout.rowY[0], availableWidth, rowHeight), gap);
		mainUi_.layoutContainer("row_bottom", UiRect(margin, panelLayout.rowY[1], availableWidth, rowHeight), gap);
		timelineBounds_ = UiRect(margin, panelLayout.timelineY, availableWidth, 10.0f);
		chartTop_ = panelLayout.chartTop;
	}
	else
	{
		const std::vector<std::string>* groups[] = { &groupA, &groupB, &groupC, &groupD };
		for (std::size_t row = 0; row < 4; ++row)
		{
			const std::string rowId = "row_" + std::to_string(row);
			mainUi_.addContainer(rowId, UiContainerKind::Row, "main_rows");
			for (std::size_t i = 0; i < groups[row]->size(); ++i)
				mainUi_.attachWidget((*groups[row])[i], rowId);
			mainUi_.layoutContainer(rowId, UiRect(margin, panelLayout.rowY[row], availableWidth, rowHeight), gap);
		}
		timelineBounds_ = UiRect(margin, panelLayout.timelineY, availableWidth, 10.0f);
		chartTop_ = panelLayout.chartTop;
	}
	if (!settings_.visual.panelExpanded)
	{
		chartTop_ = panelLayout.chartTop;
		for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
		{
			const UiWidget* widget = mainUi_.widgets()[i].get();
			mainUi_.setWidgetVisible(widget->id(), widget->id() == "panel_toggle" || widget->id() == "settings");
		}
	}
	else
	{
		for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
			mainUi_.setWidgetVisible(mainUi_.widgets()[i]->id(), true);
	}
	mainUi_.find("panel_toggle")->setLabel(settings_.visual.panelExpanded ? "v" : ">");

	const float panelWidth = std::min(520.0f, std::max(180.0f, static_cast<float>(width_) - 32.0f));
	const float panelHeight = std::min(650.0f, std::max(300.0f, static_cast<float>(height_) - 54.0f));
	settingsPanelBounds_ = UiRect(std::max(8.0f, static_cast<float>(width_) - panelWidth - 8.0f), 42.0f,
		panelWidth, std::min(panelHeight, std::max(200.0f, static_cast<float>(height_) - 50.0f)));
	settingsUi_.layoutContainer("settings_panel", settingsPanelBounds_);
	if (settingsOpen_)
		settingsUi_.setModalBounds(settingsPanelBounds_, UiAction::ViewCommand::CloseSettings);
	layoutAlgorithmMenu();
	layoutSettingsControls();
}

void ControlPanel::layoutSettingsControls()
{
	if (settingsPanelBounds_.width <= 0.0f) return;
	const float x = settingsPanelBounds_.x + 16.0f;
	const float width = std::max(1.0f, settingsPanelBounds_.width - 32.0f);
	const float tabWidth = (width - 8.0f) / 3.0f;
	settingsUi_.find("appearance_tab")->setBounds(UiRect(x, settingsPanelBounds_.y + 42.0f, tabWidth, 28.0f));
	settingsUi_.find("algorithm_tab")->setBounds(UiRect(x + tabWidth + 4.0f, settingsPanelBounds_.y + 42.0f, tabWidth, 28.0f));
	settingsUi_.find("audio_tab")->setBounds(UiRect(x + 2.0f * (tabWidth + 4.0f), settingsPanelBounds_.y + 42.0f, tabWidth, 28.0f));
	settingsContentBounds_ = UiRect(x, settingsPanelBounds_.y + 82.0f, width,
		std::max(28.0f, settingsPanelBounds_.height - 142.0f));
	const float footerY = settingsPanelBounds_.y + settingsPanelBounds_.height - 38.0f;
	settingsUi_.find("settings_close")->setBounds(UiRect(settingsPanelBounds_.x + settingsPanelBounds_.width - 92.0f,
		footerY, 76.0f, 26.0f));
	settingsUi_.find("audio_retry")->setBounds(UiRect(x, footerY, std::min(190.0f, width - 90.0f), 26.0f));
	std::vector<std::string> active;
	if (settingsTab_ == 0)
	{
		active.push_back("palette"); active.push_back("spacing");
	}
	else if (settingsTab_ == 1)
	{
		active.push_back("domain"); active.push_back("pattern");
		const sortinglogic::AlgorithmDescriptor* selected = sortinglogic::algorithmDescriptor(
			static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
		if (selected)
		{
			std::set<std::string> groups;
			for (std::size_t optionIndex = 0; optionIndex < selected->supportedOptions.size(); ++optionIndex)
			{
				const std::string groupId(selected->supportedOptions[optionIndex].groupId);
				if (groups.insert(groupId).second) active.push_back(algorithmOptionWidgetId(*selected, groupId));
			}
		}
		for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
		{
			const AlgorithmAudioChannel level = static_cast<AlgorithmAudioChannel>(channel);
			bool visible = false;
			switch (level)
			{
			case AlgorithmAudioChannel::Comparison: case AlgorithmAudioChannel::MainChangeAccent: visible = true; break;
			case AlgorithmAudioChannel::ProbeAccent:
				visible = selected && (selected->id == sortinglogic::SortAlgorithm::Counting || selected->id == sortinglogic::SortAlgorithm::Radix); break;
			case AlgorithmAudioChannel::AuxiliaryAccent:
				visible = selected && (selected->id == sortinglogic::SortAlgorithm::Merge || selected->id == sortinglogic::SortAlgorithm::Counting || selected->id == sortinglogic::SortAlgorithm::Radix); break;
			case AlgorithmAudioChannel::DiscardAccent:
				visible = selected && std::string(selected->behavior).find("discard") != std::string::npos; break;
			default:
				if (selected)
				{
					const std::vector<AlgorithmAudioChannel> roles = supportedAudioRoleChannels(*selected);
					visible = std::find(roles.begin(), roles.end(), level) != roles.end();
				}
				break;
			}
			if (visible)
				active.push_back(std::string("algorithm_audio_") + (channel < 10 ? "0" : "") + std::to_string(channel));
		}
		active.push_back("algorithm_audio_reset");
	}
	else
	{
		const char* generalAudio[] = { "audio_enabled", "master", "bed", "harmonic_exponent", "sub300", "comb_enabled", "comb_phase",
			"comb_delay", "comb_phase_offset", "comb_wet_low", "comb_wet_high", "comb_fade_start", "comb_fade_end",
			"comb_exponent", "tau_below", "tau_above", "chorus_mix", "chorus_rate", "chorus_depth",
			"chorus_weighting", "attack", "release", "mapping", "spread", "band_selector", "band_trim" };
		active.assign(generalAudio, generalAudio + sizeof(generalAudio) / sizeof(generalAudio[0]));
	}
	std::set<std::string> visibleWidgetIds;
	visibleWidgetIds.insert("appearance_tab");
	visibleWidgetIds.insert("algorithm_tab");
	visibleWidgetIds.insert("audio_tab");
	visibleWidgetIds.insert("settings_close");
	if (settingsTab_ == 2) visibleWidgetIds.insert("audio_retry");
	const float rowHeight = 48.0f;
	const float contentBottom = settingsContentBounds_.y + settingsContentBounds_.height;
	const float totalHeight = static_cast<float>(active.size()) * rowHeight;
	settingsScrollOffset_ = std::max(0.0f, std::min(settingsScrollOffset_, std::max(0.0f, totalHeight - settingsContentBounds_.height)));
	for (std::size_t i = 0; i < active.size(); ++i)
	{
		const float rowY = settingsContentBounds_.y + static_cast<float>(i) * rowHeight - settingsScrollOffset_;
		const bool visible = rowY >= settingsContentBounds_.y - 0.5f && rowY + rowHeight <= contentBottom + 0.5f;
		if (visible) visibleWidgetIds.insert(active[i]);
		UiWidget* widget = settingsUi_.find(active[i]);
		if (!widget) continue;
		if (widget->kind() == UiWidgetKind::ComboBox)
			widget->setBounds(UiRect(x, rowY + 17.0f, width, 27.0f));
		else if (widget->kind() == UiWidgetKind::Button)
			widget->setBounds(UiRect(x, rowY + 8.0f, width, 28.0f));
		else
			widget->setBounds(UiRect(x, rowY, width, 43.0f));
		if (active[i] == "band_selector")
			widget->setPopupAbove(widget->bounds().y + widget->bounds().height * 9.0f >
				settingsPanelBounds_.y + settingsPanelBounds_.height - 46.0f);
	}
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		const std::string& id = settingsUi_.widgets()[i]->id();
		settingsUi_.setWidgetVisible(id, visibleWidgetIds.find(id) != visibleWidgetIds.end());
	}
	settingsUi_.find("settings_close")->setBounds(UiRect(settingsPanelBounds_.x + settingsPanelBounds_.width - 92.0f,
		footerY, 76.0f, 26.0f));
	settingsUi_.find("audio_retry")->setBounds(UiRect(x, footerY, std::min(190.0f, width - 90.0f), 26.0f));
}

void ControlPanel::updateAudioControls()
{
	const SoundEngine::Parameters& a = settings_.audio;
	auto slider = [this](const char* id, float value) { settingsUi_.find(id)->setSliderValue(value); };
	auto selection = [this](const char* id, int value) { settingsUi_.find(id)->setSelectedIndex(value); };
	selection("audio_enabled", a.enabled ? 1 : 0);
	slider("master", a.masterLevel); slider("bed", a.bedLevel); slider("harmonic_exponent", a.bedHarmonicExponent);
	selection("sub300", a.includeSub300HzBed ? 1 : 0); selection("comb_enabled", a.bedComb.enabled ? 1 : 0);
	selection("comb_phase", a.bedComb.applyPhase ? 1 : 0); slider("comb_delay", a.bedComb.delayMicroseconds);
	slider("comb_phase_offset", a.bedComb.phaseOffsetDegrees); slider("comb_wet_low", a.bedComb.wetAtLowFrequency);
	slider("comb_wet_high", a.bedComb.wetAtHighFrequency); slider("comb_fade_start", a.bedComb.fadeStartHz);
	slider("comb_fade_end", a.bedComb.fadeEndHz); slider("comb_exponent", a.bedComb.fadeExponent);
	slider("tau_below", a.tauBelowBins); slider("tau_above", a.tauAboveBins);
	slider("chorus_mix", a.chorusMix); slider("chorus_rate", a.chorusRateHz); slider("chorus_depth", a.chorusDepthMs);
	selection("chorus_weighting", static_cast<int>(a.chorusWeighting)); slider("attack", a.attackMs); slider("release", a.releaseMs);
	selection("mapping", static_cast<int>(a.mappingMode)); selection("spread", static_cast<int>(a.comparisonSpreadMode));
	selection("band_selector", selectedBand_);
	settingsUi_.find("band_trim")->setActionIndex(selectedBand_);
	slider("band_trim", a.bandTrimDb[static_cast<std::size_t>(selectedBand_)]);
}

void ControlPanel::updateAlgorithmAudioControls(const sortinglogic::AlgorithmDescriptor& algorithm)
{
	const AlgorithmAudioLevels inherited = familyAudioLevels(settings_, algorithm.familyId);
	const AlgorithmAudioLevels offsets = variantAudioLevels(settings_, algorithm.familyId, algorithm.variantId);
	for (int channel = 0; channel < static_cast<int>(AlgorithmAudioChannel::Count); ++channel)
	{
		const AlgorithmAudioChannel level = static_cast<AlgorithmAudioChannel>(channel);
		const std::size_t index = static_cast<std::size_t>(level);
		const std::string id = std::string("algorithm_audio_") + (channel < 10 ? "0" : "") + std::to_string(channel);
		UiWidget* widget = settingsUi_.find(id);
		if (!widget) continue;
		widget->setSliderValue(offsets.decibels[index]);
		widget->setAction(AppAction(AppActionKind::SetVariantAudioLevel, algorithm.familyId, algorithm.variantId,
			offsets.decibels[index], channel));
		std::ostringstream caption;
		caption << algorithmAudioChannelName(level) << " (family " << std::fixed << std::setprecision(1)
			<< std::showpos << inherited.decibels[index] << " dB)";
		settingsCaptions_[id] = caption.str();
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(id);
		if (label != labels_.end()) label->second->setText(caption.str(), "Courier New Bold", 12, SDL_Color{ 238, 246, 249, 255 });
	}
	settingsUi_.find("algorithm_audio_reset")->setAction(AppAction(AppActionKind::ResetVariantAudioLevels,
		algorithm.familyId, algorithm.variantId));
}

void ControlPanel::updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot)
{
	const bool busy = snapshot.buildingTrace || snapshot.playing;
	const char* setupIds[] = { "shuffle", "insta_sort", "insta_invert", "algorithm", "direction", "run",
		"bars_down_coarse", "bars_down_fine", "bars_up_fine", "bars_up_coarse", "time_down", "time_up" };
	for (std::size_t i = 0; i < sizeof(setupIds) / sizeof(setupIds[0]); ++i)
	{
		mainUi_.setWidgetEnabled(setupIds[i], !busy);
	}
	mainUi_.setWidgetEnabled("run", !busy && snapshot.runCompatible);
	UiWidget* algorithm = mainUi_.find("algorithm");
	if (algorithm) algorithm->setLabel(algorithmLabel(snapshot.algorithm));
	mainUi_.find("play")->setLabel(snapshot.playing ? "Pause" : "Play");
	mainUi_.find("bars_value")->setLabel(std::to_string(snapshot.itemCount) + " bars");
	mainUi_.find("time_value")->setLabel(std::to_string(static_cast<int>(snapshot.durationSeconds)) + " sec");
	mainUi_.find("direction")->setSelectedIndex(snapshot.direction == sortinglogic::SortDirection::Descending ? 1 : 0);
	settingsUi_.find("palette")->setSelectedIndex(paletteIndex(settings_.visual.palette));
	settingsUi_.find("spacing")->setSelectedIndex(spacingIndex(settings_.visual.spacing));
	settingsUi_.find("domain")->setSelectedIndex(static_cast<int>(settings_.simulation.domain));
	settingsUi_.find("pattern")->setSelectedIndex(static_cast<int>(settings_.simulation.pattern));
	settingsUi_.setWidgetEnabled("domain", !busy);
	settingsUi_.setWidgetEnabled("pattern", !busy);
	const std::vector<sortinglogic::AlgorithmDescriptor>& optionCatalog = sortinglogic::algorithmCatalog();
	for (std::size_t algorithmIndex = 0; algorithmIndex < optionCatalog.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithmDescriptor = optionCatalog[algorithmIndex];
		std::set<std::string> groups;
		for (std::size_t optionIndex = 0; optionIndex < algorithmDescriptor.supportedOptions.size(); ++optionIndex)
		{
			const std::string groupId(algorithmDescriptor.supportedOptions[optionIndex].groupId);
			if (!groups.insert(groupId).second) continue;
			const std::string id = algorithmOptionWidgetId(algorithmDescriptor, groupId);
			UiWidget* widget = settingsUi_.find(id);
			if (!widget) continue;
			const std::string stableId = algorithmOptionStableId(settings_, algorithmDescriptor, groupId);
			int selectedIndex = 0;
			for (std::size_t candidateIndex = 0, visibleIndex = 0;
				candidateIndex < algorithmDescriptor.supportedOptions.size(); ++candidateIndex)
			{
				const sortinglogic::AlgorithmOptionDescriptor& candidate = algorithmDescriptor.supportedOptions[candidateIndex];
				if (groupId != candidate.groupId) continue;
				if (stableId == candidate.stableId) selectedIndex = static_cast<int>(visibleIndex);
				++visibleIndex;
			}
			widget->setSelectedIndex(selectedIndex);
			widget->setAction(AppAction(AppActionKind::SetAlgorithmOption, algorithmDescriptor.familyId,
				algorithmDescriptor.variantId, groupId));
			settingsUi_.setWidgetEnabled(id, !busy && snapshot.algorithm == algorithmDescriptor.id);
		}
	}
	for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
		pattern <= static_cast<int>(sortinglogic::InputPattern::OrganPipe); ++pattern)
		settingsUi_.find("pattern")->setOptionEnabled(static_cast<std::size_t>(pattern),
			sortinglogic::inputPatternCompatible(snapshot.domain, static_cast<sortinglogic::InputPattern>(pattern)));
	updateAudioControls();
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(snapshot.algorithm);
	if (descriptor) updateAlgorithmAudioControls(*descriptor);
	layoutSettingsControls();
}

void ControlPanel::syncLabels(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette)
{
	const SDL_Color textColor = { 255, 255, 255, 255 };
	for (std::map<std::string, std::unique_ptr<TextLabel> >::iterator i = labels_.begin(); i != labels_.end(); ++i)
	{
		const UiWidget* widget = mainUi_.find(i->first);
		if (!widget) widget = settingsUi_.find(i->first);
		if (widget)
		{
			std::string text = widget->label();
			if (widget->kind() == UiWidgetKind::Slider)
			{
				std::ostringstream value;
				value << settingsCaptions_[i->first] << "  " << std::fixed
					<< std::setprecision(widget->step() < 0.01f ? 3 : (widget->step() < 1.0f ? 2 : 1));
				if (i->first.find("algorithm_audio_") == 0)
					value << std::showpos << widget->value() << " dB";
				else value << widget->value();
				text = value.str();
			}
			i->second->setText(text, "Courier New Bold", widget->kind() == UiWidgetKind::ComboBox ? 14 : 12, textColor);
		}
	}
	title_->setText("Sorting Simulation | deterministic event trace", "Courier New Bold", 18, textColor);
	settingsTitle_->setText(settingsTabTitle(settingsTab_, settings_), "Courier New Bold", 17, textColor);
	const std::string direction = snapshot.direction == sortinglogic::SortDirection::Ascending ? "Ascending" : "Descending";
	std::ostringstream detailText;
	detailText << sortinglogic::algorithmDisplayName(snapshot.algorithm) << "  |  " << direction << "  |  " << snapshot.itemCount << " bars  |  ";
	detailText << sortinglogic::inputDomainName(snapshot.domain) << " / " << sortinglogic::inputPatternName(snapshot.pattern) << "  |  ";
	const sortinglogic::AlgorithmDescriptor* facts = sortinglogic::algorithmDescriptor(snapshot.algorithm);
	if (facts) detailText << (facts->stable ? "Stable" : "Unstable") << (facts->inPlace ? ", in-place" : ", auxiliary space")
		<< "  |  " << facts->timeComplexity << " time, " << facts->spaceComplexity << " space  |  ";
	if (snapshot.algorithm == sortinglogic::SortAlgorithm::Shell)
		detailText << "Gap sequence: " << sortinglogic::shellGapSequenceName(snapshot.shellGapSequence) << "  |  ";
	if (snapshot.hasTrace) detailText << "Event " << snapshot.cursor << "/" << snapshot.eventCount;
	else if (snapshot.buildingTrace) detailText << "Building trace";
	else detailText << "No trace";
	detailText << "  |  " << static_cast<int>(snapshot.elapsedSeconds) << "/" << static_cast<int>(snapshot.durationSeconds) << " sec";
	if (snapshot.inputSeed) detailText << "  |  seed " << snapshot.inputSeed;
	const std::string nextDetail = detailText.str();
	const Uint32 now = SDL_GetTicks();
	const bool allowLiveTextUpdate = !snapshot.playing || now - lastDetailUpdate_ >= 200;
	if (nextDetail != displayedDetail_ && allowLiveTextUpdate)
	{
		detail_->setText(nextDetail, "Courier New Bold", 14, textColor);
		displayedDetail_ = nextDetail;
		lastDetailUpdate_ = now;
	}
	std::ostringstream statusText;
	if (!snapshot.runCompatible) statusText << snapshot.compatibilityMessage << " | ";
	if (snapshot.hasTrace)
	{
		statusText << "Cmp " << snapshot.metrics.comparisons << "  Swap " << snapshot.metrics.swaps
			<< "  Main " << snapshot.metrics.primaryWrites << "  Aux " << snapshot.metrics.auxiliaryItemWrites
			<< "/" << snapshot.metrics.auxiliaryCounterUpdates << "  Peak " << snapshot.metrics.peakAuxiliaryCapacity;
		if (facts)
			for (std::size_t metric = 0; metric < facts->metrics.size(); ++metric)
			{
				const std::map<std::string, std::uint64_t>::const_iterator value =
					snapshot.metrics.algorithm.find(facts->metrics[metric].key);
				if (value != snapshot.metrics.algorithm.end())
					statusText << "  " << facts->metrics[metric].displayName << " " << value->second;
			}
		statusText << "  |  " << snapshot.status;
	}
	else statusText << snapshot.status;
	const std::string statusValue = statusText.str();
	const std::string nextStatus = statusValue.size() > 220 ? statusValue.substr(0, 217) + "..." : statusValue;
	if (nextStatus != displayedStatus_ && allowLiveTextUpdate)
	{
		status_->setText(nextStatus, "Courier New Bold", 14, textColor);
		displayedStatus_ = nextStatus;
	}
	settingsTitle_->setText(settingsTabTitle(settingsTab_, settings_), "Courier New Bold", 17, textColor);
	const std::string saveStatus = displayedSaveStatus_.empty() ? "Preferences save automatically." : displayedSaveStatus_;
	settingsSaveStatus_->setText(saveStatus, "Courier New Bold", 12, textColor);
}

bool ControlPanel::handleEvent(const SDL_Event& event, AppAction& action)
{
	updateLayout();
	if (algorithmMenuOpen_)
		return handleAlgorithmMenuEvent(event, action);
	if (settingsOpen_ && event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if ((settingsTab_ == 1 || settingsTab_ == 2) && settingsContentBounds_.contains(static_cast<float>(mouseX), static_cast<float>(mouseY)))
		{
			settingsScrollOffset_ = settingsScrollOffsetAfterWheel(settingsScrollOffset_, event.wheel.y,
				48.0f, 36.0f * 48.0f, settingsContentBounds_.height);
			layoutSettingsControls();
		}
		return true;
	}
	if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		inRect(timelineBounds_, event.button.x, event.button.y) && settings_.visual.panelExpanded && !settingsOpen_)
	{
		draggingTimeline_ = true;
		action = AppAction(AppActionKind::SeekFraction, timelineFraction(static_cast<float>(event.button.x)));
		return true;
	}
	if (event.type == SDL_MOUSEMOTION && draggingTimeline_)
	{
		action = AppAction(AppActionKind::SeekFraction, timelineFraction(static_cast<float>(event.motion.x)));
		return true;
	}
	if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && draggingTimeline_)
	{
		draggingTimeline_ = false;
		action = AppAction(AppActionKind::SeekFraction, timelineFraction(static_cast<float>(event.button.x)));
		return true;
	}
	UiInputEvent input(UiInputType::PointerMove);
	bool translated = true;
	if (event.type == SDL_MOUSEMOTION)
		input = UiInputEvent(UiInputType::PointerMove, static_cast<float>(event.motion.x), static_cast<float>(event.motion.y));
	else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
		input = UiInputEvent(UiInputType::PointerDown, static_cast<float>(event.button.x), static_cast<float>(event.button.y));
	else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
		input = UiInputEvent(UiInputType::PointerUp, static_cast<float>(event.button.x), static_cast<float>(event.button.y));
	else if (event.type == SDL_KEYDOWN)
	{
		UiKey key = UiKey::None;
		switch (event.key.keysym.sym)
		{
		case SDLK_TAB: key = UiKey::Tab; break;
		case SDLK_RETURN: case SDLK_KP_ENTER: key = UiKey::Enter; break;
		case SDLK_SPACE: key = UiKey::Space; break;
		case SDLK_ESCAPE: key = UiKey::Escape; break;
		case SDLK_UP: key = UiKey::Up; break;
		case SDLK_DOWN: key = UiKey::Down; break;
		case SDLK_LEFT: key = UiKey::Left; break;
		case SDLK_RIGHT: key = UiKey::Right; break;
		default: translated = false; break;
		}
		input = UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, key,
			(event.key.keysym.mod & KMOD_SHIFT) != 0);
	}
	else
		translated = false;
	if (!translated)
		return settingsOpen_;
	UiAction uiAction;
	const bool consumed = (settingsOpen_ ? settingsUi_ : mainUi_).handleEvent(input, uiAction);
	if (consumed && (uiAction.viewCommand != UiAction::ViewCommand::None || uiAction.appAction.kind != AppActionKind::None))
	{
		processAction(uiAction, action);
	}
	return consumed || settingsOpen_;
}

void ControlPanel::processAction(const UiAction& action, AppAction& output)
{
	if (action.viewCommand == UiAction::ViewCommand::OpenAlgorithmMenu)
	{
		openAlgorithmMenu();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::OpenSettings)
	{
		settingsOpen_ = true;
		settingsUi_.setInputCaptured(true);
		settingsUi_.setModalBounds(settingsPanelBounds_, UiAction::ViewCommand::CloseSettings);
		mainUi_.closeCombo();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::CloseSettings)
	{
		closeSettings();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowAlgorithmSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowAudioSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowGeneralAudioSettings)
	{
		settingsTab_ = action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings ? 0 :
			action.viewCommand == UiAction::ViewCommand::ShowAlgorithmSettings ? 1 : 2;
		settingsScrollOffset_ = 0.0f;
		settingsUi_.closeCombo();
		layoutSettingsControls();
		return;
	}
	if (action.appAction.kind == AppActionKind::SelectAlgorithm)
	{
		const std::vector<sortinglogic::AlgorithmDescriptor>& catalog = sortinglogic::algorithmCatalog();
		if (action.appAction.selection >= 0 && static_cast<std::size_t>(action.appAction.selection) < catalog.size())
		{
			const sortinglogic::AlgorithmDescriptor& descriptor = catalog[static_cast<std::size_t>(action.appAction.selection)];
			output = AppAction(AppActionKind::SelectAlgorithm, descriptor.familyId, descriptor.variantId);
		}
		return;
	}
	if (action.appAction.kind == AppActionKind::SetShellGapSequence)
	{
		output = action.appAction;
		output.selection = shellGapValue(action.appAction.selection);
		return;
	}
	if (action.appAction.kind == AppActionKind::SetAlgorithmOption)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptorById(
			action.appAction.familyId, action.appAction.variantId);
		if (descriptor)
		{
			int optionIndex = 0;
			for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
			{
				const sortinglogic::AlgorithmOptionDescriptor& option = descriptor->supportedOptions[i];
				if (action.appAction.optionGroupId != option.groupId) continue;
				if (optionIndex == action.appAction.selection)
				{
					output = AppAction(AppActionKind::SetAlgorithmOption, descriptor->familyId, descriptor->variantId,
						action.appAction.optionGroupId, option.stableId, option.value);
					break;
				}
				++optionIndex;
			}
		}
		return;
	}
	if (action.appAction.kind == AppActionKind::SelectAudioBand)
	{
		selectedBand_ = std::max(0, std::min(static_cast<int>(SoundEngine::BandCount) - 1, action.appAction.selection));
		updateAudioControls();
		layoutSettingsControls();
	}
	output = action.appAction;
}

float ControlPanel::timelineFraction(float x) const
{
	if (timelineBounds_.width <= 0.0f) return 0.0f;
	return std::max(0.0f, std::min(1.0f, (x - timelineBounds_.x) / timelineBounds_.width));
}

void ControlPanel::render(const sortinglogic::RenderSnapshot& snapshot, const std::string& persistenceStatus,
	const std::string& audioStatus)
{
	if (!isReady()) return;
	displayedSaveStatus_ = persistenceStatus;
	updateLayout();
	updateWidgetStates(snapshot);
	layoutAlgorithmMenu();
	const ThemePalette& palette = paletteFor(settings_.visual.palette);
	syncLabels(snapshot, palette);
	drawMainPanel(snapshot, palette);
	if (settingsOpen_) drawSettingsPanel(palette, audioStatus);
	if (algorithmMenuOpen_) drawAlgorithmMenu(palette);
}

void ControlPanel::drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette)
{
	submitBox(headerBounds_, palette.panel, PanelMaterial, UiLayer);
	drawWidget(*mainUi_.find("panel_toggle"), palette, UiLayer);
	drawWidget(*mainUi_.find("settings"), palette, UiLayer);
	UiRect titleBounds(48.0f, 5.0f, std::max(1.0f, static_cast<float>(width_) - 102.0f), 22.0f);
	drawText(*title_, titleBounds, 17.0f, palette.text, TextLayer);
	if (!settings_.visual.panelExpanded)
	{
		std::ostringstream compact;
		compact << sortinglogic::algorithmDisplayName(snapshot.algorithm) << " | "
			<< (snapshot.direction == sortinglogic::SortDirection::Ascending ? "Ascending" : "Descending") << " | ";
		if (snapshot.buildingTrace) compact << "Building trace";
		else if (snapshot.playing) compact << "Playing";
		else if (snapshot.hasTrace) compact << "Event " << snapshot.cursor << "/" << snapshot.eventCount;
		else compact << snapshot.itemCount << " bars | Ready";
		status_->setText(compact.str(), "Courier New Bold", 12, SDL_Color{ 255, 255, 255, 255 });
		drawText(*status_, UiRect(50.0f, 25.0f, std::max(1.0f, static_cast<float>(width_) - 104.0f), 14.0f),
			12.0f, palette.mutedText, TextLayer);
		return;
	}
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		if (widget.id() != "panel_toggle" && widget.id() != "settings" && widget.visible())
		{
			drawWidget(widget, palette, UiLayer);
		}
	}
	if (snapshot.hasTrace || snapshot.buildingTrace)
	{
		const glm::vec4 rail = palette.timelineRail;
		submitBox(timelineBounds_, rail, TimelineMaterial, UiLayer);
		const float fraction = snapshot.eventCount == 0 ? 0.0f :
			static_cast<float>(snapshot.cursor) / static_cast<float>(snapshot.eventCount);
		submitBox(UiRect(timelineBounds_.x, timelineBounds_.y, timelineBounds_.width * fraction, timelineBounds_.height), palette.accent, AccentMaterial, layerOffset(UiLayer, 1));
		submitBox(UiRect(timelineBounds_.x + std::max(0.0f, timelineBounds_.width * fraction - 4.0f), timelineBounds_.y - 3.0f, 8.0f, timelineBounds_.height + 6.0f), palette.timelineThumb, MaterialId::TimelineThumb, layerOffset(UiLayer, 1));
	}
	const float detailY = timelineBounds_.y + 14.0f;
	drawText(*detail_, UiRect(16.0f, detailY, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.text, TextLayer);
	drawText(*status_, UiRect(16.0f, detailY + 19.0f, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.mutedText, TextLayer);
	// Open combo options are submitted last within the UI layer, preserving their painter order over the chart.
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
		if (mainUi_.widgets()[i]->visible() && mainUi_.widgets()[i]->expanded())
			drawWidget(*mainUi_.widgets()[i], palette, DropdownLayer, true);
}

void ControlPanel::drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus)
{
	submitBox(UiRect(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)), palette.overlayDim, MaterialId::ModalDim, DimLayer);
	submitBox(settingsPanelBounds_, palette.panel, PanelMaterial, SettingsLayer);
	submitBox(UiRect(settingsPanelBounds_.x, settingsPanelBounds_.y, settingsPanelBounds_.width, 2.0f), palette.accent, AccentMaterial, RenderLayer::SettingsAccent);
	drawText(*settingsTitle_, UiRect(settingsPanelBounds_.x + 16.0f, settingsPanelBounds_.y + 13.0f, settingsPanelBounds_.width - 32.0f, 28.0f), 17.0f, palette.text, SettingsTextLayer);
	const UiWidget& appearanceTab = *settingsUi_.find("appearance_tab");
	const UiWidget& algorithmTab = *settingsUi_.find("algorithm_tab");
	const UiWidget& audioTab = *settingsUi_.find("audio_tab");
	const RenderLayer controlLayer = RenderLayer::SettingsControls;
	const UiWidget* tabs[] = { &appearanceTab, &algorithmTab, &audioTab };
	for (int tab = 0; tab < 3; ++tab)
	{
		submitBox(tabs[tab]->bounds(), tab == settingsTab_ ? palette.accent : palette.button, ButtonMaterial, controlLayer);
		drawWidget(*tabs[tab], palette, controlLayer);
	}
	const UiRect activeTab = tabs[settingsTab_]->bounds();
	submitBox(UiRect(activeTab.x, activeTab.y + activeTab.height - 2.0f, activeTab.width, 2.0f),
		palette.accent, AccentMaterial, layerOffset(controlLayer, 1));
	submitBox(settingsContentBounds_, palette.background, PanelMaterial, SettingsLayer);
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *settingsUi_.widgets()[i];
		if (!widget.visible() || widget.id() == "appearance_tab" || widget.id() == "algorithm_tab" ||
			widget.id() == "audio_tab" || widget.id() == "settings_close" || widget.id() == "audio_retry") continue;
		drawSettingsControl(widget, palette);
	}
	drawWidget(*settingsUi_.find("settings_close"), palette, RenderLayer::SettingsControls);
	if (settingsTab_ == 2)
	{
		audioDeviceStatus_->setText(audioStatus, "Courier New Bold", 11, SDL_Color{ 238, 246, 249, 255 });
		drawText(*audioDeviceStatus_, UiRect(settingsPanelBounds_.x + 16.0f,
			settingsPanelBounds_.y + settingsPanelBounds_.height - 62.0f,
			settingsPanelBounds_.width - 130.0f, 16.0f), 11.0f, palette.mutedText, SettingsTextLayer);
		drawWidget(*settingsUi_.find("audio_retry"), palette, RenderLayer::SettingsControls);
	}
	else
		drawText(*settingsSaveStatus_, UiRect(settingsPanelBounds_.x + 16.0f,
			settingsPanelBounds_.y + settingsPanelBounds_.height - 58.0f,
			settingsPanelBounds_.width - 120.0f, 16.0f), 11.0f, palette.mutedText, SettingsTextLayer);
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
		if (settingsUi_.widgets()[i]->visible() && settingsUi_.widgets()[i]->expanded())
			drawWidget(*settingsUi_.widgets()[i], palette, RenderLayer::SettingsDropdown, true);
}

void ControlPanel::drawSettingsControl(const UiWidget& widget, const ThemePalette& palette)
{
	const ClipBounds clip(settingsContentBounds_.x, settingsContentBounds_.y,
		settingsContentBounds_.width, settingsContentBounds_.height);
	if (widget.kind() == UiWidgetKind::Button)
	{
		drawWidget(widget, palette, RenderLayer::SettingsControls);
		return;
	}
	if (widget.kind() == UiWidgetKind::ComboBox)
	{
		const std::string captionId = widget.id() + "#caption";
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator caption = labels_.find(captionId);
		if (caption != labels_.end())
			drawText(*caption->second, UiRect(widget.bounds().x, widget.bounds().y - 16.0f, widget.bounds().width, 15.0f),
				11.0f, palette.mutedText, SettingsTextLayer, clip);
		drawWidget(widget, palette, RenderLayer::SettingsControls);
		return;
	}
	if (widget.kind() != UiWidgetKind::Slider) return;
	std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
	if (label != labels_.end())
		drawText(*label->second, UiRect(widget.bounds().x, widget.bounds().y + 1.0f, widget.bounds().width, 16.0f),
			11.0f, palette.text, SettingsTextLayer, clip);
	const float fraction = widget.maximum() <= widget.minimum() ? 0.0f :
		(widget.value() - widget.minimum()) / (widget.maximum() - widget.minimum());
	const float railY = widget.bounds().y + 29.0f;
	const UiRect rail(widget.bounds().x + 2.0f, railY, std::max(1.0f, widget.bounds().width - 4.0f), 4.0f);
	submitBox(rail, palette.timelineRail, TimelineMaterial, RenderLayer::SettingsControls, clip);
	submitBox(UiRect(rail.x, rail.y, rail.width * fraction, rail.height), palette.accent, AccentMaterial, RenderLayer::SettingsControls, clip);
	const float knobX = rail.x + rail.width * fraction - 5.0f;
	submitBox(UiRect(knobX, rail.y - 3.0f, 10.0f, 10.0f), widget.focused() ? palette.held : palette.text,
		MaterialId::TimelineThumb, RenderLayer::SettingsControls, clip);
}

void ControlPanel::drawWidget(const UiWidget& widget, const ThemePalette& palette, RenderLayer layer, bool popup)
{
	const ClipBounds screenClip(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
	if (popup)
	{
		for (std::size_t option = 0; option < widget.options().size(); ++option)
		{
			const UiRect bounds = widget.optionBounds(option);
			if (bounds.width <= 0.0f || bounds.height <= 0.0f) continue;
			const bool selected = static_cast<int>(option) == widget.selectedIndex();
			submitBox(bounds, !widget.optionEnabled(option) ? palette.disabledButton : selected ? palette.accent : palette.button, PopupMaterial, layer, screenClip);
			std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id() + "#option" + std::to_string(option));
			if (label != labels_.end())
			{
				label->second->setText(widget.options()[option], "Courier New Bold", 14, SDL_Color{ 255, 255, 255, 255 });
				drawText(*label->second, UiRect(bounds.x + 2.0f, bounds.y, bounds.width - 4.0f, bounds.height), 13.0f,
					widget.optionEnabled(option) ? palette.text : palette.mutedText, layerOffset(layer, 1), screenClip);
			}
		}
		return;
	}
	if (!widget.visible()) return;
	glm::vec4 fill = widget.kind() == UiWidgetKind::ComboBox ? palette.button : palette.button;
	if (widget.id() == "bars_value" || widget.id() == "time_value") fill = palette.valueButton;
	if (widget.id() == "run") fill = palette.run;
	if (widget.id() == "play" && widget.label() == "Pause") fill = palette.playActive;
	if (widget.id() == "reset") fill = palette.reset;
	if (!widget.enabled()) fill = palette.disabledButton;
	else if (widget.pressed()) fill = glm::vec4(fill.r * 0.78f, fill.g * 0.78f, fill.b * 0.78f, fill.a);
	else if (widget.hovered()) fill = brighter(fill);
	if (widget.expanded()) fill = palette.accent;
	submitBox(widget.bounds(), fill, widget.kind() == UiWidgetKind::ComboBox ? ValueMaterial : ButtonMaterial, layer, screenClip);
	if (widget.focused())
		submitBox(UiRect(widget.bounds().x, widget.bounds().y, widget.bounds().width, 2.0f), palette.held, AccentMaterial, layerOffset(layer, 1), screenClip);
	if (widget.id() == "panel_toggle")
	{
		const float x = widget.bounds().x;
		const float y = widget.bounds().y;
		const float pixel = 3.0f;
		if (settings_.visual.panelExpanded)
		{
			submitBox(UiRect(x + 7, y + 8, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 18, y + 8, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 10, y + 11, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 15, y + 11, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 12.5f, y + 14, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
		}
		else
		{
			submitBox(UiRect(x + 8, y + 6, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 11, y + 9, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 14, y + 12, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 11, y + 15, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(x + 8, y + 18, pixel, pixel), palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
		}
		return;
	}
	if (widget.id() == "settings")
	{
		const float x = widget.bounds().x;
		const float y = widget.bounds().y;
		const float pixel = 4.0f;
		// Center the 18x18 pixel-art cog against the button's current bounds.
		const float cx = x + widget.bounds().width * 0.5f - 6.0f;
		const float cy = y + widget.bounds().height * 0.5f - 6.0f;
		const glm::vec4 color = settingsOpen_ ? palette.accent : palette.text;
		submitBox(UiRect(cx + 2, cy + 2, 8, 8), color, MaterialId::Gear, layerOffset(layer, 2), screenClip);
		submitBox(UiRect(cx + 4, cy + 4, 4, 4), palette.button, MaterialId::GearCutout, layerOffset(layer, 3), screenClip);
		submitBox(UiRect(cx + 4, cy - 3, pixel, pixel), color, MaterialId::Gear, layerOffset(layer, 2), screenClip);
		submitBox(UiRect(cx + 4, cy + 11, pixel, pixel), color, MaterialId::Gear, layerOffset(layer, 2), screenClip);
		submitBox(UiRect(cx - 3, cy + 4, pixel, pixel), color, MaterialId::Gear, layerOffset(layer, 2), screenClip);
		submitBox(UiRect(cx + 11, cy + 4, pixel, pixel), color, MaterialId::Gear, layerOffset(layer, 2), screenClip);
		return;
	}
	if (widget.id() == "algorithm")
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(
			static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
		if (descriptor)
		{
			const std::string familyKey("#algorithm_family_button");
			const std::string variantKey("#algorithm_variant_button");
			if (!labels_[familyKey]) labels_[familyKey].reset(new TextLabel("", "Courier New Bold", 10, SDL_Color{ 238, 246, 249, 255 }));
			if (!labels_[variantKey]) labels_[variantKey].reset(new TextLabel("", "Courier New Bold", 10, SDL_Color{ 238, 246, 249, 255 }));
			labels_[familyKey]->setText(compactFamilyName(descriptor->familyDisplayName), "Courier New Bold", 10, SDL_Color{ 238, 246, 249, 255 });
			labels_[variantKey]->setText(compactVariantName(descriptor->displayName), "Courier New Bold", 10, SDL_Color{ 238, 246, 249, 255 });
			drawText(*labels_[familyKey], UiRect(widget.bounds().x + 2.0f, widget.bounds().y + 1.0f,
				widget.bounds().width - 4.0f, widget.bounds().height * 0.48f), 10.0f, palette.text, layerOffset(layer, 2), screenClip);
			drawText(*labels_[variantKey], UiRect(widget.bounds().x + 2.0f, widget.bounds().y + widget.bounds().height * 0.48f,
				widget.bounds().width - 4.0f, widget.bounds().height * 0.48f), 10.0f, palette.text, layerOffset(layer, 2), screenClip);
			return;
		}
	}
	std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
	if (label != labels_.end())
		drawText(*label->second, widget.bounds(), widget.kind() == UiWidgetKind::ComboBox ? 13.0f : 14.0f,
			palette.text, layerOffset(layer, 2), screenClip);
}

void ControlPanel::drawText(TextLabel& label, const UiRect& bounds, float targetHeight, const glm::vec4& tint, RenderLayer layer, const ClipBounds& clip)
{
	const glm::vec2 dimensions = label.getTextureDimensions();
	if (label.getTextureId() == 0 || dimensions.x <= 0.0f || dimensions.y <= 0.0f || bounds.width <= 0.0f || bounds.height <= 0.0f)
		return;
	float height = std::min(targetHeight, std::max(1.0f, bounds.height - 2.0f));
	float width = dimensions.x * height / dimensions.y;
	const float maxWidth = std::max(1.0f, bounds.width - 8.0f);
	if (width > maxWidth)
	{
		const float factor = maxWidth / width;
		width *= factor;
		height *= factor;
	}
	const glm::vec2 position(bounds.x + (bounds.width - width) * 0.5f, bounds.y + (bounds.height - height) * 0.5f);
	renderer_.submit(QuadInstance(position, glm::vec2(width, height),
		RenderDescriptor(tint, label.getTextureId(), MaterialId::Text), layer, clip));
}

void ControlPanel::submitBox(const UiRect& bounds, const glm::vec4& color, MaterialId material, RenderLayer layer, const ClipBounds& clip)
{
	if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
	renderer_.submit(QuadInstance(glm::vec2(bounds.x, bounds.y), glm::vec2(bounds.width, bounds.height),
		RenderDescriptor(color, 0, material), layer, clip));
}

void ControlPanel::closeSettings()
{
	settingsOpen_ = false;
	settingsUi_.setInputCaptured(false);
	settingsUi_.closeCombo();
}

void ControlPanel::openAlgorithmMenu()
{
	if (settingsOpen_) return;
	const sortinglogic::AlgorithmDescriptor* current = sortinglogic::algorithmDescriptor(
		static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
	algorithmMenuOpen_ = true;
	algorithmMenuVariantPane_ = false;
	algorithmSearchFocused_ = true;
	algorithmSearch_.clear();
	algorithmMenuFamily_ = current ? current->familyId : sortinglogic::FamilyId();
	algorithmFamilySelection_ = 0;
	algorithmVariantSelection_ = 0;
	algorithmFamilyScroll_ = 0;
	algorithmVariantScroll_ = 0;
	mainUi_.closeCombo();
	SDL_StartTextInput();
	layoutAlgorithmMenu();
}

void ControlPanel::closeAlgorithmMenu()
{
	algorithmMenuOpen_ = false;
	algorithmMenuVariantPane_ = false;
	algorithmSearchFocused_ = false;
	SDL_StopTextInput();
}

void ControlPanel::layoutAlgorithmMenu()
{
	if (!algorithmMenuOpen_ || width_ <= 0 || height_ <= 0) return;
	const float menuWidth = std::min(700.0f, std::max(1.0f, static_cast<float>(width_) - 24.0f));
	const float menuHeight = std::min(520.0f, std::max(180.0f, static_cast<float>(height_) - 24.0f));
	const UiRect anchor = mainUi_.find("algorithm")->bounds();
	float left = std::max(12.0f, std::min(static_cast<float>(width_) - menuWidth - 12.0f, anchor.x));
	float top = anchor.y + anchor.height + 6.0f;
	if (top + menuHeight > static_cast<float>(height_) - 12.0f)
		top = std::max(12.0f, anchor.y - menuHeight - 6.0f);
	algorithmMenuBounds_ = UiRect(left, top, menuWidth, menuHeight);
	algorithmSearchBounds_ = UiRect(left + 12.0f, top + 12.0f, menuWidth - 24.0f, 30.0f);
	const float paneTop = top + 52.0f;
	const float paneHeight = std::max(64.0f, menuHeight - 64.0f);
	const float familyWidth = (menuWidth - 36.0f) * 0.46f;
	algorithmFamilyBounds_ = UiRect(left + 12.0f, paneTop, familyWidth, paneHeight);
	algorithmVariantBounds_ = UiRect(algorithmFamilyBounds_.x + familyWidth + 12.0f, paneTop,
		menuWidth - familyWidth - 36.0f, paneHeight);
}

void ControlPanel::selectAlgorithmMenuVariant(const sortinglogic::AlgorithmDescriptor& algorithm, AppAction& action)
{
	action = AppAction(AppActionKind::SelectAlgorithm, algorithm.familyId, algorithm.variantId);
	closeAlgorithmMenu();
}

bool ControlPanel::handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action)
{
	layoutAlgorithmMenu();
	const std::vector<AlgorithmFamilyEntry> families = algorithmFamilies();
	std::vector<const AlgorithmFamilyEntry*> matches = visibleFamilies(families, algorithmSearch_);
	const AlgorithmFamilyEntry* selectedFamily = nullptr;
	for (std::size_t i = 0; i < matches.size(); ++i)
		if (matches[i]->id == algorithmMenuFamily_) { selectedFamily = matches[i]; algorithmFamilySelection_ = static_cast<int>(i); break; }
	if (!selectedFamily && !matches.empty())
	{
		algorithmFamilySelection_ = std::max(0, std::min(algorithmFamilySelection_, static_cast<int>(matches.size()) - 1));
		selectedFamily = matches[static_cast<std::size_t>(algorithmFamilySelection_)];
		algorithmMenuFamily_ = selectedFamily->id;
		algorithmVariantSelection_ = 0;
	}
	std::vector<const sortinglogic::AlgorithmDescriptor*> variants = visibleVariants(selectedFamily, algorithmSearch_);
	const int familyVisible = std::max(1, static_cast<int>(algorithmFamilyBounds_.height / 30.0f));
	const int variantVisible = std::max(1, static_cast<int>(algorithmVariantBounds_.height / 30.0f));
	algorithmFamilyScroll_ = std::max(0, std::min(algorithmFamilyScroll_, std::max(0, static_cast<int>(matches.size()) - familyVisible)));
	algorithmVariantScroll_ = std::max(0, std::min(algorithmVariantScroll_, std::max(0, static_cast<int>(variants.size()) - variantVisible)));
	auto keepSelectionVisible = [](int selection, int visible, int count, int& scroll)
	{
		if (selection < scroll) scroll = selection;
		else if (selection >= scroll + visible) scroll = selection - visible + 1;
		scroll = std::max(0, std::min(scroll, std::max(0, count - visible)));
	};
	if (event.type == SDL_TEXTINPUT && algorithmSearchFocused_)
	{
		if (algorithmSearch_.size() < 64) algorithmSearch_ += event.text.text;
		algorithmFamilyScroll_ = algorithmVariantScroll_ = 0;
		algorithmFamilySelection_ = algorithmVariantSelection_ = 0;
		return true;
	}
	if (event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if (algorithmFamilyBounds_.contains(static_cast<float>(mouseX), static_cast<float>(mouseY)))
			algorithmFamilyScroll_ = std::max(0, std::min(std::max(0, static_cast<int>(matches.size()) - familyVisible), algorithmFamilyScroll_ - event.wheel.y * 3));
		else if (algorithmVariantBounds_.contains(static_cast<float>(mouseX), static_cast<float>(mouseY)))
			algorithmVariantScroll_ = std::max(0, std::min(std::max(0, static_cast<int>(variants.size()) - variantVisible), algorithmVariantScroll_ - event.wheel.y * 3));
		return true;
	}
	if (event.type == SDL_KEYDOWN)
	{
		const SDL_Keycode key = event.key.keysym.sym;
		if (key == SDLK_ESCAPE)
		{
			if (algorithmMenuVariantPane_) { algorithmMenuVariantPane_ = false; algorithmSearchFocused_ = false; }
			else closeAlgorithmMenu();
			return true;
		}
		if (key == SDLK_BACKSPACE && algorithmSearchFocused_)
		{
			if (!algorithmSearch_.empty()) algorithmSearch_.erase(algorithmSearch_.size() - 1);
			algorithmFamilyScroll_ = algorithmVariantScroll_ = 0;
			return true;
		}
		if (key == SDLK_TAB)
		{
			algorithmSearchFocused_ = !algorithmSearchFocused_;
			return true;
		}
		if (key == SDLK_UP || key == SDLK_DOWN)
		{
			algorithmSearchFocused_ = false;
			int& selection = algorithmMenuVariantPane_ ? algorithmVariantSelection_ : algorithmFamilySelection_;
			const int count = algorithmMenuVariantPane_ ? static_cast<int>(variants.size()) : static_cast<int>(matches.size());
			if (count > 0)
				selection = std::max(0, std::min(count - 1, selection + (key == SDLK_DOWN ? 1 : -1)));
			if (algorithmMenuVariantPane_)
				keepSelectionVisible(selection, variantVisible, count, algorithmVariantScroll_);
			else
			{
				keepSelectionVisible(selection, familyVisible, count, algorithmFamilyScroll_);
				if (count > 0) algorithmMenuFamily_ = matches[static_cast<std::size_t>(selection)]->id;
			}
			return true;
		}
		if ((key == SDLK_RIGHT || key == SDLK_RETURN || key == SDLK_KP_ENTER) && !algorithmMenuVariantPane_)
		{
			algorithmSearchFocused_ = false;
			if (!matches.empty())
			{
				algorithmMenuFamily_ = matches[static_cast<std::size_t>(algorithmFamilySelection_)]->id;
				selectedFamily = matches[static_cast<std::size_t>(algorithmFamilySelection_)];
				variants = visibleVariants(selectedFamily, algorithmSearch_);
				algorithmVariantSelection_ = 0;
				for (std::size_t i = 0; i < variants.size(); ++i)
					if (static_cast<int>(variants[i]->id) == settings_.simulation.algorithm) algorithmVariantSelection_ = static_cast<int>(i);
				algorithmMenuVariantPane_ = true;
				algorithmVariantScroll_ = 0;
			}
			return true;
		}
		if (key == SDLK_LEFT)
		{
			algorithmMenuVariantPane_ = false;
			algorithmSearchFocused_ = false;
			return true;
		}
		if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && algorithmMenuVariantPane_ &&
			!variants.empty() && algorithmVariantSelection_ >= 0 && static_cast<std::size_t>(algorithmVariantSelection_) < variants.size())
		{
			selectAlgorithmMenuVariant(*variants[static_cast<std::size_t>(algorithmVariantSelection_)], action);
			return true;
		}
		return true;
	}
	if (event.type == SDL_MOUSEMOTION)
		return true;
	if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		const float px = static_cast<float>(event.button.x), py = static_cast<float>(event.button.y);
		if (!algorithmMenuBounds_.contains(px, py))
		{
			closeAlgorithmMenu();
			return true;
		}
		if (algorithmSearchBounds_.contains(px, py))
		{
			algorithmSearchFocused_ = true;
			return true;
		}
		algorithmSearchFocused_ = false;
		if (algorithmFamilyBounds_.contains(px, py) && !matches.empty())
		{
			const int row = algorithmFamilyScroll_ + static_cast<int>((py - algorithmFamilyBounds_.y) / 30.0f);
			if (row >= 0 && static_cast<std::size_t>(row) < matches.size())
			{
				algorithmFamilySelection_ = row;
				algorithmMenuFamily_ = matches[static_cast<std::size_t>(row)]->id;
				selectedFamily = matches[static_cast<std::size_t>(row)];
				variants = visibleVariants(selectedFamily, algorithmSearch_);
				algorithmVariantSelection_ = 0;
				for (std::size_t i = 0; i < variants.size(); ++i)
					if (static_cast<int>(variants[i]->id) == settings_.simulation.algorithm) algorithmVariantSelection_ = static_cast<int>(i);
				algorithmMenuVariantPane_ = true;
				algorithmVariantScroll_ = 0;
			}
			return true;
		}
		if (algorithmVariantBounds_.contains(px, py) && !variants.empty())
		{
			const int row = algorithmVariantScroll_ + static_cast<int>((py - algorithmVariantBounds_.y) / 30.0f);
			if (row >= 0 && static_cast<std::size_t>(row) < variants.size())
			{
				algorithmVariantSelection_ = row;
				selectAlgorithmMenuVariant(*variants[static_cast<std::size_t>(row)], action);
			}
			return true;
		}
		return true;
	}
	return true;
}

void ControlPanel::drawAlgorithmMenu(const ThemePalette& palette)
{
	const std::vector<AlgorithmFamilyEntry> families = algorithmFamilies();
	const std::vector<const AlgorithmFamilyEntry*> matches = visibleFamilies(families, algorithmSearch_);
	const AlgorithmFamilyEntry* selectedFamily = nullptr;
	for (std::size_t i = 0; i < matches.size(); ++i)
		if (matches[i]->id == algorithmMenuFamily_) { selectedFamily = matches[i]; break; }
	const std::vector<const sortinglogic::AlgorithmDescriptor*> variants = visibleVariants(selectedFamily, algorithmSearch_);
	const ClipBounds screen(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
	const ClipBounds familyClip(algorithmFamilyBounds_.x, algorithmFamilyBounds_.y, algorithmFamilyBounds_.width, algorithmFamilyBounds_.height);
	const ClipBounds variantClip(algorithmVariantBounds_.x, algorithmVariantBounds_.y, algorithmVariantBounds_.width, algorithmVariantBounds_.height);
	submitBox(UiRect(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)), palette.overlayDim, MaterialId::ModalDim, RenderLayer::ModalDim);
	submitBox(algorithmMenuBounds_, palette.panel, PanelMaterial, RenderLayer::SettingsPanel, screen);
	submitBox(UiRect(algorithmMenuBounds_.x, algorithmMenuBounds_.y, algorithmMenuBounds_.width, 2.0f), palette.accent, AccentMaterial, RenderLayer::SettingsAccent, screen);
	submitBox(algorithmSearchBounds_, algorithmSearchFocused_ ? palette.valueButton : palette.background, PopupMaterial, RenderLayer::SettingsDropdown, screen);
	if (!labels_["#algorithm_search"]) labels_["#algorithm_search"].reset(new TextLabel("", "Courier New Bold", 14, { 238, 246, 249, 255 }));
	TextLabel& searchLabel = *labels_["#algorithm_search"];
	searchLabel.setText(std::string("Search algorithms: ") + algorithmSearch_ + (algorithmSearchFocused_ ? "_" : ""),
		"Courier New Bold", 14, SDL_Color{ 238, 246, 249, 255 });
	drawText(searchLabel, algorithmSearchBounds_, 14.0f, palette.text, RenderLayer::SettingsDropdownText, screen);
	submitBox(algorithmFamilyBounds_, palette.background, PanelMaterial, RenderLayer::SettingsDropdown, screen);
	submitBox(algorithmVariantBounds_, palette.background, PanelMaterial, RenderLayer::SettingsDropdown, screen);
	const float rowHeight = 30.0f;
	const int familyRows = std::max(1, static_cast<int>(algorithmFamilyBounds_.height / rowHeight));
	for (int row = 0; row < familyRows && algorithmFamilyScroll_ + row < static_cast<int>(matches.size()); ++row)
	{
		const AlgorithmFamilyEntry* family = matches[static_cast<std::size_t>(algorithmFamilyScroll_ + row)];
		const UiRect bounds(algorithmFamilyBounds_.x, algorithmFamilyBounds_.y + row * rowHeight, algorithmFamilyBounds_.width, rowHeight);
		const bool selected = family->id == algorithmMenuFamily_;
		submitBox(bounds, selected ? palette.accent : palette.button, PopupMaterial, RenderLayer::SettingsDropdown, familyClip);
		const std::string key = "#menu_family_" + family->id;
		if (!labels_[key]) labels_[key].reset(new TextLabel(family->name, "Courier New Bold", 13, { 238, 246, 249, 255 }));
		labels_[key]->setText(family->name, "Courier New Bold", 13, SDL_Color{ 238, 246, 249, 255 });
			drawText(*labels_[key], UiRect(bounds.x + 6.0f, bounds.y, bounds.width - 18.0f, bounds.height), 12.0f,
			palette.text, RenderLayer::SettingsDropdownText, familyClip);
	}
	if (matches.empty())
	{
		if (!labels_["#algorithm_no_family"]) labels_["#algorithm_no_family"].reset(new TextLabel("No matching families", "Courier New Bold", 13, { 238, 246, 249, 255 }));
		drawText(*labels_["#algorithm_no_family"], algorithmFamilyBounds_, 13.0f, palette.mutedText, RenderLayer::SettingsDropdownText, familyClip);
	}
	if (selectedFamily)
	{
		const int variantRows = std::max(1, static_cast<int>(algorithmVariantBounds_.height / rowHeight));
		for (int row = 0; row < variantRows && algorithmVariantScroll_ + row < static_cast<int>(variants.size()); ++row)
		{
			const sortinglogic::AlgorithmDescriptor* variant = variants[static_cast<std::size_t>(algorithmVariantScroll_ + row)];
			const UiRect bounds(algorithmVariantBounds_.x, algorithmVariantBounds_.y + row * rowHeight, algorithmVariantBounds_.width, rowHeight);
			const bool selected = static_cast<int>(variant->id) == settings_.simulation.algorithm;
			const bool keyboardSelected = algorithmMenuVariantPane_ && algorithmVariantScroll_ + row == algorithmVariantSelection_;
			submitBox(bounds, keyboardSelected ? palette.accent : selected ? palette.accent : palette.button, PopupMaterial, RenderLayer::SettingsDropdown, variantClip);
			const std::string key = "#menu_variant_" + selectedFamily->id + "_" + variant->variantId;
			if (!labels_[key]) labels_[key].reset(new TextLabel(variant->displayName, "Courier New Bold", 13, { 238, 246, 249, 255 }));
			labels_[key]->setText(variant->displayName, "Courier New Bold", 13, SDL_Color{ 238, 246, 249, 255 });
			drawText(*labels_[key], UiRect(bounds.x + 8.0f, bounds.y, bounds.width - 16.0f, bounds.height), 12.0f,
				palette.text, RenderLayer::SettingsDropdownText, variantClip);
		}
		if (variants.empty())
		{
			if (!labels_["#algorithm_no_variant"]) labels_["#algorithm_no_variant"].reset(new TextLabel("No matching variants", "Courier New Bold", 13, { 238, 246, 249, 255 }));
			drawText(*labels_["#algorithm_no_variant"], algorithmVariantBounds_, 13.0f, palette.mutedText, RenderLayer::SettingsDropdownText, variantClip);
		}
	}
	else
	{
		if (!labels_["#algorithm_choose_family"]) labels_["#algorithm_choose_family"].reset(new TextLabel("Choose a family to see its variants", "Courier New Bold", 13, { 238, 246, 249, 255 }));
		drawText(*labels_["#algorithm_choose_family"], algorithmVariantBounds_, 13.0f, palette.mutedText, RenderLayer::SettingsDropdownText, variantClip);
	}
}
