#include "ControlPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>

#include "LayoutMetrics.h"
#include "SettingsScroll.h"
#include "WindowsFontCatalog.h"

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

	template <typename Consumer>
	class DispatchingUiPaintContext : public UiPaintContext
	{
	public:
		explicit DispatchingUiPaintContext(Consumer& consumer) : consumer_(consumer) {}
		void submit(const UiPaintCommand& command) override { consumer_(command); }
	private:
		Consumer& consumer_;
	};

	glm::vec4 toGlmColor(const UiColor& color)
	{
		return glm::vec4(color.r, color.g, color.b, color.a);
	}

	UiColor toUiColor(const glm::vec4& color)
	{
		return UiColor(color.r, color.g, color.b, color.a);
	}

	RenderLayer renderLayerForUiPaint(UiPaintLayer layer)
	{
		switch (layer)
		{
		case UiPaintLayer::Backdrop: return RenderLayer::ModalDim;
		case UiPaintLayer::Panel: return RenderLayer::SettingsPanel;
		case UiPaintLayer::Accent: return RenderLayer::SettingsAccent;
		case UiPaintLayer::Content: return RenderLayer::SettingsDropdown;
		case UiPaintLayer::Text: return RenderLayer::SettingsDropdownText;
		case UiPaintLayer::SettingsText: return RenderLayer::SettingsText;
		}
		return RenderLayer::SettingsDropdown;
	}

	MaterialId materialForUiPaint(UiPaintMaterial material)
	{
		switch (material)
		{
		case UiPaintMaterial::Panel: return PanelMaterial;
		case UiPaintMaterial::Popup: return PopupMaterial;
		case UiPaintMaterial::Accent: return AccentMaterial;
		case UiPaintMaterial::ModalDim: return MaterialId::ModalDim;
		case UiPaintMaterial::Solid: return PopupMaterial;
		}
		return PopupMaterial;
	}

	ClipBounds renderClip(const UiPaintCommand& command)
	{
		return command.hasClip ? ClipBounds(command.clip.x, command.clip.y, command.clip.width, command.clip.height) : ClipBounds();
	}

	bool translateAlgorithmBrowserEvent(const SDL_Event& event, UiInputEvent& input)
	{
		if (event.type == SDL_MOUSEMOTION)
			input = UiInputEvent(UiInputType::PointerMove, static_cast<float>(event.motion.x), static_cast<float>(event.motion.y));
		else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
			input = UiInputEvent(UiInputType::PointerDown, static_cast<float>(event.button.x), static_cast<float>(event.button.y));
		else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
			input = UiInputEvent(UiInputType::PointerUp, static_cast<float>(event.button.x), static_cast<float>(event.button.y));
		else if (event.type == SDL_MOUSEWHEEL)
		{
			int mouseX = 0, mouseY = 0;
			SDL_GetMouseState(&mouseX, &mouseY);
			input = UiInputEvent(UiInputType::Wheel, static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y);
		}
		else if (event.type == SDL_TEXTINPUT)
			input = UiInputEvent(UiInputType::TextInput, std::string(event.text.text));
		else if (event.type == SDL_KEYDOWN)
		{
			UiKey key = UiKey::None;
			switch (event.key.keysym.sym)
			{
			case SDLK_TAB: key = UiKey::Tab; break;
			case SDLK_RETURN: case SDLK_KP_ENTER: key = UiKey::Enter; break;
			case SDLK_BACKSPACE: key = UiKey::Backspace; break;
			case SDLK_ESCAPE: key = UiKey::Escape; break;
			case SDLK_UP: key = UiKey::Up; break;
			case SDLK_DOWN: key = UiKey::Down; break;
			case SDLK_LEFT: key = UiKey::Left; break;
			case SDLK_RIGHT: key = UiKey::Right; break;
			default: return false;
			}
			input = UiInputEvent(UiInputType::KeyDown, 0.0f, 0.0f, key,
				(event.key.keysym.mod & KMOD_SHIFT) != 0);
		}
		else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			input = UiInputEvent(UiInputType::FocusLost);
		else return false;
		return true;
	}

	std::vector<std::string> paletteNames()
	{
		std::vector<std::string> names;
		const std::vector<PaletteId>& catalog = paletteCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			names.push_back(paletteDisplayName(catalog[i]));
		return names;
	}

	std::vector<std::string> uiPresetNames()
	{
		std::vector<std::string> names;
		const std::vector<UiPresetId>& catalog = uiPresetCatalog();
		for (std::size_t i = 0; i < catalog.size(); ++i) names.push_back(uiPresetDefinition(catalog[i]).displayName);
		return names;
	}

	const WindowsFontCatalog& uiFontCatalog()
	{
		static const WindowsFontCatalog catalog;
		return catalog;
	}

	std::vector<std::string> fontFamilyNames()
	{
		std::vector<std::string> names;
		const std::vector<WindowsFontFace>& faces = uiFontCatalog().faces();
		for (std::size_t i = 0; i < faces.size(); ++i)
			if (std::find(names.begin(), names.end(), faces[i].family) == names.end()) names.push_back(faces[i].family);
		return names;
	}

	std::vector<std::string> fontStyleNames(const std::string& family)
	{
		std::vector<std::string> names;
		const std::vector<WindowsFontFace>& faces = uiFontCatalog().faces();
		for (std::size_t i = 0; i < faces.size(); ++i)
			if (faces[i].family == family && std::find(names.begin(), names.end(), faces[i].style) == names.end()) names.push_back(faces[i].style);
		return names;
	}

	std::string tokenDisplayValue(const UiTokenValue& value)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(3);
		switch (value.type)
		{
		case UiTokenType::Color:
			text << "R " << value.color.r << "  G " << value.color.g << "  B " << value.color.b << "  A " << value.color.a;
			break;
		case UiTokenType::Number: text << value.number; break;
		case UiTokenType::Boolean: text << (value.boolean ? "ON" : "OFF"); break;
		case UiTokenType::Choice: text << value.text; break;
		case UiTokenType::Font: text << value.font.family << " | " << value.font.style; break;
		}
		return text.str();
	}

	bool writeUtf8File(const std::wstring& path, const std::string& contents, std::string* error)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			if (error) *error = "Could not create the profile file.";
			return false;
		}
		DWORD written = 0;
		const BOOL ok = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size();
		CloseHandle(file);
		if (!ok && error) *error = "Could not write the full profile file.";
		return ok != FALSE;
	}

bool readUtf8File(const std::wstring& path, std::string& contents, std::string* error)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			if (error) *error = "Could not open the profile file.";
			return false;
		}
		LARGE_INTEGER size;
		if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 1024 * 1024)
		{
			CloseHandle(file);
			if (error) *error = "The profile file is too large or could not be read.";
			return false;
		}
		contents.assign(static_cast<std::size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		const BOOL ok = contents.empty() || ReadFile(file, &contents[0], static_cast<DWORD>(contents.size()), &read, nullptr);
		CloseHandle(file);
		if (!ok || read != contents.size())
		{
			if (error) *error = "Could not read the full profile file.";
			return false;
		}
		return true;
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
			pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
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

	AppAction reconstructedAction(ReconstructedAudioParameterId parameter, float value = 0.0f, int selection = 0)
	{
		return AppAction(AppActionKind::SetReconstructedAudioParameter, parameter, value, selection);
	}

	bool inRect(const UiRect& rect, int x, int y)
	{
		return rect.contains(static_cast<float>(x), static_cast<float>(y));
	}

	bool parseInlineInteger(const std::string& text, int& value)
	{
		if (text.empty()) return false;
		int parsed = 0;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			const unsigned char character = static_cast<unsigned char>(text[i]);
			if (character < '0' || character > '9') return false;
			const int digit = character - '0';
			if (parsed > (std::numeric_limits<int>::max() - digit) / 10) return false;
			parsed = parsed * 10 + digit;
		}
		value = parsed;
		return true;
	}

std::string formatDuration(float seconds)
{
	std::ostringstream value;
	value << std::setprecision(6) << seconds << " sec";
	return value.str();
}

	int paletteIndex(PaletteId id) { return static_cast<int>(id); }
	int spacingIndex(BarSpacingMode mode) { return static_cast<int>(mode); }

	const UiThemeCustomization& activeTheme(const UserPreferences& settings)
	{
		const std::map<UiPresetId, UiThemeCustomization>::const_iterator found = settings.appearance.themes.find(settings.appearance.activePreset);
		if (found != settings.appearance.themes.end()) return found->second;
		static const UiThemeCustomization fallback;
		return fallback;
	}

	float themeNumber(const UserPreferences& settings, const char* tokenId, float fallback)
	{
		const UiTokenValue value = effectiveUiTokenValue(settings.appearance.activePreset, activeTheme(settings), tokenId);
		return value.type == UiTokenType::Number ? static_cast<float>(value.number) : fallback;
	}

	bool themeBoolean(const UserPreferences& settings, const char* tokenId, bool fallback)
	{
		const UiPresetId preset = settings.appearance.activePreset;
		const UiTokenValue value = effectiveUiTokenValue(preset, activeTheme(settings), tokenId);
		return value.type == UiTokenType::Boolean ? value.boolean : fallback;
	}

	unsigned long long appearanceRevision(const UserPreferences& settings, UiInvalidation invalidation)
	{
		unsigned long long hash = 1469598103934665603ULL;
		const UiPresetId preset = settings.appearance.activePreset;
		const UiPresetDefinition& definition = uiPresetDefinition(preset);
		const UiThemeCustomization& theme = activeTheme(settings);
		for (std::size_t tokenIndex = 0; tokenIndex < definition.tokens.size(); ++tokenIndex)
		{
			const UiTokenDescriptor& descriptor = definition.tokens[tokenIndex];
			if ((static_cast<int>(descriptor.invalidation) & static_cast<int>(invalidation)) == 0) continue;
			const std::string encoded = encodeUiTokenValue(effectiveUiTokenValue(preset, theme, descriptor.stableId));
			for (std::size_t i = 0; i < descriptor.stableId.size(); ++i)
				hash = (hash ^ static_cast<unsigned char>(descriptor.stableId[i])) * 1099511628211ULL;
			for (std::size_t i = 0; i < encoded.size(); ++i)
				hash = (hash ^ static_cast<unsigned char>(encoded[i])) * 1099511628211ULL;
		}
		return hash;
	}

	unsigned long long layoutRevision(const UserPreferences& settings)
	{
		return appearanceRevision(settings, UiInvalidation::Layout);
	}

	unsigned long long fontMetricRevision(const UserPreferences& settings)
	{
		return appearanceRevision(settings, UiInvalidation::Typography);
	}

	std::map<std::string, UiColor> paletteColors(const ThemePalette& palette)
	{
		std::map<std::string, UiColor> colors;
		colors["color.background"] = UiColor(palette.background.r, palette.background.g, palette.background.b, palette.background.a);
		colors["color.bar"] = UiColor(palette.bar.r, palette.bar.g, palette.bar.b, palette.bar.a);
		colors["color.compared"] = UiColor(palette.compared.r, palette.compared.g, palette.compared.b, palette.compared.a);
		colors["color.held"] = UiColor(palette.held.r, palette.held.g, palette.held.b, palette.held.a);
		colors["color.shifted"] = UiColor(palette.shifted.r, palette.shifted.g, palette.shifted.b, palette.shifted.a);
		colors["color.write_target"] = UiColor(palette.writeTarget.r, palette.writeTarget.g, palette.writeTarget.b, palette.writeTarget.a);
		colors["color.gap"] = UiColor(palette.gap.r, palette.gap.g, palette.gap.b, palette.gap.a);
		colors["color.panel"] = UiColor(palette.panel.r, palette.panel.g, palette.panel.b, palette.panel.a);
		colors["color.button"] = UiColor(palette.button.r, palette.button.g, palette.button.b, palette.button.a);
		colors["color.disabled_button"] = UiColor(palette.disabledButton.r, palette.disabledButton.g, palette.disabledButton.b, palette.disabledButton.a);
		colors["color.value_button"] = UiColor(palette.valueButton.r, palette.valueButton.g, palette.valueButton.b, palette.valueButton.a);
		colors["color.accent"] = UiColor(palette.accent.r, palette.accent.g, palette.accent.b, palette.accent.a);
		colors["color.run"] = UiColor(palette.run.r, palette.run.g, palette.run.b, palette.run.a);
		colors["color.play_active"] = UiColor(palette.playActive.r, palette.playActive.g, palette.playActive.b, palette.playActive.a);
		colors["color.reset"] = UiColor(palette.reset.r, palette.reset.g, palette.reset.b, palette.reset.a);
		colors["color.text"] = UiColor(palette.text.r, palette.text.g, palette.text.b, palette.text.a);
		colors["color.muted_text"] = UiColor(palette.mutedText.r, palette.mutedText.g, palette.mutedText.b, palette.mutedText.a);
		colors["color.border"] = UiColor(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
		colors["color.timeline_rail"] = UiColor(palette.timelineRail.r, palette.timelineRail.g, palette.timelineRail.b, palette.timelineRail.a);
		colors["color.timeline_thumb"] = UiColor(palette.timelineThumb.r, palette.timelineThumb.g, palette.timelineThumb.b, palette.timelineThumb.a);
		return colors;
	}

	std::string algorithmLabel(sortinglogic::SortAlgorithm algorithm)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(algorithm);
		return descriptor ? descriptor->displayName : "Select algorithm";
	}

	std::string settingsTabTitle(SettingsPanelPage page, const UserPreferences& settings)
	{
		if (page == SettingsPanelPage::Appearance) return "Settings | Appearance";
		if (page == SettingsPanelPage::Algorithm)
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

	float measureLogicalTextWidth(const std::string& text, const ResolvedTextStyle& style)
	{
		const glm::vec2 dimensions = TextLabel::measureText(text, style);
		return dimensions.x * 96.0f / static_cast<float>(std::max(1, style.dpi));
	}
}

ControlPanel::ControlPanel(Window& window, Renderer2D& renderer, const UserPreferences& settings)
	: window_(window), renderer_(renderer), settings_(settings),
	mainUi_(), settingsUi_(), appearanceUi_(), settingsPanelModel_(), algorithmBrowser_(), algorithmBrowserView_(), settingsPanelView_(), screenModel_(), layoutResult_(), renderPreferences_(settings), labels_(), settingsCaptions_(), paletteCaption_(new TextLabel("Palette", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	spacingCaption_(new TextLabel("Bar spacing", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	title_(new TextLabel("Sorting Simulation | deterministic event trace", "Courier New Bold", 18, { 238, 246, 249, 255 })),
	detail_(new TextLabel("No trace", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	status_(new TextLabel("Ready.", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	collapsedSummary_(new TextLabel("Insertion Sort - 0%", "Courier New Bold", 20, { 255, 255, 255, 255 })),
	settingsTitle_(new TextLabel("Settings", "Courier New Bold", 17, { 238, 246, 249, 255 })),
	settingsSaveStatus_(new TextLabel("Preferences save automatically.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	audioDeviceStatus_(new TextLabel("Audio device is not initialized.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	displayedDetail_(), displayedStatus_(), displayedCollapsedSummary_(), displayedSaveStatus_(), lastDetailUpdate_(0),
	timelineBounds_(), timelineControlBounds_(), timelineLeftArrowBounds_(), timelineRightArrowBounds_(),
	headerBounds_(), gearBounds_(), settingsPanelBounds_(), settingsContentBounds_(), draggingTimeline_(false),
	settingsOpen_(false), appearanceEditorOpen_(false), appearanceWasSettingsOpen_(false), appearanceSearchFocused_(false), drawingAppearanceEditor_(false),
	algorithmProfileScrollViewport_(), algorithmProfileComplexityLayout_(measureLogicalTextWidth),
	algorithmProfileDescriptionLayout_(measureLogicalTextWidth),
	algorithmProfileScrollTrack_(), algorithmProfileScrollThumb_(),
	algorithmProfileAlgorithm_(), algorithmProfileLineHeight_(18.0f), algorithmProfileVisible_(false), draggingAlgorithmProfileScroll_(false),
	algorithmProfileDragStartY_(0.0f), algorithmProfileDragStartOffset_(0.0f),
	chartViewport_(), width_(0), height_(0), layoutKey_(), layoutCacheValid_(false), laidOutPreset_(settings.appearance.activePreset), fontFallbackActive_(false),
	inlineEditWidgetId_(), inlineEditBuffer_(), inlineEditReplaceOnNextDigit_(false), inlineEditAppearanceValue_(false),
	appearanceDraft_(settings.appearance), appearanceEditorModel_(), appearanceError_(), appearanceLayout_(),
	appearanceEditorTitle_(new TextLabel("Appearance customization", "Courier New Bold", 18, { 255, 255, 255, 255 })),
	appearanceTokenDescription_(new TextLabel("", "Courier New Bold", 13, { 255, 255, 255, 255 })),
	appearanceStatus_(new TextLabel("", "Courier New Bold", 12, { 255, 255, 255, 255 }))
{
	createWidgets();
	updateLayout();
}

ControlPanel::~ControlPanel()
{
	if (!inlineEditWidgetId_.empty() || algorithmBrowser_.open || appearanceSearchFocused_) SDL_StopTextInput();
}

bool ControlPanel::isReady() const
{
	return renderer_.isReady();
}

void ControlPanel::createWidgets()
{
	mainUi_.addButton("panel_toggle", ">>", AppAction(AppActionKind::TogglePanelExpanded));
	mainUi_.addButton("settings", "[=]", AppAction(), UiAction::ViewCommand::OpenSettings);
	mainUi_.addComboBox("menu_file", std::vector<std::string>{ "File", "Build Trace", "Generate & Play", "Reset" }, 0);
	mainUi_.addComboBox("menu_view", std::vector<std::string>{ "View", "Toggle Setup", "Borderless Fullscreen" }, 0);
	mainUi_.addComboBox("menu_tools", std::vector<std::string>{ "Tools", "Algorithm Browser...", "Settings...", "Appearance..." }, 0);
	mainUi_.find("menu_file")->setOptionEnabled(0, false);
	mainUi_.find("menu_view")->setOptionEnabled(0, false);
	mainUi_.find("menu_tools")->setOptionEnabled(0, false);
	mainUi_.find("menu_file")->setPopupAbove(false);
	mainUi_.find("menu_view")->setPopupAbove(false);
	mainUi_.find("menu_tools")->setPopupAbove(false);
	mainUi_.addButton("shuffle", "Shuffle", AppAction(AppActionKind::Shuffle));
	mainUi_.addButton("insta_sort", "Insta-sort", AppAction(AppActionKind::InstaSort));
	mainUi_.addButton("insta_invert", "Insta-invert", AppAction(AppActionKind::InstaInvert));
	mainUi_.addButton("algorithm", algorithmLabel(static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm)),
		AppAction(), UiAction::ViewCommand::OpenAlgorithmMenu);
	mainUi_.addComboBox("direction", std::vector<std::string>{ "Ascending", "Descending" }, settings_.simulation.descending ? 1 : 0,
		AppAction(AppActionKind::SetDirection));
	mainUi_.addButton("run", "Build Trace", AppAction(AppActionKind::BuildTrace));
	mainUi_.addButton("play", "Generate & Play", AppAction(AppActionKind::GenerateAndPlay));
	mainUi_.addButton("reset", "Reset", AppAction(AppActionKind::Reset));
	mainUi_.addButton("previous", "< Event", AppAction(AppActionKind::PreviousEvent));
	mainUi_.addButton("next", "Event >", AppAction(AppActionKind::NextEvent));
	mainUi_.addButton("bars_down", "Bars --", AppAction(AppActionKind::AdjustItemCount, -10.0f));
	mainUi_.addButton("bars_value", "80 bars");
	mainUi_.addButton("bars_up", "Bars ++", AppAction(AppActionKind::AdjustItemCount, 10.0f));
	mainUi_.addButton("time_down", "Time --", AppAction(AppActionKind::AdjustDuration, -1.0f));
	mainUi_.addButton("time_value", "10 sec");
	mainUi_.addButton("time_up", "Time ++", AppAction(AppActionKind::AdjustDuration, 1.0f));
	mainUi_.addComboBox("domain", domainNames(), static_cast<int>(settings_.simulation.domain), AppAction(AppActionKind::SetInputDomain));
	mainUi_.addComboBox("pattern", inputPatternNames(), static_cast<int>(settings_.simulation.pattern), AppAction(AppActionKind::SetInputPattern));
	mainUi_.addButton("seed_readout", "Seed");
	mainUi_.addButton("start_overlay", "Generate & Play", AppAction(AppActionKind::GenerateAndPlay));
	settingsUi_.addButton("appearance_tab", "Appearance", AppAction(), UiAction::ViewCommand::ShowAppearanceSettings);
	settingsUi_.addButton("algorithm_tab", "Algorithm", AppAction(), UiAction::ViewCommand::ShowAlgorithmSettings);
	settingsUi_.addButton("audio_tab", "General Audio", AppAction(), UiAction::ViewCommand::ShowGeneralAudioSettings);
	settingsUi_.addButton("audio_basic_tab", "Basic", AppAction(), UiAction::ViewCommand::ShowAudioBasic);
	settingsUi_.addButton("audio_advanced_tab", "Advanced", AppAction(), UiAction::ViewCommand::ShowAudioAdvanced);
	settingsUi_.addComboBox("preset", uiPresetNames(), static_cast<int>(settings_.appearance.activePreset), AppAction(AppActionKind::SetUiPreset));
	settingsUi_.addComboBox("palette", paletteNames(), paletteIndex(activeTheme(settings_).palette), AppAction(AppActionKind::SetPalette));
	settingsUi_.addComboBox("spacing", spacingNames(), spacingIndex(settings_.visual.spacing), AppAction(AppActionKind::SetSpacing));
	settingsUi_.addButton("appearance_customize", "Customize appearance...", AppAction(), UiAction::ViewCommand::OpenAppearanceEditor);
	settingsUi_.addComboBox("domain", domainNames(), static_cast<int>(settings_.simulation.domain), AppAction(AppActionKind::SetInputDomain));
	settingsUi_.addComboBox("pattern", inputPatternNames(), static_cast<int>(settings_.simulation.pattern), AppAction(AppActionKind::SetInputPattern));
	settingsUi_.addComboBox("playback_timing", std::vector<std::string>{ "Fixed duration", "Reference cadence" },
		static_cast<int>(settings_.simulation.playbackTimingMode), AppAction(AppActionKind::SetPlaybackTimingMode));
	settingsUi_.addComboBox("merge_buffer_visualization", booleanNames(),
		settings_.simulation.mergeBufferVisualizationEnabled ? 1 : 0, AppAction(AppActionKind::SetMergeBufferVisualization));
	settingsUi_.addComboBox("completion_scan", booleanNames(), settings_.simulation.completionScanEnabled ? 1 : 0,
		AppAction(AppActionKind::SetCompletionScan));
	settingsCaptions_["preset"] = "Interface preset";
	settingsCaptions_["palette"] = "Palette";
	settingsCaptions_["spacing"] = "Bar spacing";
	settingsCaptions_["appearance_customize"] = "";
	settingsCaptions_["domain"] = "Input domain";
	settingsCaptions_["pattern"] = "Input pattern";
	settingsCaptions_["playback_timing"] = "Playback timing";
	settingsCaptions_["merge_buffer_visualization"] = "Merge buffer visualization";
	settingsCaptions_["completion_scan"] = "Completion scan";
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
			const std::string id = settingsAlgorithmOptionControlId(algorithm, groupId);
			settingsUi_.addComboBox(id, optionNames, selectedIndex,
				AppAction(AppActionKind::SetAlgorithmOption, algorithm.familyId, algorithm.variantId, groupId));
			settingsCaptions_[id] = settingsAlgorithmOptionCaption(groupId);
		}
	}
	const SoundEngine::ReconstructedParameters& audio = settings_.reconstructedAudio;
	settingsUi_.addComboBox("recon_enabled", booleanNames(), audio.audioEnabled ? 1 : 0,
		reconstructedAction(ReconstructedAudioParameterId::Enabled));
	settingsUi_.addSlider("recon_master", "Master volume", 0.0f, 1.0f, 0.01f, static_cast<float>(audio.masterLevel),
		reconstructedAction(ReconstructedAudioParameterId::MasterLevel));
	settingsUi_.addComboBox("recon_idle_policy", std::vector<std::string>{ "Hold selection", "Release selection" },
		static_cast<int>(audio.idlePolicy), reconstructedAction(ReconstructedAudioParameterId::IdlePolicy));
	settingsUi_.addSlider("recon_release", "Special release (ms)", 0.0f, 2000.0f, 10.0f,
		static_cast<float>(audio.specialReleaseMilliseconds), reconstructedAction(ReconstructedAudioParameterId::SpecialReleaseMilliseconds));
	settingsUi_.addButton("recon_restore", "Restore reconstructed defaults", AppAction(AppActionKind::RestoreReconstructedDefaults));
	settingsUi_.addSlider("recon_min_pitch", "Minimum pitch (Hz)", 50.0f, 1000.0f, 1.0f,
		static_cast<float>(audio.minimumFrequencyHz), reconstructedAction(ReconstructedAudioParameterId::MinimumFrequencyHz));
	settingsUi_.addSlider("recon_max_pitch", "Maximum pitch (Hz)", 1000.0f, 8000.0f, 1.0f,
		static_cast<float>(audio.maximumFrequencyHz), reconstructedAction(ReconstructedAudioParameterId::MaximumFrequencyHz));
	settingsUi_.addComboBox("recon_mapping", std::vector<std::string>{ "Linear Hz", "Log frequency" },
		static_cast<int>(audio.mapping), reconstructedAction(ReconstructedAudioParameterId::Mapping));
	settingsUi_.addSlider("recon_clock", "Excitation clock (Hz)", 100.0f, 600.0f, 1.0f,
		static_cast<float>(audio.excitationClockHz), reconstructedAction(ReconstructedAudioParameterId::ExcitationClockHz));
	settingsUi_.addSlider("recon_cycles", "Carrier cycles", 1.0f, 16.0f, 0.1f,
		static_cast<float>(audio.carrierCycles), reconstructedAction(ReconstructedAudioParameterId::CarrierCycles));
	settingsUi_.addSlider("recon_tukey", "Tukey taper", 0.0f, 1.0f, 0.01f,
		static_cast<float>(audio.tukeyFraction), reconstructedAction(ReconstructedAudioParameterId::TukeyFraction));
	settingsUi_.addSlider("recon_delay", "Second operand delay (us)", 0.0f, 2000.0f, 1.0f,
		static_cast<float>(audio.secondOperandDelayMicroseconds), reconstructedAction(ReconstructedAudioParameterId::SecondOperandDelayMicroseconds));
	settingsUi_.addComboBox("recon_lowpass", booleanNames(), audio.lowPassEnabled ? 1 : 0,
		reconstructedAction(ReconstructedAudioParameterId::LowPassEnabled));
	settingsUi_.addSlider("recon_cutoff", "Low-pass cutoff (Hz)", 500.0f, 20000.0f, 10.0f,
		static_cast<float>(audio.lowPassCutoffHz), reconstructedAction(ReconstructedAudioParameterId::LowPassCutoffHz));
	const char* audioIds[] = { "recon_enabled", "recon_master", "recon_idle_policy", "recon_release", "recon_min_pitch",
		"recon_max_pitch", "recon_mapping", "recon_clock", "recon_cycles", "recon_tukey", "recon_delay", "recon_lowpass", "recon_cutoff" };
	const char* audioNames[] = { "Audio enabled", "Master volume", "Idle selection", "Special release (ms)", "Minimum pitch (Hz)",
		"Maximum pitch (Hz)", "Value mapping", "Excitation clock (Hz)", "Carrier cycles", "Tukey taper", "Second operand delay (us)",
		"Tone low-pass filter", "Low-pass cutoff (Hz)" };
	for (std::size_t i = 0; i < sizeof(audioIds) / sizeof(audioIds[0]); ++i) settingsCaptions_[audioIds[i]] = audioNames[i];
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
	appearanceUi_.addComboBox("appearance_preset", uiPresetNames(), static_cast<int>(settings_.appearance.activePreset));
	appearanceUi_.addComboBox("appearance_palette", paletteNames(), paletteIndex(activeTheme(settings_).palette));
	appearanceUi_.addComboBox("appearance_category", std::vector<std::string>{ "All categories", "Colors", "Typography", "Spacing / layout", "Decorations" }, 0);
	appearanceUi_.addButton("appearance_search", "Search appearance tokens");
	appearanceUi_.addComboBox("appearance_token", std::vector<std::string>(), 0);
	appearanceUi_.addButton("appearance_value_down", "-");
	appearanceUi_.addButton("appearance_value", "0");
	appearanceUi_.addButton("appearance_value_up", "+");
	appearanceUi_.addSlider("appearance_value_slider", "Property value", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	appearanceUi_.addButton("appearance_color", "Pick color...");
	appearanceUi_.addButton("appearance_color_alpha", "Alpha 1.000");
	appearanceUi_.addSlider("appearance_color_r", "Red", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	appearanceUi_.addSlider("appearance_color_g", "Green", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	appearanceUi_.addSlider("appearance_color_b", "Blue", 0.0f, 1.0f, 0.01f, 0.0f, AppAction());
	appearanceUi_.addSlider("appearance_color_a", "Alpha", 0.0f, 1.0f, 0.01f, 1.0f, AppAction());
	appearanceUi_.addButton("appearance_boolean", "Toggle value");
	appearanceUi_.addComboBox("appearance_choice", std::vector<std::string>(), 0);
	appearanceUi_.addComboBox("appearance_font_family", std::vector<std::string>(), 0);
	appearanceUi_.addComboBox("appearance_font_style", std::vector<std::string>(), 0);
	appearanceUi_.addButton("appearance_reset_token", "Reset property");
	appearanceUi_.addButton("appearance_reset_theme", "Reset this preset");
	appearanceUi_.addButton("appearance_import", "Import profile...");
	appearanceUi_.addButton("appearance_export", "Export profile...");
	appearanceUi_.addButton("appearance_apply", "Apply");
	appearanceUi_.addButton("appearance_cancel", "Cancel");
	mainUi_.find("panel_toggle")->setRole(UiControlRole::PanelToggle);
	mainUi_.find("settings")->setRole(UiControlRole::Settings);
	mainUi_.find("algorithm")->setRole(UiControlRole::AlgorithmChoice);
	mainUi_.find("run")->setRole(UiControlRole::BuildTrace);
	mainUi_.find("play")->setRole(UiControlRole::Playback);
	mainUi_.find("reset")->setRole(UiControlRole::Reset);
	mainUi_.find("bars_value")->setRole(UiControlRole::EditableValue);
	mainUi_.find("time_value")->setRole(UiControlRole::EditableValue);
	mainUi_.find("start_overlay")->setRole(UiControlRole::Playback);
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
		if (mainUi_.widgets()[i]->kind() == UiWidgetKind::ComboBox) mainUi_.widgets()[i]->setRole(UiControlRole::Choice);
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
		if (settingsUi_.widgets()[i]->kind() == UiWidgetKind::ComboBox) settingsUi_.widgets()[i]->setRole(UiControlRole::Choice);

	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 16, { 238, 246, 249, 255 }));
	}
	for (std::size_t i = 0; i < appearanceUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *appearanceUi_.widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 15, { 255, 255, 255, 255 }));
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
	refreshAppearanceTokens();
}

void ControlPanel::updateLayout()
{
	width_ = window_.getLogicalWidth();
	height_ = window_.getLogicalHeight();
	if (width_ <= 0 || height_ <= 0)
		return;
	const UiPresetId preset = renderPreferences_.appearance.activePreset;
	const LayoutCacheKey currentKey(width_, height_, window_.getDrawableWidth(), window_.getDrawableHeight(),
		settings_.visual.panelExpanded, static_cast<int>(preset), layoutRevision(renderPreferences_), fontMetricRevision(renderPreferences_));
	if (layoutCacheValid_ && sameLayoutInputs(layoutKey_, currentKey))
	{
		layoutSettingsControls();
		layoutMainComboPopups();
		layoutAlgorithmMenu();
		return;
	}
	layoutKey_ = currentKey;
	layoutCacheValid_ = true;
	laidOutPreset_ = preset;
	UiLayoutInput layoutInput;
	layoutInput.width = static_cast<float>(width_);
	layoutInput.height = static_cast<float>(height_);
	layoutInput.expanded = settings_.visual.panelExpanded;
	layoutInput.headerHeight = preset == UiPresetId::ClassicWindows95 ? 0.0f :
		themeNumber(renderPreferences_, "layout.header_height", 42.0f);
	layoutInput.menuHeight = preset == UiPresetId::Current ? 0.0f : themeNumber(renderPreferences_, "layout.menu_height", 26.0f);
	layoutInput.margin = themeNumber(renderPreferences_, "layout.control_margin", preset == UiPresetId::Current ? 16.0f : 12.0f);
	layoutInput.gap = themeNumber(renderPreferences_, "layout.control_gap", 6.0f);
	layoutInput.controlHeight = themeNumber(renderPreferences_, "layout.control_height", 30.0f);
	layoutInput.railWidth = themeNumber(renderPreferences_, "layout.rail_width", 340.0f);
	layoutInput.transportHeight = themeNumber(renderPreferences_, "layout.transport_height", 88.0f);
	layoutInput.chartInset = themeNumber(renderPreferences_, "layout.chart_inset", preset == UiPresetId::Current ? 0.0f : 16.0f);
	layoutInput.contentGap = themeNumber(renderPreferences_, "layout.content_gap", 14.0f);
	layoutInput.timelineArrowSize = themeNumber(renderPreferences_, "scrollbar.arrow_size", 14.0f);
	std::vector<std::string> controlIds;
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i) controlIds.push_back(mainUi_.widgets()[i]->id());
	layoutResult_ = calculateUiLayout(preset, layoutInput, controlIds);
	mainUi_.setFocusOrder(layoutResult_.focusOrder);
	headerBounds_ = layoutResult_.header;
	timelineBounds_ = layoutResult_.timelineTrack.width > 0.0f ? layoutResult_.timelineTrack : layoutResult_.timeline;
	timelineControlBounds_ = layoutResult_.timeline;
	timelineLeftArrowBounds_ = layoutResult_.timelineLeftArrow;
	timelineRightArrowBounds_ = layoutResult_.timelineRightArrow;
	chartViewport_ = layoutResult_.chartViewport;
	for (std::map<std::string, UiRect>::const_iterator bounds = layoutResult_.controlBounds.begin();
		bounds != layoutResult_.controlBounds.end(); ++bounds)
	{
		UiWidget* widget = mainUi_.find(bounds->first);
		if (widget) widget->setBounds(bounds->second);
	}
	mainUi_.find("direction")->setPresentation(preset != UiPresetId::Current
		? UiWidgetPresentation::RadioGroup : UiWidgetPresentation::Standard);
	mainUi_.find("direction")->setInteractiveSubregions(layoutResult_.interactiveSubregions["direction"]);
	mainUi_.find("bars_value")->setPresentation(UiWidgetPresentation::EditableLabel);
	mainUi_.find("time_value")->setPresentation(UiWidgetPresentation::EditableLabel);
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		UiWidget* widget = settingsUi_.widgets()[i].get();
		if (widget->kind() == UiWidgetKind::Slider)
		{
			widget->setPresentation(preset != UiPresetId::Current ? UiWidgetPresentation::DosScrollbar : UiWidgetPresentation::Standard);
			widget->setScrollbarArrowSize(preset != UiPresetId::Current &&
				themeBoolean(renderPreferences_, "scrollbar.show_arrows", true)
				? themeNumber(renderPreferences_, "scrollbar.arrow_size", 14.0f) : 0.0f);
		}
		else if (widget->kind() == UiWidgetKind::ComboBox && widget->options().size() == 2 &&
			(widget->id().find("enabled") != std::string::npos || widget->id().find("visualization") != std::string::npos ||
			 widget->id().find("completion_scan") != std::string::npos || widget->id() == "completion_scan"))
			widget->setPresentation(preset != UiPresetId::Current ? UiWidgetPresentation::RadioGroup : UiWidgetPresentation::Standard);
		else widget->setPresentation(UiWidgetPresentation::Standard);
	}
	if (!settings_.visual.panelExpanded)
	{
		for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
		{
			const UiWidget* widget = mainUi_.widgets()[i].get();
			const std::string& id = widget->id();
			const bool headerControl = (preset != UiPresetId::ClassicWindows95 && (id == "panel_toggle" || id == "settings")) ||
				(preset != UiPresetId::Current && (id == "menu_file" || id == "menu_view" || id == "menu_tools"));
			mainUi_.setWidgetVisible(id, headerControl || id == "start_overlay");
		}
	}
	else
	{
		for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
			mainUi_.setWidgetVisible(mainUi_.widgets()[i]->id(), true);
	}
	const bool hasClassicRail = preset != UiPresetId::Current;
	mainUi_.setWidgetVisible("menu_file", hasClassicRail);
	mainUi_.setWidgetVisible("menu_view", hasClassicRail);
	mainUi_.setWidgetVisible("menu_tools", hasClassicRail);
	mainUi_.setWidgetVisible("run", !hasClassicRail);
	mainUi_.setWidgetVisible("panel_toggle", preset != UiPresetId::ClassicWindows95);
	mainUi_.setWidgetVisible("settings", preset != UiPresetId::ClassicWindows95);
	const bool wideLayout = width_ >= 900;
	mainUi_.setWidgetVisible("domain", hasClassicRail && wideLayout && settings_.visual.panelExpanded);
	mainUi_.setWidgetVisible("pattern", hasClassicRail && wideLayout && settings_.visual.panelExpanded);
	mainUi_.setWidgetVisible("seed_readout", hasClassicRail && wideLayout && settings_.visual.panelExpanded);
	mainUi_.setWidgetVisible("start_overlay", !settings_.visual.panelExpanded);
	mainUi_.find("panel_toggle")->setLabel(settings_.visual.panelExpanded ? "v" : ">");
	gearBounds_ = mainUi_.find("settings")->bounds();

	const float panelWidth = std::min(520.0f, std::max(180.0f, static_cast<float>(width_) - 32.0f));
	const float panelHeight = std::min(650.0f, std::max(300.0f, static_cast<float>(height_) - 54.0f));
	settingsPanelBounds_ = UiRect(std::max(8.0f, static_cast<float>(width_) - panelWidth - 8.0f), 42.0f,
		panelWidth, std::min(panelHeight, std::max(200.0f, static_cast<float>(height_) - 50.0f)));
	settingsUi_.layoutContainer("settings_panel", settingsPanelBounds_);
	if (settingsOpen_)
		settingsUi_.setModalBounds(settingsPanelBounds_, UiAction::ViewCommand::CloseSettings);
	layoutAlgorithmMenu();
	layoutSettingsControls();
	layoutMainComboPopups();
	if (appearanceEditorOpen_) layoutAppearanceEditor();
}

void ControlPanel::layoutMainComboPopups()
{
	layoutResult_.popupBounds.clear();
	const ResolvedTextStyle textStyle = resolveTextStyle(13.0f);

	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		UiWidget* widget = mainUi_.widgets()[i].get();
		widget->setPopupBounds(UiRect());
		if (widget->kind() != UiWidgetKind::ComboBox || !widget->visible() ||
			widget->bounds().width <= 0.0f || widget->bounds().height <= 0.0f || widget->options().empty())
			continue;

		float popupWidth = widget->bounds().width;
		for (std::size_t option = 0; option < widget->options().size(); ++option)
		{
			const glm::vec2 measured = TextLabel::measureText(widget->options()[option], textStyle);
			if (measured.y > 0.0f)
				popupWidth = std::max(popupWidth, measured.x * textStyle.logicalHeight / measured.y + 22.0f);
		}
		popupWidth = std::min(popupWidth, std::max(0.0f, static_cast<float>(width_) - 16.0f));
		const std::size_t visibleOptions = widget->visibleOptionCount();
		const float popupHeight = std::min(widget->bounds().height * static_cast<float>(visibleOptions),
			std::max(0.0f, static_cast<float>(height_) - 16.0f));
		const float popupX = std::max(8.0f, std::min(widget->bounds().x,
			static_cast<float>(width_) - popupWidth - 8.0f));
		float popupY = widget->bounds().y + widget->bounds().height;
		bool popupAbove = false;
		if (popupY + popupHeight > static_cast<float>(height_) - 8.0f)
		{
			const float aboveY = widget->bounds().y - popupHeight;
			if (aboveY >= 8.0f)
			{
				popupY = aboveY;
				popupAbove = true;
			}
			else popupY = std::max(8.0f, static_cast<float>(height_) - popupHeight - 8.0f);
		}
		const UiRect popup(popupX, popupY, popupWidth, popupHeight);
		widget->setPopupAbove(popupAbove);
		widget->setPopupBounds(popup);
		layoutResult_.popupBounds[widget->id()] = popup;
	}
}

void ControlPanel::layoutSettingsControls()
{
	if (settingsPanelBounds_.width <= 0.0f) return;
	const float x = settingsPanelBounds_.x + 16.0f;
	const float width = std::max(1.0f, settingsPanelBounds_.width - 32.0f);
	const SettingsPanelGeometry panelGeometry = calculateSettingsPanelGeometry(settingsPanelBounds_, settingsPanelModel_.page());
	settingsUi_.find("appearance_tab")->setBounds(panelGeometry.tabs[0]);
	settingsUi_.find("algorithm_tab")->setBounds(panelGeometry.tabs[1]);
	settingsUi_.find("audio_tab")->setBounds(panelGeometry.tabs[2]);
	settingsUi_.find("audio_basic_tab")->setBounds(panelGeometry.audioGroups[0]);
	settingsUi_.find("audio_advanced_tab")->setBounds(panelGeometry.audioGroups[1]);
	settingsContentBounds_ = panelGeometry.content;
	settingsUi_.find("settings_close")->setBounds(panelGeometry.closeButton);
	settingsUi_.find("audio_retry")->setBounds(panelGeometry.audioRetryButton);
	const sortinglogic::AlgorithmDescriptor* selected = sortinglogic::algorithmDescriptor(
		static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
	const std::vector<std::string> active = settingsPanelControlIds(
		settingsPanelModel_.page(), settingsPanelModel_.audioGroup(), selected);
	std::set<std::string> visibleWidgetIds;
	visibleWidgetIds.insert("appearance_tab");
	visibleWidgetIds.insert("algorithm_tab");
	visibleWidgetIds.insert("audio_tab");
	visibleWidgetIds.insert("settings_close");
	if (settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio))
	{
		visibleWidgetIds.insert("audio_retry");
		visibleWidgetIds.insert("audio_basic_tab");
		visibleWidgetIds.insert("audio_advanced_tab");
	}
	const float rowHeight = 48.0f;
	const float contentBottom = settingsContentBounds_.y + settingsContentBounds_.height;
	const float totalHeight = static_cast<float>(active.size()) * rowHeight;
	const float scrollbarThickness = themeNumber(renderPreferences_, "scrollbar.thickness", 8.0f);
	settingsPanelModel_.scrollViewport().configure(settingsContentBounds_, totalHeight, rowHeight, settingsPanelModel_.scrollOffset());
	for (std::size_t i = 0; i < active.size(); ++i)
	{
		const UiRect rowBounds = settingsPanelModel_.scrollViewport().itemBounds(i, rowHeight);
		const float rowY = rowBounds.y;
		const bool visible = settingsPanelModel_.scrollViewport().itemIntersectsViewport(i, rowHeight) && rowY + rowHeight <= contentBottom + 0.5f;
		if (visible) visibleWidgetIds.insert(active[i]);
		UiWidget* widget = settingsUi_.find(active[i]);
		if (!widget) continue;
		const SettingsWidgetGeometry geometry = calculateSettingsWidgetGeometry(*widget, rowBounds,
			x, width, settingsPanelBounds_, scrollbarThickness);
		widget->setBounds(geometry.bounds);
		widget->setInteractiveSubregions(geometry.interactiveSubregions);
		if (geometry.setPopupAbove) widget->setPopupAbove(geometry.popupAbove);
	}
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		const std::string& id = settingsUi_.widgets()[i]->id();
		settingsUi_.setWidgetVisible(id, visibleWidgetIds.find(id) != visibleWidgetIds.end());
	}
}

void ControlPanel::updateAudioControls()
{
	const SoundEngine::ReconstructedParameters& a = settings_.reconstructedAudio;
	auto slider = [this](const char* id, float value) { settingsUi_.find(id)->setSliderValue(value); };
	auto selection = [this](const char* id, int value) { settingsUi_.find(id)->setSelectedIndex(value); };
	selection("recon_enabled", a.audioEnabled ? 1 : 0);
	slider("recon_master", static_cast<float>(a.masterLevel));
	selection("recon_idle_policy", static_cast<int>(a.idlePolicy));
	slider("recon_release", static_cast<float>(a.specialReleaseMilliseconds));
	slider("recon_min_pitch", static_cast<float>(a.minimumFrequencyHz));
	slider("recon_max_pitch", static_cast<float>(a.maximumFrequencyHz));
	selection("recon_mapping", static_cast<int>(a.mapping));
	slider("recon_clock", static_cast<float>(a.excitationClockHz));
	slider("recon_cycles", static_cast<float>(a.carrierCycles));
	slider("recon_tukey", static_cast<float>(a.tukeyFraction));
	slider("recon_delay", static_cast<float>(a.secondOperandDelayMicroseconds));
	selection("recon_lowpass", a.lowPassEnabled ? 1 : 0);
	slider("recon_cutoff", static_cast<float>(a.lowPassCutoffHz));
	settingsUi_.setWidgetEnabled("recon_release", a.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
	settingsUi_.setWidgetEnabled("recon_cutoff", a.lowPassEnabled);
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
		if (label != labels_.end()) label->second->setContent(caption.str());
	}
	settingsUi_.find("algorithm_audio_reset")->setAction(AppAction(AppActionKind::ResetVariantAudioLevels,
		algorithm.familyId, algorithm.variantId));
}

void ControlPanel::updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot, bool playbackPending)
{
	screenModel_ = buildUiScreenModel(snapshot, renderPreferences_, playbackPending, displayedSaveStatus_);
	const bool busy = !screenModel_.setupEnabled;
	const char* setupIds[] = { "shuffle", "insta_sort", "insta_invert", "algorithm", "direction", "run",
		"bars_down", "bars_up", "time_down", "time_up" };
	for (std::size_t i = 0; i < sizeof(setupIds) / sizeof(setupIds[0]); ++i)
	{
		mainUi_.setWidgetEnabled(setupIds[i], !busy);
	}
	mainUi_.setWidgetEnabled("run", screenModel_.runEnabled);
	mainUi_.setWidgetEnabled("play", screenModel_.playEnabled);
	UiWidget* algorithm = mainUi_.find("algorithm");
	if (algorithm) algorithm->setLabel(algorithmLabel(snapshot.algorithm));
	std::string playCaption = screenModel_.playCaption;
	if (renderPreferences_.appearance.activePreset != UiPresetId::Current)
	{
		switch (screenModel_.transport)
		{
		case UiTransportState::NoTrace: playCaption = "Play"; break;
		case UiTransportState::Ready: playCaption = "Play"; break;
		case UiTransportState::Playing: playCaption = "Pause"; break;
		case UiTransportState::Queued: playCaption = "Cancel"; break;
		case UiTransportState::Building: playCaption = "Queue Play"; break;
		case UiTransportState::Preparing: playCaption = "Preparing"; break;
		case UiTransportState::Replay: playCaption = "Replay"; break;
		}
	}
	mainUi_.find("play")->setLabel(playCaption);
	mainUi_.find("bars_value")->setLabel(std::to_string(snapshot.itemCount) + " bars");
	mainUi_.find("seed_readout")->setLabel("Seed " + std::to_string(snapshot.inputSeed));
	mainUi_.setWidgetEnabled("seed_readout", false);
	const bool referenceCadence = settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence;
	mainUi_.setWidgetEnabled("bars_value", !busy);
	mainUi_.setWidgetEnabled("time_value", !busy && !referenceCadence);
	mainUi_.find("time_value")->setLabel(screenModel_.durationCaption);
	mainUi_.find("direction")->setSelectedIndex(snapshot.direction == sortinglogic::SortDirection::Descending ? 1 : 0);
	mainUi_.find("domain")->setSelectedIndex(static_cast<int>(snapshot.domain));
	mainUi_.find("pattern")->setSelectedIndex(static_cast<int>(snapshot.pattern));
	mainUi_.setWidgetEnabled("domain", !busy);
	mainUi_.setWidgetEnabled("pattern", !busy);
	mainUi_.setWidgetVisible("start_overlay", !settings_.visual.panelExpanded && !snapshot.hasTrace && !snapshot.buildingTrace);
	mainUi_.setWidgetEnabled("start_overlay", screenModel_.playEnabled);
	mainUi_.find("menu_file")->setOptionEnabled(1, screenModel_.runEnabled);
	mainUi_.find("menu_file")->setOptionEnabled(2, screenModel_.playEnabled);
	mainUi_.find("menu_file")->setOptionEnabled(3, true);
	for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
		pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
		mainUi_.find("pattern")->setOptionEnabled(static_cast<std::size_t>(pattern),
			sortinglogic::inputPatternCompatible(snapshot.domain, static_cast<sortinglogic::InputPattern>(pattern)) &&
			(static_cast<sortinglogic::InputPattern>(pattern) != sortinglogic::InputPattern::AndrutReference ||
				sortinglogic::algorithmSupports(snapshot.algorithm, sortinglogic::InputDomain::NormalizedFloats)));
	settingsUi_.find("preset")->setSelectedIndex(static_cast<int>(renderPreferences_.appearance.activePreset));
	settingsUi_.find("palette")->setSelectedIndex(paletteIndex(activeTheme(renderPreferences_).palette));
	settingsUi_.find("spacing")->setSelectedIndex(spacingIndex(settings_.visual.spacing));
	settingsUi_.find("domain")->setSelectedIndex(static_cast<int>(settings_.simulation.domain));
	settingsUi_.find("pattern")->setSelectedIndex(static_cast<int>(settings_.simulation.pattern));
	settingsUi_.find("playback_timing")->setSelectedIndex(static_cast<int>(settings_.simulation.playbackTimingMode));
	settingsUi_.find("merge_buffer_visualization")->setSelectedIndex(settings_.simulation.mergeBufferVisualizationEnabled ? 1 : 0);
	settingsUi_.find("completion_scan")->setSelectedIndex(settings_.simulation.completionScanEnabled ? 1 : 0);
	settingsUi_.setWidgetEnabled("domain", !busy);
	settingsUi_.setWidgetEnabled("pattern", !busy);
	settingsUi_.setWidgetEnabled("playback_timing", !busy);
	settingsUi_.setWidgetEnabled("merge_buffer_visualization", !busy && snapshot.algorithm == sortinglogic::SortAlgorithm::Merge);
	settingsUi_.setWidgetEnabled("completion_scan", !busy);
	mainUi_.setWidgetEnabled("time_down", !busy && !referenceCadence);
	mainUi_.setWidgetEnabled("time_up", !busy && !referenceCadence);
	const std::vector<sortinglogic::AlgorithmDescriptor>& optionCatalog = sortinglogic::algorithmCatalog();
	for (std::size_t algorithmIndex = 0; algorithmIndex < optionCatalog.size(); ++algorithmIndex)
	{
		const sortinglogic::AlgorithmDescriptor& algorithmDescriptor = optionCatalog[algorithmIndex];
		std::set<std::string> groups;
		for (std::size_t optionIndex = 0; optionIndex < algorithmDescriptor.supportedOptions.size(); ++optionIndex)
		{
			const std::string groupId(algorithmDescriptor.supportedOptions[optionIndex].groupId);
			if (!groups.insert(groupId).second) continue;
			const std::string id = settingsAlgorithmOptionControlId(algorithmDescriptor, groupId);
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
		pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
		settingsUi_.find("pattern")->setOptionEnabled(static_cast<std::size_t>(pattern),
			sortinglogic::inputPatternCompatible(snapshot.domain, static_cast<sortinglogic::InputPattern>(pattern)) &&
			(static_cast<sortinglogic::InputPattern>(pattern) != sortinglogic::InputPattern::AndrutReference ||
				sortinglogic::algorithmSupports(snapshot.algorithm, sortinglogic::InputDomain::NormalizedFloats)));
	updateAudioControls();
	const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(snapshot.algorithm);
	if (descriptor) updateAlgorithmAudioControls(*descriptor);
	layoutSettingsControls();
	updateInlineEditLabel();
}

void ControlPanel::syncLabels(const sortinglogic::RenderSnapshot& snapshot)
{
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
			i->second->setContent(text);
		}
	}
	title_->setContent(renderPreferences_.appearance.activePreset == UiPresetId::ClassicWindows95
		? "Actually Good Sorting Sounds" : "Sorting Simulation | deterministic event trace");
	mainUi_.find("next")->setLabel(renderPreferences_.appearance.activePreset == UiPresetId::ClassicWindows95 ? "Step" : "Event >");
	std::ostringstream detailText;
	const UiAlgorithmCapabilities& facts = screenModel_.algorithmCapabilities;
	if (renderPreferences_.appearance.activePreset != UiPresetId::Current)
	{
		if (snapshot.hasTrace) detailText << "Event " << snapshot.cursor << "/" << snapshot.eventCount;
		else if (snapshot.buildingTrace) detailText << "Building trace";
		else detailText << "No trace";
		detailText << " | " << std::fixed << std::setprecision(1) << snapshot.elapsedSeconds << "/";
		if (settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			detailText << snapshot.audioEligibleEventCount / 300.0;
		else detailText << snapshot.durationSeconds;
		detailText << " sec | seed " << snapshot.inputSeed;
	}
	else
	{
		detailText << screenModel_.algorithm << "  |  " << screenModel_.direction << "  |  " << screenModel_.itemCount << " bars  |  ";
		detailText << screenModel_.inputDomain << " / " << screenModel_.inputPattern << "  |  ";
		if (facts.available) detailText << (facts.stable ? "Stable" : "Unstable") << (facts.inPlace ? ", in-place" : ", auxiliary space")
			<< "  |  " << facts.timeComplexity << " time, " << facts.spaceComplexity << " space  |  ";
		if (!screenModel_.shellGapSequence.empty()) detailText << "Gap sequence: " << screenModel_.shellGapSequence << "  |  ";
		if (snapshot.hasTrace) detailText << "Event " << snapshot.cursor << "/" << snapshot.eventCount;
		else if (snapshot.buildingTrace) detailText << "Building trace";
		else detailText << "No trace";
		detailText << "  |  " << std::fixed << std::setprecision(1) << snapshot.elapsedSeconds << "/";
		if (settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence)
			detailText << snapshot.audioEligibleEventCount / 300.0;
		else detailText << snapshot.durationSeconds;
		detailText << " sec";
		if (snapshot.inputSeed) detailText << "  |  seed " << snapshot.inputSeed;
	}
	const std::string nextDetail = detailText.str();
	const Uint32 now = SDL_GetTicks();
	const bool allowLiveTextUpdate = !snapshot.playing || now - lastDetailUpdate_ >= 200;
	if (nextDetail != displayedDetail_ && allowLiveTextUpdate)
	{
		detail_->setContent(nextDetail);
		displayedDetail_ = nextDetail;
		lastDetailUpdate_ = now;
	}
	std::ostringstream statusText;
	if (renderPreferences_.appearance.activePreset != UiPresetId::Current)
	{
		if (!snapshot.runCompatible) statusText << facts.compatibilityMessage << " | ";
		else statusText << screenModel_.algorithm << " | " << screenModel_.inputDomain << "/" << screenModel_.inputPattern << " | ";
		if (snapshot.hasTrace)
			statusText << "Cmp " << snapshot.metrics.comparisons << " | Swap " << snapshot.metrics.swaps
				<< " | Main " << snapshot.metrics.primaryWrites << " | Aux " << snapshot.metrics.auxiliaryItemWrites
				<< "/" << snapshot.metrics.auxiliaryCounterUpdates << " | Peak " << snapshot.metrics.peakAuxiliaryCapacity;
		else statusText << snapshot.status;
	}
	else if (!snapshot.runCompatible) statusText << facts.compatibilityMessage << " | ";
	if (renderPreferences_.appearance.activePreset == UiPresetId::Current && snapshot.hasTrace)
	{
		statusText << "Cmp " << snapshot.metrics.comparisons << "  Swap " << snapshot.metrics.swaps
			<< "  Main " << snapshot.metrics.primaryWrites << "  Aux " << snapshot.metrics.auxiliaryItemWrites
			<< "/" << snapshot.metrics.auxiliaryCounterUpdates << "  Peak " << snapshot.metrics.peakAuxiliaryCapacity;
		for (std::size_t metric = 0; metric < facts.metrics.size(); ++metric)
			statusText << "  " << facts.metrics[metric].caption << " " << facts.metrics[metric].value;
		statusText << "  |  " << snapshot.status;
	}
	else if (renderPreferences_.appearance.activePreset == UiPresetId::Current) statusText << snapshot.status;
	const std::string statusValue = statusText.str();
	const std::string nextStatus = statusValue.size() > 220 ? statusValue.substr(0, 217) + "..." : statusValue;
	if (nextStatus != displayedStatus_ && allowLiveTextUpdate)
	{
		status_->setContent(nextStatus);
		displayedStatus_ = nextStatus;
	}
	std::ostringstream collapsedSummary;
	collapsedSummary << sortinglogic::algorithmDisplayName(snapshot.algorithm) << " - "
		<< traceProgressPercent(snapshot.hasTrace, snapshot.complete, snapshot.cursor, snapshot.eventCount) << "%";
	const std::string nextCollapsedSummary = collapsedSummary.str();
	if (nextCollapsedSummary != displayedCollapsedSummary_ && allowLiveTextUpdate)
	{
		collapsedSummary_->setContent(nextCollapsedSummary);
		displayedCollapsedSummary_ = nextCollapsedSummary;
	}
	settingsTitle_->setContent(settingsTabTitle(settingsPanelModel_.page(), settings_));
	const UiTokenValue requestedFont = effectiveUiTokenValue(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_), "typeface.primary");
	const std::string saveStatus = fontFallbackActive_ && requestedFont.type == UiTokenType::Font
		? "Font " + requestedFont.font.family + " " + requestedFont.font.style + " unavailable; using bundled Courier New."
		: displayedSaveStatus_.empty() ? "Preferences save automatically." : displayedSaveStatus_;
	settingsSaveStatus_->setContent(saveStatus);
}

bool ControlPanel::handleEvent(const SDL_Event& event, AppAction& action)
{
	if (!appearanceEditorOpen_ && !algorithmBrowser_.open && !appearanceSearchFocused_ && inlineEditWidgetId_.empty() &&
		event.type == SDL_KEYDOWN && !event.key.repeat &&
		event.key.keysym.sym == SDLK_F12 && (event.key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT)) == (KMOD_CTRL | KMOD_SHIFT))
	{
		openAppearanceEditor();
		return true;
	}
	renderPreferences_ = settings_;
	if (appearanceEditorOpen_) renderPreferences_.appearance = appearanceDraft_;
	updateLayout();
	if (appearanceEditorOpen_)
		return handleAppearanceEditorEvent(event, action);
	if (algorithmBrowser_.open)
		return handleAlgorithmMenuEvent(event, action);
	if (!settingsOpen_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		const UiWidget* bars = mainUi_.find("bars_value");
		const UiWidget* duration = mainUi_.find("time_value");
		std::string targetWidgetId;
		if (bars && bars->visible() && bars->enabled() && inRect(bars->bounds(), event.button.x, event.button.y))
			targetWidgetId = "bars_value";
		else if (duration && duration->visible() && duration->enabled() && inRect(duration->bounds(), event.button.x, event.button.y))
			targetWidgetId = "time_value";
		if (!targetWidgetId.empty())
		{
			if (!inlineEditWidgetId_.empty() && targetWidgetId == inlineEditWidgetId_) return true;
			if (!inlineEditWidgetId_.empty() && !commitInlineEdit(action)) return true;
			beginInlineEdit(targetWidgetId);
			return true;
		}
		if (!inlineEditWidgetId_.empty())
		{
			if (!commitInlineEdit(action)) return true;
			return true;
		}
	}
	if (!inlineEditWidgetId_.empty())
	{
		if (event.type == SDL_TEXTINPUT)
			return appendInlineEditText(event.text.text);
		if (event.type == SDL_KEYDOWN)
		{
			const SDL_Keycode key = event.key.keysym.sym;
			if (key == SDLK_ESCAPE)
			{
				cancelInlineEdit();
				return true;
			}
			if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
				return commitInlineEdit(action) || !inlineEditWidgetId_.empty();
			if (key == SDLK_BACKSPACE)
			{
				inlineEditReplaceOnNextDigit_ = false;
				if (!inlineEditBuffer_.empty()) inlineEditBuffer_.erase(inlineEditBuffer_.size() - 1);
				updateInlineEditLabel();
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEditBuffer_.clear();
				inlineEditReplaceOnNextDigit_ = false;
				updateInlineEditLabel();
				return true;
			}
			return true;
		}
	}
	if (settingsOpen_ && event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if (settingsPanelModel_.isPage(SettingsPanelPage::Algorithm) || settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio))
		{
			if (settingsPanelModel_.scrollViewport().scrollWheel(static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y))
			{
				layoutSettingsControls();
			}
		}
		return true;
	}
	if (!settingsOpen_ && algorithmProfileVisible_ && event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if (algorithmProfileScrollViewport_.scrollWheel(static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y))
			return true;
	}
	if (!settingsOpen_ && algorithmProfileVisible_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		algorithmProfileScrollViewport_.maximumOffset() > 0.0f &&
		algorithmProfileScrollTrack_.contains(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
	{
		const float trackTravel = algorithmProfileScrollTrack_.height - algorithmProfileScrollThumb_.height;
		if (trackTravel > 0.0f && !algorithmProfileScrollThumb_.contains(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
		{
			const float fraction = std::max(0.0f, std::min(1.0f,
				(static_cast<float>(event.button.y) - algorithmProfileScrollTrack_.y - algorithmProfileScrollThumb_.height * 0.5f) / trackTravel));
			algorithmProfileScrollViewport_.setOffset(fraction * algorithmProfileScrollViewport_.maximumOffset());
		}
		draggingAlgorithmProfileScroll_ = true;
		algorithmProfileDragStartY_ = static_cast<float>(event.button.y);
		algorithmProfileDragStartOffset_ = algorithmProfileScrollViewport_.offset();
		return true;
	}
	if (!settingsOpen_ && algorithmProfileVisible_ && event.type == SDL_MOUSEMOTION && draggingAlgorithmProfileScroll_)
	{
		const float trackTravel = algorithmProfileScrollTrack_.height - algorithmProfileScrollThumb_.height;
		if (trackTravel > 0.0f)
		{
			const float ratio = algorithmProfileScrollViewport_.maximumOffset() / trackTravel;
			algorithmProfileScrollViewport_.setOffset(algorithmProfileDragStartOffset_ +
				(static_cast<float>(event.motion.y) - algorithmProfileDragStartY_) * ratio);
		}
		return true;
	}
	if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && draggingAlgorithmProfileScroll_)
	{
		draggingAlgorithmProfileScroll_ = false;
		return true;
	}
	if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
		draggingAlgorithmProfileScroll_ = false;
	if (!settingsOpen_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		settings_.visual.panelExpanded && timelineControlBounds_.contains(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
	{
		if (timelineLeftArrowBounds_.contains(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
		{
			action = AppAction(AppActionKind::PreviousEvent);
			return true;
		}
		if (timelineRightArrowBounds_.contains(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
		{
			action = AppAction(AppActionKind::NextEvent);
			return true;
		}
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
		input.control = (event.key.keysym.mod & KMOD_CTRL) != 0;
		input.alt = (event.key.keysym.mod & KMOD_ALT) != 0;
	}
	else if (event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		input = UiInputEvent(UiInputType::Wheel, static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y);
	}
	else if (event.type == SDL_TEXTINPUT)
		input = UiInputEvent(UiInputType::TextInput, std::string(event.text.text));
	else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
		input = UiInputEvent(UiInputType::FocusLost);
	else
		translated = false;
	if (!translated)
		return settingsOpen_ || action.kind != AppActionKind::None;
	if (event.type == SDL_MOUSEBUTTONDOWN)
	{
		const SDL_Keymod modifiers = SDL_GetModState();
		input.shift = (modifiers & KMOD_SHIFT) != 0;
		input.control = (modifiers & KMOD_CTRL) != 0;
		input.alt = (modifiers & KMOD_ALT) != 0;
	}
	UiAction uiAction;
	const bool consumed = (settingsOpen_ ? settingsUi_ : mainUi_).handleEvent(input, uiAction);
	if (consumed && (uiAction.viewCommand != UiAction::ViewCommand::None || uiAction.appAction.kind != AppActionKind::None ||
		uiAction.event.kind != UiEventKind::None))
	{
		AppAction processedAction;
		processAction(uiAction, processedAction);
		if (processedAction.kind != AppActionKind::None || action.kind == AppActionKind::None)
			action = processedAction;
	}
	return consumed || settingsOpen_ || action.kind != AppActionKind::None;
}

void ControlPanel::processAction(const UiAction& action, AppAction& output)
{
	if (action.event.kind == UiEventKind::SelectionChanged &&
		(action.event.controlId == "menu_file" || action.event.controlId == "menu_view" || action.event.controlId == "menu_tools"))
	{
		UiWidget* menu = mainUi_.find(action.event.controlId);
		const int selected = action.event.selection;
		if (menu) menu->setSelectedIndex(0);
		if (action.event.controlId == "menu_file")
		{
			if (selected == 1) output = AppAction(AppActionKind::BuildTrace);
			else if (selected == 2) output = AppAction(AppActionKind::GenerateAndPlay);
			else if (selected == 3) output = AppAction(AppActionKind::Reset);
		}
		else if (action.event.controlId == "menu_view")
		{
			if (selected == 1) output = AppAction(AppActionKind::TogglePanelExpanded);
			else if (selected == 2) output = AppAction(AppActionKind::ToggleBorderlessFullscreen);
		}
		else if (action.event.controlId == "menu_tools")
		{
			if (selected == 1) openAlgorithmMenu();
			else if (selected == 2) processAction(UiAction(AppAction(), UiAction::ViewCommand::OpenSettings), output);
			else if (selected == 3) openAppearanceEditor();
		}
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::OpenAppearanceEditor)
	{
		openAppearanceEditor();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::CloseAppearanceEditor)
	{
		closeAppearanceEditor(true);
		return;
	}
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
	if (action.viewCommand == UiAction::ViewCommand::ShowAudioBasic ||
		action.viewCommand == UiAction::ViewCommand::ShowAudioAdvanced)
	{
		settingsPanelModel_.showAudioGroup(action.viewCommand == UiAction::ViewCommand::ShowAudioAdvanced
			? SettingsAudioGroup::Advanced : SettingsAudioGroup::Basic);
		settingsUi_.closeCombo();
		layoutSettingsControls();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowAlgorithmSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowAudioSettings ||
		action.viewCommand == UiAction::ViewCommand::ShowGeneralAudioSettings)
	{
		const SettingsPanelPage page = action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings
			? SettingsPanelPage::Appearance : action.viewCommand == UiAction::ViewCommand::ShowAlgorithmSettings
			? SettingsPanelPage::Algorithm : SettingsPanelPage::GeneralAudio;
		settingsPanelModel_.showPage(page);
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
		settingsPanelModel_.selectAudioBand(action.appAction.selection, static_cast<int>(SoundEngine::BandCount));
		updateAudioControls();
		layoutSettingsControls();
	}
	if (action.event.kind == UiEventKind::Activated &&
		(action.event.controlId == "bars_down" || action.event.controlId == "bars_up"))
	{
		const float direction = action.event.controlId == "bars_down" ? -1.0f : 1.0f;
		output = AppAction(AppActionKind::AdjustItemCount,
			direction * stepDeltaWithModifiers(10.0f, action.event.control, action.event.shift));
		return;
	}
	if (action.event.kind == UiEventKind::Activated &&
		(action.event.controlId == "time_down" || action.event.controlId == "time_up"))
	{
		const float direction = action.event.controlId == "time_down" ? -1.0f : 1.0f;
		output = AppAction(AppActionKind::AdjustDuration,
			direction * stepDeltaWithModifiers(1.0f, action.event.control, action.event.shift));
		return;
	}
	if (action.appAction.kind == AppActionKind::SetUiPreset)
	{
		mainUi_.closeCombo();
		settingsUi_.closeCombo();
		draggingTimeline_ = false;
		cancelInlineEdit();
		mainUi_.setInputCaptured(false);
		settingsUi_.setInputCaptured(settingsOpen_);
		layoutCacheValid_ = false;
	}
	output = action.appAction;
}

float ControlPanel::timelineFraction(float x) const
{
	if (timelineBounds_.width <= 0.0f) return 0.0f;
	return std::max(0.0f, std::min(1.0f, (x - timelineBounds_.x) / timelineBounds_.width));
}

void ControlPanel::render(const sortinglogic::RenderSnapshot& snapshot, const std::string& audioStatus)
{
	if (!isReady()) return;
	const ThemePalette palette = paletteForAppearance(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_));
	syncLabels(snapshot);
	fontFallbackActive_ = false;
	drawMainPanel(snapshot, palette);
	if (settingsOpen_ && !appearanceEditorOpen_) drawSettingsPanel(palette, audioStatus);
	if (algorithmBrowser_.open) drawAlgorithmMenu(palette);
	if (appearanceEditorOpen_) drawAppearanceEditor();
}

void ControlPanel::prepareFrame(const sortinglogic::RenderSnapshot& snapshot,
	const std::string& persistenceStatus, bool playbackPending)
{
	renderPreferences_ = settings_;
	if (appearanceEditorOpen_) renderPreferences_.appearance = appearanceDraft_;
	displayedSaveStatus_ = persistenceStatus;
	updateLayout();
	updateWidgetStates(snapshot, playbackPending);
	layoutAlgorithmMenu();
}

void ControlPanel::drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette)
{
	const UiPresetId preset = renderPreferences_.appearance.activePreset;
	const bool classicVga = preset == UiPresetId::FlatVgaDos || preset == UiPresetId::ClassicWindows95;
	const bool windows95 = preset == UiPresetId::ClassicWindows95;
	algorithmProfileVisible_ = false;
	algorithmProfileScrollTrack_ = UiRect();
	algorithmProfileScrollThumb_ = UiRect();
	submitBox(headerBounds_, windows95 ? palette.accent : palette.panel, PanelMaterial, UiLayer);
	if (layoutResult_.menuBar.height > 0.0f)
	{
		submitBox(layoutResult_.menuBar, windows95 ? palette.button : palette.panel, PanelMaterial, UiLayer);
		const glm::vec4 menuEdge = windows95 ? glm::vec4(0.33f, 0.33f, 0.33f, 1.0f) :
			glm::vec4(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
		submitBox(UiRect(layoutResult_.menuBar.x, layoutResult_.menuBar.y + layoutResult_.menuBar.height - 1.0f,
			layoutResult_.menuBar.width, 1.0f), menuEdge, ButtonMaterial, layerOffset(UiLayer, 1));
	}
	if (classicVga && settings_.visual.panelExpanded)
	{
		const UiRect transport = layoutResult_.transportArea;
		if (transport.width > 0.0f && transport.height > 0.0f)
		{
			submitBox(transport, palette.panel, PanelMaterial, UiLayer);
			const float border = 1.0f;
			const glm::vec4 edge(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
			submitBox(UiRect(transport.x, transport.y, transport.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(transport.x, transport.y + transport.height - border, transport.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
		}
		if (width_ >= 900)
		{
			const UiRect rail = layoutResult_.setupRail;
			submitBox(rail, palette.panel, PanelMaterial, UiLayer);
			const float border = 1.0f;
			const glm::vec4 edge(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
			submitBox(UiRect(rail.x, rail.y, rail.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(rail.x, rail.y + rail.height - border, rail.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(rail.x, rail.y, border, rail.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(rail.x + rail.width - border, rail.y, border, rail.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			const UiRect chart = chartViewport_;
			if (chart.width > 0.0f && chart.height > 0.0f)
			{
				submitBox(UiRect(chart.x, chart.y, chart.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(chart.x, chart.y + chart.height - border, chart.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(chart.x, chart.y, border, chart.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(chart.x + chart.width - border, chart.y, border, chart.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			}
		}
	}
	if (mainUi_.find("panel_toggle")->visible()) drawWidget(*mainUi_.find("panel_toggle"), palette, UiLayer);
	if (mainUi_.find("settings")->visible()) drawWidget(*mainUi_.find("settings"), palette, UiLayer);
	float titleLeft = 48.0f;
	float titleRight = static_cast<float>(width_) - 54.0f;
	if (classicVga && !windows95 && mainUi_.find("menu_tools")->visible())
		titleLeft = mainUi_.find("menu_tools")->bounds().x + mainUi_.find("menu_tools")->bounds().width + 12.0f;
	if (!mainUi_.find("settings")->visible()) titleRight = static_cast<float>(width_) - 12.0f;
	UiRect titleBounds(titleLeft, 5.0f, std::max(1.0f, titleRight - titleLeft), 22.0f);
	if (!windows95) drawText(*title_, titleBounds, 17.0f, palette.text, TextLayer, ClipBounds(), TextOverflowPolicy::Marquee);
	const char* menuIds[] = { "menu_file", "menu_view", "menu_tools" };
	for (std::size_t i = 0; i < sizeof(menuIds) / sizeof(menuIds[0]); ++i)
		drawWidget(*mainUi_.find(menuIds[i]), palette, UiLayer);
	if (!settings_.visual.panelExpanded)
	{
		const UiRect chart = chartViewport_;
		if (chart.width > 0.0f && chart.height > 0.0f)
		{
			// Draw a small pixel triangle instead of using a directional glyph so
			// orientation remains crisp and theme-independent at every font size.
			const float cell = 2.0f;
			const float triangleWidth = 14.0f;
			const float triangleX = chart.x + 10.0f;
			const float triangleY = chart.y + 7.0f;
			const glm::vec4 white(1.0f, 1.0f, 1.0f, 1.0f);
			for (int row = 0; row < 7; ++row)
			{
				const int rowFromTip = snapshot.direction == sortinglogic::SortDirection::Ascending ? row : 6 - row;
				const float rowWidth = static_cast<float>(rowFromTip + 1) * cell;
				const UiRect pixelRow(triangleX + (triangleWidth - rowWidth) * 0.5f,
					triangleY + static_cast<float>(row) * cell, rowWidth, cell);
				submitBox(pixelRow, white, MaterialId::PixelIcon, TextLayer);
			}
			const UiRect summaryBounds(triangleX + triangleWidth + 8.0f, chart.y + 3.0f,
				std::max(0.0f, chart.x + chart.width - (triangleX + triangleWidth + 16.0f)), 26.0f);
			drawText(*collapsedSummary_, summaryBounds, 20.0f, white, TextLayer,
				ClipBounds(chart.x, chart.y, chart.width, chart.height), TextOverflowPolicy::Marquee,
				TextHorizontalAlignment::Left);
		}
		if (mainUi_.find("start_overlay")->visible())
		{
			const UiRect chart = chartViewport_;
			const UiRect button = mainUi_.find("start_overlay")->bounds();
			const float cardWidth = std::min(480.0f, std::max(0.0f, chart.width - 24.0f));
			const UiRect card(std::max(0.0f, chart.x + (chart.width - cardWidth) * 0.5f),
				std::max(headerBounds_.height + 8.0f, button.y - 74.0f), cardWidth, 142.0f);
			submitBox(card, palette.panel, PanelMaterial, RenderLayer::MainControls);
			const glm::vec4 edge(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
			submitBox(UiRect(card.x, card.y, card.width, 2.0f), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(card.x, card.y + card.height - 2.0f, card.width, 2.0f), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			if (!labels_["#collapsed_empty_title"])
				labels_["#collapsed_empty_title"].reset(new TextLabel("No trace has been generated", "Courier New Bold", 16, SDL_Color{ 255, 255, 255, 255 }));
			if (!labels_["#collapsed_empty_hint"])
				labels_["#collapsed_empty_hint"].reset(new TextLabel("Start the simulation to reveal the event trace. Shortcut: Space", "Courier New Bold", 12, SDL_Color{ 255, 255, 255, 255 }));
			drawText(*labels_["#collapsed_empty_title"], UiRect(card.x + 8.0f, card.y + 12.0f, card.width - 16.0f, 22.0f),
				16.0f, palette.text, TextLayer);
			drawText(*labels_["#collapsed_empty_hint"], UiRect(card.x + 8.0f, card.y + 42.0f, card.width - 16.0f, 18.0f),
				12.0f, palette.mutedText, TextLayer);
			drawWidget(*mainUi_.find("start_overlay"), palette, UiLayer);
		}
		// The collapsed branch returns before the full control composition. Keep
		// menu popups in the upper overlay pass so the chart cannot hide them.
		for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
			if (mainUi_.widgets()[i]->visible() && mainUi_.widgets()[i]->expanded())
				drawWidget(*mainUi_.widgets()[i], palette, DropdownLayer, true);
		return;
	}
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		if (widget.id() != "panel_toggle" && widget.id() != "settings" &&
			widget.id() != "menu_file" && widget.id() != "menu_view" && widget.id() != "menu_tools" &&
			widget.visible())
		{
			drawWidget(widget, palette, UiLayer);
		}
	}
	if (classicVga && width_ >= 900 && layoutResult_.setupRail.height > 220.0f)
	{
		const UiRect rail = layoutResult_.setupRail;
		const UiRect seed = mainUi_.find("seed_readout")->bounds();
		const float cardY = std::max(seed.y + seed.height + 12.0f, rail.y + 258.0f);
		const float cardBottom = rail.y + rail.height - 10.0f;
		const float cardHeight = std::max(0.0f, cardBottom - cardY);
		if (cardHeight >= 78.0f)
		{
			algorithmProfileVisible_ = true;
			const UiRect card(rail.x + 8.0f, cardY, std::max(0.0f, rail.width - 16.0f), cardHeight);
			submitBox(card, palette.background, PanelMaterial, UiLayer);
			const glm::vec4 edge(palette.border.r, palette.border.g, palette.border.b, palette.border.a);
			const float border = 1.0f;
			submitBox(UiRect(card.x, card.y, card.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(card.x, card.y + card.height - border, card.width, border), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(card.x, card.y, border, card.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			submitBox(UiRect(card.x + card.width - border, card.y, border, card.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
			const UiAlgorithmCapabilities& capabilities = screenModel_.algorithmCapabilities;
			std::ostringstream inputTypes;
			inputTypes << "Supported input: ";
			if (capabilities.supportsNormalizedFloat) inputTypes << "FLOAT";
			if (capabilities.supportsNormalizedFloat && capabilities.supportsIntegerKeys) inputTypes << " / ";
			if (capabilities.supportsIntegerKeys) inputTypes << "INTEGER";
			if (!capabilities.supportsNormalizedFloat && !capabilities.supportsIntegerKeys) inputTypes << "NONE";
			std::ostringstream safeLimit;
			safeLimit << "Safe setup limit: " << capabilities.safeMaximumItemCount << " bars";
			if (!labels_["#rail_profile_title"])
			{
				labels_["#rail_profile_title"].reset(new TextLabel("ALGORITHM PROFILE", "Courier New Bold", 14, SDL_Color{ 255, 255, 255, 255 }));
				labels_["#rail_shortcuts_title"].reset(new TextLabel("KEYBOARD", "Courier New Bold", 12, SDL_Color{ 255, 255, 255, 255 }));
				labels_["#rail_shortcuts"].reset(new TextLabel("SPACE Play  |  1 Build  |  D Order", "Courier New Bold", 11, SDL_Color{ 255, 255, 255, 255 }));
				labels_["#rail_fullscreen"].reset(new TextLabel("ALT+ENTER Fullscreen", "Courier New Bold", 11, SDL_Color{ 255, 255, 255, 255 }));
			}
			if (algorithmProfileAlgorithm_ != screenModel_.algorithm)
			{
				algorithmProfileAlgorithm_ = screenModel_.algorithm;
				algorithmProfileScrollViewport_.setOffset(0.0f);
			}

			const float profileTextHeight = 13.0f;
			const ResolvedTextStyle profileTextStyle = resolveTextStyle(profileTextHeight);
			const ResolvedTextStyle complexityTextStyle = resolveTextStyle(11.0f);
			const bool showShortcuts = cardHeight >= 112.0f;
			float shortcutY = card.y + 70.0f;
			if (showShortcuts && cardHeight >= 150.0f) shortcutY = card.y + card.height - 58.0f;
			const float profileBottom = showShortcuts ? shortcutY - 10.0f : card.y + card.height - 8.0f;
			const float profileWidth = std::max(0.0f, card.width - 16.0f);
			const std::string complexity = (capabilities.available ? capabilities.timeComplexity : "Unknown") +
				std::string(" time  |  ") + (capabilities.available ? capabilities.spaceComplexity : "Unknown") + " space";
			algorithmProfileComplexityLayout_.update(complexity, profileWidth, complexityTextStyle);
			const std::vector<std::string>& complexityLines = algorithmProfileComplexityLayout_.lines();
			const float complexityLineHeight = 13.0f;
			const ClipBounds cardClip(card.x + 2.0f, card.y + 2.0f, std::max(0.0f, card.width - 4.0f),
				std::max(0.0f, card.height - 4.0f));
			drawText(*labels_["#rail_profile_title"], UiRect(card.x + 8.0f, card.y + 8.0f, profileWidth, 15.0f),
				12.0f, palette.text, TextLayer, cardClip, TextOverflowPolicy::Clamp, TextHorizontalAlignment::Center);
			for (std::size_t lineIndex = 0; lineIndex < complexityLines.size(); ++lineIndex)
			{
				const std::string labelId = "#rail_profile_complexity_" + std::to_string(lineIndex);
				if (!labels_[labelId]) labels_[labelId].reset(new TextLabel(complexityLines[lineIndex], "Courier New Bold", 11, SDL_Color{ 255, 255, 255, 255 }));
				else labels_[labelId]->setContent(complexityLines[lineIndex]);
				drawText(*labels_[labelId], UiRect(card.x + 8.0f, card.y + 27.0f + lineIndex * complexityLineHeight, profileWidth, complexityLineHeight),
					complexityTextStyle, palette.mutedText, TextLayer, cardClip, TextOverflowPolicy::Clamp, TextHorizontalAlignment::Center);
			}

			const float factsY = card.y + 30.0f + static_cast<float>(complexityLines.size()) * complexityLineHeight + 3.0f;
			const std::string properties = std::string(capabilities.stable ? "STABLE" : "UNSTABLE") + "  |  " +
				(capabilities.inPlace ? "IN-PLACE" : "AUXILIARY SPACE");
			const std::string factTexts[] = { properties, inputTypes.str(), safeLimit.str() };
			const char* factIds[] = { "#rail_profile_properties", "#rail_profile_input", "#rail_profile_limit" };
			const float factLineHeight = 18.0f;
			for (std::size_t factIndex = 0; factIndex < 3; ++factIndex)
			{
				if (cardHeight < factsY - card.y + (factIndex + 1) * factLineHeight + 8.0f) break;
				if (!labels_[factIds[factIndex]]) labels_[factIds[factIndex]].reset(new TextLabel(factTexts[factIndex], "Courier New Bold", 13, SDL_Color{ 255, 255, 255, 255 }));
				else labels_[factIds[factIndex]]->setContent(factTexts[factIndex]);
				drawText(*labels_[factIds[factIndex]], UiRect(card.x + 8.0f, factsY + factIndex * factLineHeight, profileWidth, factLineHeight),
					13.0f, palette.text, TextLayer, cardClip, TextOverflowPolicy::Clamp, TextHorizontalAlignment::Center);
			}

			const float descriptionTop = factsY + 3.0f * factLineHeight + 3.0f;
			const UiRect profileFrame(card.x + 6.0f, descriptionTop, std::max(0.0f, card.width - 12.0f),
				std::max(0.0f, profileBottom - descriptionTop));
			const UiRect profileTextViewport(profileFrame.x + 7.0f, profileFrame.y + 6.0f,
				std::max(0.0f, profileFrame.width - 24.0f), std::max(0.0f, profileFrame.height - 12.0f));
			if (!capabilities.behavior.empty() && profileTextViewport.height > 0.0f)
			{
				submitBox(profileFrame, palette.panel, ButtonMaterial, UiLayer);
				submitBox(UiRect(profileFrame.x, profileFrame.y, profileFrame.width, 1.0f), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(profileFrame.x, profileFrame.y + profileFrame.height - 1.0f, profileFrame.width, 1.0f), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(profileFrame.x, profileFrame.y, 1.0f, profileFrame.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(profileFrame.x + profileFrame.width - 1.0f, profileFrame.y, 1.0f, profileFrame.height), edge, ButtonMaterial, layerOffset(UiLayer, 1));
				algorithmProfileDescriptionLayout_.update(capabilities.behavior, profileTextViewport.width, profileTextStyle);
				const std::vector<std::string>& descriptionLines = algorithmProfileDescriptionLayout_.lines();
				algorithmProfileLineHeight_ = std::max(17.0f, profileTextStyle.logicalHeight * 1.42f);
				algorithmProfileScrollViewport_.configure(profileTextViewport,
					static_cast<float>(descriptionLines.size()) * algorithmProfileLineHeight_, algorithmProfileLineHeight_,
					algorithmProfileScrollViewport_.offset());
				const ClipBounds profileClip(profileTextViewport.x, profileTextViewport.y,
					profileTextViewport.width, profileTextViewport.height);
				const std::size_t firstLine = algorithmProfileScrollViewport_.firstVisibleItem(algorithmProfileLineHeight_);
				const std::size_t visibleLines = algorithmProfileScrollViewport_.visibleItemCount(descriptionLines.size(), algorithmProfileLineHeight_);
				const std::size_t endLine = std::min(descriptionLines.size(), firstLine + visibleLines);
				for (std::size_t lineIndex = firstLine; lineIndex < endLine; ++lineIndex)
				{
					const std::string labelId = "#rail_profile_line_" + std::to_string(lineIndex);
					if (!labels_[labelId]) labels_[labelId].reset(new TextLabel(descriptionLines[lineIndex], "Courier New Bold", 13, SDL_Color{ 255, 255, 255, 255 }));
					else labels_[labelId]->setContent(descriptionLines[lineIndex]);
					const float lineY = profileTextViewport.y + static_cast<float>(lineIndex) * algorithmProfileLineHeight_ - algorithmProfileScrollViewport_.offset();
					drawText(*labels_[labelId], UiRect(profileTextViewport.x, lineY, profileTextViewport.width, algorithmProfileLineHeight_),
						profileTextStyle, palette.text, TextLayer, profileClip, TextOverflowPolicy::Clamp, TextHorizontalAlignment::Left);
				}
			}
			else
			{
				algorithmProfileScrollViewport_.configure(UiRect(), 0.0f, profileTextHeight, 0.0f);
			}
			if (algorithmProfileScrollViewport_.maximumOffset() > 0.0f && profileFrame.height > 0.0f)
			{
				algorithmProfileScrollTrack_ = UiRect(profileFrame.x + profileFrame.width - 11.0f,
					profileTextViewport.y, 5.0f, profileTextViewport.height);
				submitBox(algorithmProfileScrollTrack_, palette.timelineRail, TimelineMaterial, UiLayer);
				const float thumbHeight = std::min(algorithmProfileScrollTrack_.height,
					std::max(18.0f, algorithmProfileScrollTrack_.height * profileTextViewport.height /
						std::max(1.0f, algorithmProfileScrollViewport_.contentExtent())));
				const float thumbTravel = std::max(0.0f, algorithmProfileScrollTrack_.height - thumbHeight);
				const float scrollFraction = algorithmProfileScrollViewport_.maximumOffset() > 0.0f
					? algorithmProfileScrollViewport_.offset() / algorithmProfileScrollViewport_.maximumOffset() : 0.0f;
				algorithmProfileScrollThumb_ = UiRect(algorithmProfileScrollTrack_.x,
					algorithmProfileScrollTrack_.y + thumbTravel * scrollFraction, algorithmProfileScrollTrack_.width, thumbHeight);
				submitBox(algorithmProfileScrollThumb_, palette.accent, MaterialId::TimelineThumb, layerOffset(UiLayer, 1));
			}
			if (showShortcuts)
			{
				const float separatorY = shortcutY - 8.0f;
				submitBox(UiRect(card.x + 8.0f, separatorY, std::max(0.0f, card.width - 16.0f), 1.0f), edge,
					ButtonMaterial, layerOffset(UiLayer, 1));
				drawText(*labels_["#rail_shortcuts_title"], UiRect(card.x + 8.0f, shortcutY, card.width - 16.0f, 15.0f),
					12.0f, palette.text, TextLayer);
				drawText(*labels_["#rail_shortcuts"], UiRect(card.x + 8.0f, shortcutY + 18.0f, card.width - 16.0f, 15.0f),
					11.0f, palette.mutedText, TextLayer);
				drawText(*labels_["#rail_fullscreen"], UiRect(card.x + 8.0f, shortcutY + 34.0f, card.width - 16.0f, 15.0f),
					11.0f, palette.mutedText, TextLayer);
			}
		}
	}
	if (snapshot.hasTrace || snapshot.buildingTrace || classicVga)
	{
		const glm::vec4 rail = palette.timelineRail;
		submitBox(timelineBounds_, rail, TimelineMaterial, UiLayer);
		const double timelineDuration = settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence
			? snapshot.audioEligibleEventCount / 300.0 : snapshot.durationSeconds;
		const float fraction = (!snapshot.hasTrace && !snapshot.buildingTrace) || timelineDuration <= 0.0 ? 0.0f :
			static_cast<float>(std::max(0.0, std::min(1.0, snapshot.elapsedSeconds / timelineDuration)));
		submitBox(UiRect(timelineBounds_.x, timelineBounds_.y, timelineBounds_.width * fraction, timelineBounds_.height), palette.accent, AccentMaterial, layerOffset(UiLayer, 1));
		if (classicVga)
		{
			const float tickHeight = std::min(7.0f, timelineBounds_.height * 0.45f);
			for (int tick = 0; tick <= 10; ++tick)
			{
				const float x = timelineBounds_.x + timelineBounds_.width * static_cast<float>(tick) / 10.0f;
				submitBox(UiRect(x, timelineBounds_.y + timelineBounds_.height - tickHeight, 1.0f, tickHeight),
					palette.text, MaterialId::PixelIcon, layerOffset(UiLayer, 2));
			}
		}
		const float thumbWidth = classicVga ? std::min(12.0f, timelineBounds_.width) : 8.0f;
		submitBox(UiRect(timelineBounds_.x + std::max(0.0f, timelineBounds_.width * fraction - thumbWidth * 0.5f),
			timelineBounds_.y - (classicVga ? 3.0f : 3.0f), thumbWidth, timelineBounds_.height + 6.0f),
			palette.timelineThumb, MaterialId::TimelineThumb, layerOffset(UiLayer, 1));
		if (timelineLeftArrowBounds_.width > 0.0f && timelineRightArrowBounds_.width > 0.0f)
		{
			const UiRect arrows[] = { timelineLeftArrowBounds_, timelineRightArrowBounds_ };
			for (int side = 0; side < 2; ++side)
			{
				const UiRect& arrow = arrows[side];
				submitBox(arrow, palette.button, ButtonMaterial, UiLayer);
				const glm::vec4 bright(windows95 ? 0.96f : palette.text.r, windows95 ? 0.96f : palette.text.g,
					windows95 ? 0.96f : palette.text.b, 1.0f);
				const glm::vec4 dark(windows95 ? 0.25f : palette.border.r, windows95 ? 0.25f : palette.border.g,
					windows95 ? 0.25f : palette.border.b, 1.0f);
				submitBox(UiRect(arrow.x, arrow.y, arrow.width, 1.0f), bright, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(arrow.x, arrow.y, 1.0f, arrow.height), bright, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(arrow.x, arrow.y + arrow.height - 1.0f, arrow.width, 1.0f), dark, ButtonMaterial, layerOffset(UiLayer, 1));
				submitBox(UiRect(arrow.x + arrow.width - 1.0f, arrow.y, 1.0f, arrow.height), dark, ButtonMaterial, layerOffset(UiLayer, 1));
				const char* leftArrow[] = { "..#", ".##", "###", ".##", "..#" };
				const char* rightArrow[] = { "#..", "##.", "###", "##.", "#.." };
				const char* const* pattern = side == 0 ? leftArrow : rightArrow;
				const float pixel = std::max(1.0f, std::min(2.0f, arrow.height / 8.0f));
				const float iconWidth = pixel * 3.0f;
				const float iconHeight = pixel * 5.0f;
				const float iconX = arrow.x + (arrow.width - iconWidth) * 0.5f;
				const float iconY = arrow.y + (arrow.height - iconHeight) * 0.5f;
				for (int row = 0; row < 5; ++row)
					for (int column = 0; column < 3; ++column)
						if (pattern[row][column] == '#')
							submitBox(UiRect(iconX + column * pixel, iconY + row * pixel, pixel, pixel),
								palette.text, MaterialId::PixelIcon, layerOffset(UiLayer, 2));
			}
		}
	}
	if (classicVga && layoutResult_.telemetry.width > 0.0f)
	{
		drawText(*detail_, UiRect(layoutResult_.telemetry.x, layoutResult_.telemetry.y,
			layoutResult_.telemetry.width, 14.0f), 12.0f, palette.text, TextLayer);
		drawText(*status_, UiRect(layoutResult_.telemetry.x, layoutResult_.telemetry.y + 14.0f,
			layoutResult_.telemetry.width, 14.0f), 11.0f, palette.mutedText, TextLayer);
	}
	else
	{
		const float detailY = timelineBounds_.y + 14.0f;
		drawText(*detail_, UiRect(16.0f, detailY, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.text, TextLayer);
		drawText(*status_, UiRect(16.0f, detailY + 19.0f, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.mutedText, TextLayer);
	}
	// Open combo options are submitted last within the UI layer, preserving their painter order over the chart.
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
		if (mainUi_.widgets()[i]->visible() && mainUi_.widgets()[i]->expanded())
			drawWidget(*mainUi_.widgets()[i], palette, DropdownLayer, true);
}

void ControlPanel::drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus)
{
	SettingsPanelChrome chrome;
	chrome.screenWidth = width_;
	chrome.screenHeight = height_;
	chrome.panelBounds = settingsPanelBounds_;
	chrome.contentBounds = settingsContentBounds_;
	chrome.title = settingsTitle_->text();
	SettingsPanelPalette chromePalette;
	chromePalette.panel = toUiColor(palette.panel);
	chromePalette.accent = toUiColor(palette.accent);
	chromePalette.text = toUiColor(palette.text);
	chromePalette.mutedText = toUiColor(palette.mutedText);
	chromePalette.background = toUiColor(palette.background);
	chromePalette.overlayDim = toUiColor(palette.overlayDim);
	if (settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio))
	{
		const SoundEngine::ReconstructedParameters& p = settings_.reconstructedAudio;
		const SoundEngine::ReconstructedParameters defaults;
		const bool referenceProfile = p.minimumFrequencyHz == defaults.minimumFrequencyHz &&
			p.maximumFrequencyHz == defaults.maximumFrequencyHz && p.mapping == defaults.mapping &&
			p.excitationClockHz == defaults.excitationClockHz && p.carrierCycles == defaults.carrierCycles &&
			p.tukeyFraction == defaults.tukeyFraction &&
			p.secondOperandDelayMicroseconds == defaults.secondOperandDelayMicroseconds &&
			!p.lowPassEnabled;
		bool neutralAlgorithmGains = true;
		for (std::map<sortinglogic::FamilyId, AlgorithmAudioLevels>::const_iterator family = settings_.familyAudioDefaults.begin();
			family != settings_.familyAudioDefaults.end(); ++family)
			for (std::size_t i = 0; i < family->second.decibels.size(); ++i)
				neutralAlgorithmGains = neutralAlgorithmGains && family->second.decibels[i] == 0.0f;
		for (std::map<std::string, AlgorithmAudioLevels>::const_iterator variant = settings_.variantAudioOffsets.begin();
			variant != settings_.variantAudioOffsets.end(); ++variant)
			for (std::size_t i = 0; i < variant->second.decibels.size(); ++i)
				neutralAlgorithmGains = neutralAlgorithmGains && variant->second.decibels[i] == 0.0f;
		const std::string profileStatus = std::string(referenceProfile && neutralAlgorithmGains
			? "Reconstructed reference | " : "Customized | ") + audioStatus;
		audioDeviceStatus_->setContent(profileStatus);
		chrome.footer = profileStatus;
		chrome.footerBounds = UiRect(settingsPanelBounds_.x + 16.0f,
			settingsPanelBounds_.y + settingsPanelBounds_.height - 62.0f,
			settingsPanelBounds_.width - 130.0f, 16.0f);
	}
	else
	{
		chrome.footer = settingsSaveStatus_->text();
		chrome.footerBounds = UiRect(settingsPanelBounds_.x + 16.0f,
			settingsPanelBounds_.y + settingsPanelBounds_.height - 58.0f,
			settingsPanelBounds_.width - 120.0f, 16.0f);
	}
	auto renderCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
	DispatchingUiPaintContext<decltype(renderCommand)> paintContext(renderCommand);
	settingsPanelView_.compose(chrome, chromePalette, paintContext);
	const UiWidget& appearanceTab = *settingsUi_.find("appearance_tab");
	const UiWidget& algorithmTab = *settingsUi_.find("algorithm_tab");
	const UiWidget& audioTab = *settingsUi_.find("audio_tab");
	const RenderLayer controlLayer = RenderLayer::SettingsControls;
	const UiWidget* tabs[] = { &appearanceTab, &algorithmTab, &audioTab };
	for (int tab = 0; tab < 3; ++tab)
	{
		submitBox(tabs[tab]->bounds(), tab == settingsPanelModel_.pageIndex() ? palette.accent : palette.button, ButtonMaterial, controlLayer);
		drawWidget(*tabs[tab], palette, controlLayer);
	}
	const UiRect activeTab = tabs[settingsPanelModel_.pageIndex()]->bounds();
	submitBox(UiRect(activeTab.x, activeTab.y + activeTab.height - 2.0f, activeTab.width, 2.0f),
		palette.accent, AccentMaterial, layerOffset(controlLayer, 1));
	if (settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio))
	{
		const UiWidget* audioGroups[] = { settingsUi_.find("audio_basic_tab"), settingsUi_.find("audio_advanced_tab") };
		for (int i = 0; i < 2; ++i)
		{
			submitBox(audioGroups[i]->bounds(), ((settingsPanelModel_.audioGroup() == SettingsAudioGroup::Advanced) == (i == 1)) ? palette.accent : palette.button,
				ButtonMaterial, controlLayer);
			drawWidget(*audioGroups[i], palette, controlLayer);
		}
	}
	for (std::size_t i = 0; i < settingsUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *settingsUi_.widgets()[i];
		if (!widget.visible() || widget.id() == "appearance_tab" || widget.id() == "algorithm_tab" ||
			widget.id() == "audio_tab" || widget.id() == "audio_basic_tab" || widget.id() == "audio_advanced_tab" ||
			widget.id() == "settings_close" || widget.id() == "audio_retry") continue;
		drawSettingsControl(widget, palette);
	}
	drawWidget(*settingsUi_.find("settings_close"), palette, RenderLayer::SettingsControls);
	if (settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio))
	{
		drawWidget(*settingsUi_.find("audio_retry"), palette, RenderLayer::SettingsControls);
	}
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
	drawSliderControl(widget, palette, clip, RenderLayer::SettingsControls);
}

void ControlPanel::drawSliderControl(const UiWidget& widget, const ThemePalette& palette, const ClipBounds& clip, RenderLayer layer)
{
	std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
	if (label != labels_.end())
	{
		if (drawingAppearanceEditor_)
		{
			std::ostringstream caption;
			caption << widget.label() << "  " << std::fixed << std::setprecision(2) << widget.value();
			label->second->setContent(caption.str());
		}
		drawText(*label->second, UiRect(widget.bounds().x, widget.bounds().y + 1.0f, widget.bounds().width, 16.0f),
			11.0f, palette.text, SettingsTextLayer, clip);
	}
	const float fraction = widget.maximum() <= widget.minimum() ? 0.0f :
		(widget.value() - widget.minimum()) / (widget.maximum() - widget.minimum());
	const float railY = widget.bounds().y + 28.0f;
	const bool dos = widget.presentation() == UiWidgetPresentation::DosScrollbar;
	const float arrow = dos ? std::min(widget.scrollbarArrowSize(), widget.bounds().width * 0.25f) : 2.0f;
	const float dosThickness = std::min(themeNumber(renderPreferences_, "scrollbar.thickness", 8.0f),
		std::max(1.0f, widget.bounds().height - 29.0f));
	const UiRect rail = dos && widget.interactiveSubregions().size() >= 3
		? widget.interactiveSubregions()[1]
		: UiRect(widget.bounds().x + arrow, railY, std::max(1.0f, widget.bounds().width - 2.0f * arrow), dos ? dosThickness : 4.0f);
	submitBox(rail, palette.timelineRail, TimelineMaterial, layer, clip);
	submitBox(UiRect(rail.x, rail.y, rail.width * fraction, rail.height), palette.accent, AccentMaterial, layer, clip);
	const float thumbWidth = dos ? std::max(8.0f, std::min(widget.bounds().width,
		themeNumber(renderPreferences_, "scrollbar.thumb_min_size", 20.0f))) : 10.0f;
	const float knobX = rail.x + rail.width * fraction - thumbWidth * 0.5f;
	submitBox(UiRect(knobX, rail.y - (dos ? 1.0f : 3.0f), thumbWidth, dos ? 10.0f : 10.0f), widget.focused() ? palette.held : palette.text,
		MaterialId::TimelineThumb, layer, clip);
	if (dos && arrow > 0.0f)
	{
		const float side = std::max(10.0f, arrow);
		const UiRect leftArrow = widget.interactiveSubregions().size() >= 3 ? widget.interactiveSubregions()[0]
			: UiRect(widget.bounds().x, widget.bounds().y + 27.0f, side, 12.0f);
		const UiRect rightArrow = widget.interactiveSubregions().size() >= 3 ? widget.interactiveSubregions()[2]
			: UiRect(widget.bounds().x + widget.bounds().width - side, widget.bounds().y + 27.0f, side, 12.0f);
		submitBox(leftArrow, palette.button, ButtonMaterial, layer, clip);
		submitBox(rightArrow, palette.button, ButtonMaterial, layer, clip);
		const float centerY = leftArrow.y + leftArrow.height * 0.5f;
		submitBox(UiRect(leftArrow.x + side * 0.35f, centerY, side * 0.30f, 1.0f), palette.text, MaterialId::PixelIcon, layer, clip);
		submitBox(UiRect(rightArrow.x + side * 0.35f, centerY, side * 0.30f, 1.0f), palette.text, MaterialId::PixelIcon, layer, clip);
		submitBox(UiRect(rightArrow.x + side * 0.47f, centerY - 2.0f, 1.0f, 5.0f), palette.text, MaterialId::PixelIcon, layer, clip);
	}
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
			const std::string labelId = widget.id() + "#option" + std::to_string(option);
			std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(labelId);
			if (label == labels_.end())
			{
				labels_[labelId].reset(new TextLabel(widget.options()[option], "Courier New Bold", 14, { 255, 255, 255, 255 }));
				label = labels_.find(labelId);
			}
			label->second->setContent(widget.options()[option]);
			drawText(*label->second, UiRect(bounds.x + 2.0f, bounds.y, bounds.width - 4.0f, bounds.height), 13.0f,
				widget.optionEnabled(option) ? palette.text : palette.mutedText, layerOffset(layer, 1), screenClip,
				TextOverflowPolicy::Marquee);
		}
		return;
	}
	if (!widget.visible()) return;
	const UiPresetId preset = drawingAppearanceEditor_ ? appearanceEditorModel_.preset() : renderPreferences_.appearance.activePreset;
	const UiThemeCustomization readableEditorTheme = defaultUiThemeCustomization(preset, PaletteId::Monochrome);
	const UiThemeCustomization& paintTheme = drawingAppearanceEditor_ ? readableEditorTheme : activeTheme(renderPreferences_);
	const bool playbackActive = widget.role() == UiControlRole::Playback && screenModel_.transport == UiTransportState::Playing;
	const std::string role = widget.role() == UiControlRole::BuildTrace
		? (preset == UiPresetId::FlatVgaDos ? "button" : "run") :
		playbackActive ? "play-active" : widget.role() == UiControlRole::Playback ? "play" :
		widget.role() == UiControlRole::Reset ? "reset" : widget.role() == UiControlRole::EditableValue ? "value" :
		widget.role() == UiControlRole::Choice || widget.role() == UiControlRole::AlgorithmChoice ? "choice" : "button";
	const UiControlPaintStyle style = resolveUiControlPaintStyle(preset, paintTheme, role,
		widget.enabled(), widget.hovered(), widget.pressed(), widget.expanded(),
		paletteColors(drawingAppearanceEditor_ ? paletteForAppearance(UiPresetId::Current, readableEditorTheme) : palette));
	const glm::vec4 fill(style.fill.r, style.fill.g, style.fill.b, style.fill.a);
	const bool classicMenu = preset == UiPresetId::ClassicWindows95 &&
		(widget.id() == "menu_file" || widget.id() == "menu_view" || widget.id() == "menu_tools");
	if (!classicMenu || widget.hovered() || widget.expanded())
		submitBox(widget.bounds(), classicMenu ? palette.accent : fill,
			widget.kind() == UiWidgetKind::ComboBox ? ValueMaterial : ButtonMaterial, layer, screenClip);
	const float border = std::max(0.0f, style.borderWidth);
	if (border > 0.0f && !classicMenu)
	{
		const glm::vec4 edge(style.border.r, style.border.g, style.border.b, style.border.a);
		submitBox(UiRect(widget.bounds().x, widget.bounds().y, widget.bounds().width, border), edge, ButtonMaterial, layerOffset(layer, 1), screenClip);
		submitBox(UiRect(widget.bounds().x, widget.bounds().y + widget.bounds().height - border, widget.bounds().width, border), edge, ButtonMaterial, layerOffset(layer, 1), screenClip);
		submitBox(UiRect(widget.bounds().x, widget.bounds().y, border, widget.bounds().height), edge, ButtonMaterial, layerOffset(layer, 1), screenClip);
		submitBox(UiRect(widget.bounds().x + widget.bounds().width - border, widget.bounds().y, border, widget.bounds().height), edge, ButtonMaterial, layerOffset(layer, 1), screenClip);
		if (preset == UiPresetId::FlatVgaDos)
		{
			const glm::vec4 highlight(std::min(1.0f, edge.r * 1.18f + 0.12f),
				std::min(1.0f, edge.g * 1.18f + 0.12f), std::min(1.0f, edge.b * 1.18f + 0.12f), edge.a);
			const glm::vec4 shadow(fill.r * 0.42f, fill.g * 0.42f, fill.b * 0.42f, 1.0f);
			submitBox(UiRect(widget.bounds().x, widget.bounds().y, widget.bounds().width, border), highlight, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x, widget.bounds().y, border, widget.bounds().height), highlight, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x, widget.bounds().y + widget.bounds().height - border, widget.bounds().width, border), shadow, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x + widget.bounds().width - border, widget.bounds().y, border, widget.bounds().height), shadow, ButtonMaterial, layerOffset(layer, 2), screenClip);
		}
		else if (preset == UiPresetId::ClassicWindows95)
		{
			const glm::vec4 light(0.96f, 0.96f, 0.96f, 1.0f);
			const glm::vec4 shadow(0.25f, 0.25f, 0.25f, 1.0f);
			const glm::vec4 topLeft = widget.pressed() ? shadow : light;
			const glm::vec4 bottomRight = widget.pressed() ? light : shadow;
			submitBox(UiRect(widget.bounds().x, widget.bounds().y, widget.bounds().width, border), topLeft, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x, widget.bounds().y, border, widget.bounds().height), topLeft, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x, widget.bounds().y + widget.bounds().height - border, widget.bounds().width, border), bottomRight, ButtonMaterial, layerOffset(layer, 2), screenClip);
			submitBox(UiRect(widget.bounds().x + widget.bounds().width - border, widget.bounds().y, border, widget.bounds().height), bottomRight, ButtonMaterial, layerOffset(layer, 2), screenClip);
		}
	}
	if (widget.focused())
		submitBox(UiRect(widget.bounds().x, widget.bounds().y, widget.bounds().width, std::max(1.0f, border)), palette.held, AccentMaterial, layerOffset(layer, 1), screenClip);
	if (widget.presentation() == UiWidgetPresentation::RadioGroup)
	{
		const std::size_t count = widget.options().size();
		if (count == 0) return;
		const float cellWidth = widget.bounds().width / static_cast<float>(count);
		for (std::size_t option = 0; option < count; ++option)
		{
			const UiRect cell(widget.bounds().x + static_cast<float>(option) * cellWidth, widget.bounds().y, cellWidth, widget.bounds().height);
			const bool selected = static_cast<int>(option) == widget.selectedIndex();
			const glm::vec4 cellFill = !widget.optionEnabled(option) ? palette.disabledButton : selected ? palette.accent : palette.button;
			submitBox(cell, cellFill, ButtonMaterial, layerOffset(layer, 1), screenClip);
			if (style.squareIndicator)
			{
				const UiTokenValue indicatorToken = effectiveUiTokenValue(preset, paintTheme, "selection.indicator_size");
				const float requestedIndicator = indicatorToken.type == UiTokenType::Number
					? static_cast<float>(indicatorToken.number) : 12.0f;
				const float indicator = std::min(requestedIndicator, std::max(1.0f, cell.height - 8.0f));
				const float ix = cell.x + 7.0f;
				const float iy = cell.y + (cell.height - indicator) * 0.5f;
				submitBox(UiRect(ix, iy, indicator, indicator), selected ? palette.text : palette.panel, ButtonMaterial, layerOffset(layer, 2), screenClip);
				if (selected && indicator > 4.0f)
					submitBox(UiRect(ix + 3.0f, iy + 3.0f, indicator - 6.0f, indicator - 6.0f), palette.accent, ButtonMaterial, layerOffset(layer, 3), screenClip);
			}
			std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id() + "#option" + std::to_string(option));
			if (label != labels_.end()) drawText(*label->second, UiRect(cell.x + (style.squareIndicator ? 22.0f : 2.0f), cell.y,
				cell.width - (style.squareIndicator ? 24.0f : 4.0f), cell.height), 13.0f,
				widget.optionEnabled(option) ? palette.text : palette.mutedText, layerOffset(layer, 2), screenClip,
				TextOverflowPolicy::Marquee);
		}
		return;
	}
	if (widget.role() == UiControlRole::PanelToggle)
	{
		if (preset == UiPresetId::FlatVgaDos)
		{
			std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
			if (label != labels_.end()) drawText(*label->second, widget.bounds(), 14.0f, palette.text, layerOffset(layer, 2), screenClip);
			return;
		}
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
	if (widget.role() == UiControlRole::Settings)
	{
		if (preset == UiPresetId::FlatVgaDos)
		{
			std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
			if (label != labels_.end()) drawText(*label->second, widget.bounds(), 13.0f, palette.text, layerOffset(layer, 2), screenClip);
			return;
		}
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
	if (widget.role() == UiControlRole::AlgorithmChoice)
	{
		const sortinglogic::AlgorithmDescriptor* descriptor = sortinglogic::algorithmDescriptor(
			static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
		if (descriptor)
		{
			const std::string variantKey("#algorithm_variant_button");
			if (!labels_[variantKey]) labels_[variantKey].reset(new TextLabel("", "Courier New Bold", 10, SDL_Color{ 238, 246, 249, 255 }));
			labels_[variantKey]->setContent(descriptor->displayName);
			drawText(*labels_[variantKey], widget.bounds(), 14.0f, palette.text, layerOffset(layer, 2), screenClip,
				TextOverflowPolicy::Marquee);
			return;
		}
	}
	if (style.drawArrow && preset != UiPresetId::Current && widget.kind() == UiWidgetKind::ComboBox && !classicMenu)
	{
		const UiTokenValue arrowToken = effectiveUiTokenValue(preset, paintTheme, "control.arrow_size");
		const float arrow = arrowToken.type == UiTokenType::Number ? static_cast<float>(arrowToken.number) : 6.0f;
		const float cx = widget.bounds().x + widget.bounds().width - arrow - 7.0f;
		const float cy = widget.bounds().y + widget.bounds().height * 0.5f;
		for (int row = 0; row < 3; ++row)
			submitBox(UiRect(cx + row, cy - 1.0f + row, std::max(1.0f, arrow - row * 2.0f), 1.0f),
				palette.text, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
	}
	if (preset == UiPresetId::ClassicWindows95 &&
		(widget.id() == "play" || widget.id() == "previous" || widget.id() == "next" || widget.id() == "reset"))
	{
		static const char* playIcon[] = { "..#....", "..##...", "..###..", "..##...", "..#...." };
		static const char* pauseIcon[] = { ".##.##.", ".##.##.", ".##.##.", ".##.##.", ".##.##." };
		static const char* previousIcon[] = { "#...#..", "#..##..", "#.###..", "#..##..", "#...#.." };
		static const char* nextIcon[] = { "..#...#", "..##..#", ".###.#.", "..##..#", "..#...#" };
		static const char* resetIcon[] = { ".#####.", "##...#.", "#..##..", "#..##..", ".#...##", ".#####." };
		const char* const* icon = widget.id() == "reset" ? resetIcon :
			widget.id() == "previous" ? previousIcon : widget.id() == "next" ? nextIcon :
			(screenModel_.transport == UiTransportState::Playing ? pauseIcon : playIcon);
		const int rows = widget.id() == "reset" ? 6 : 5;
		const int columns = 7;
		const float pixel = std::max(2.0f, std::min(4.0f, widget.bounds().height / 14.0f));
		const float iconX = widget.bounds().x + 8.0f;
		const float iconY = widget.bounds().y + (widget.bounds().height - rows * pixel) * 0.5f;
		const glm::vec4 iconColor = widget.id() == "play" ? palette.writeTarget :
			widget.id() == "reset" ? palette.shifted : palette.bar;
		for (int row = 0; row < rows; ++row)
			for (int column = 0; column < columns; ++column)
				if (icon[row][column] == '#')
					submitBox(UiRect(iconX + column * pixel, iconY + row * pixel, pixel - 0.5f, pixel - 0.5f),
						iconColor, MaterialId::PixelIcon, layerOffset(layer, 2), screenClip);
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
		if (label != labels_.end())
			drawText(*label->second, UiRect(iconX + columns * pixel + 6.0f, widget.bounds().y,
				widget.bounds().x + widget.bounds().width - iconX - columns * pixel - 10.0f, widget.bounds().height),
				13.0f, palette.text, layerOffset(layer, 2), screenClip);
		return;
	}
	std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
	if (label != labels_.end())
	{
		UiRect labelBounds = widget.bounds();
		if (style.drawArrow && widget.kind() == UiWidgetKind::ComboBox)
			labelBounds.width = std::max(1.0f, labelBounds.width - themeNumber(renderPreferences_, "control.arrow_size", 6.0f) - 12.0f);
		drawText(*label->second, labelBounds, widget.kind() == UiWidgetKind::ComboBox ? 13.0f : 14.0f,
			palette.text, layerOffset(layer, 2), screenClip, TextOverflowPolicy::Marquee);
	}
}

ResolvedTextStyle ControlPanel::resolveTextStyle(float targetHeight) const
{
	const UiTokenValue fontValue = effectiveUiTokenValue(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_), "typeface.primary");
	const UiFontReference font = drawingAppearanceEditor_ ? UiFontReference() :
		fontValue.type == UiTokenType::Font ? fontValue.font : UiFontReference();
	if (!drawingAppearanceEditor_)
	{
		const char* sizeToken = targetHeight >= 16.0f ? "typography.title_size" :
			targetHeight >= 14.0f ? "typography.control_size" :
			targetHeight >= 12.0f ? "typography.body_size" : "typography.caption_size";
		const float factorySize = targetHeight >= 16.0f ? 18.0f :
			targetHeight >= 14.0f ? 16.0f : targetHeight >= 12.0f ? 14.0f : 12.0f;
		targetHeight *= themeNumber(renderPreferences_, sizeToken, factorySize) / factorySize;
	}
	const int logicalWidth = window_.getLogicalWidth();
	const int dpi = logicalWidth > 0 ? std::max(48, std::min(384,
		static_cast<int>(std::lround(96.0 * window_.getDrawableWidth() / logicalWidth)))) : 96;
	// SDL_ttf interprets its size argument in points. With the matching output
	// DPI, 72 points per inch yields the requested logical pixel height directly;
	// doubling the size and shrinking the texture made every label visibly soft.
	const int rasterSize = std::max(8, static_cast<int>(std::lround(targetHeight * 72.0f / 96.0f)));
	return ResolvedTextStyle(font, rasterSize, dpi, targetHeight);
}

void ControlPanel::drawText(TextLabel& label, const UiRect& bounds, float targetHeight, const glm::vec4& tint,
	RenderLayer layer, const ClipBounds& clip, TextOverflowPolicy overflow, TextHorizontalAlignment alignment)
{
	drawText(label, bounds, resolveTextStyle(targetHeight), tint, layer, clip, overflow, alignment);
}

void ControlPanel::drawText(TextLabel& label, const UiRect& bounds, const ResolvedTextStyle& style, const glm::vec4& tint,
	RenderLayer layer, const ClipBounds& clip, TextOverflowPolicy overflow, TextHorizontalAlignment alignment)
{
	label.setText(label.text(), style, SDL_Color{ 255, 255, 255, 255 });
	fontFallbackActive_ = fontFallbackActive_ || label.usingFallbackFont();
	const glm::vec2 dimensions = label.getTextureDimensions();
	if (label.getTextureId() == 0 || dimensions.x <= 0.0f || dimensions.y <= 0.0f || bounds.width <= 0.0f || bounds.height <= 0.0f)
		return;
	const TextQuadLayout textLayout = layoutTextQuad(ClipBounds(bounds.x, bounds.y, bounds.width, bounds.height),
		dimensions, style.logicalHeight, overflow, SDL_GetTicks(), clip, alignment);
	if (!textLayout.visible) return;
	renderer_.submit(QuadInstance(textLayout.position, textLayout.size,
		RenderDescriptor(tint, label.getTextureId(), MaterialId::Text), layer, textLayout.clip));
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
	algorithmBrowser_.begin(current ? current->familyId : sortinglogic::FamilyId());
	mainUi_.closeCombo();
	SDL_StartTextInput();
	layoutAlgorithmMenu();
}

void ControlPanel::closeAlgorithmMenu()
{
	algorithmBrowser_.close();
	SDL_StopTextInput();
}

void ControlPanel::beginInlineEdit(const std::string& widgetId)
{
	if ((widgetId == "appearance_value" || widgetId == "appearance_color_alpha") && appearanceEditorOpen_)
	{
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(appearanceEditorModel_.preset()), appearanceEditorModel_.selectedToken());
		if (!descriptor || (widgetId == "appearance_value" && descriptor->type != UiTokenType::Number) ||
			(widgetId == "appearance_color_alpha" && descriptor->type != UiTokenType::Color)) return;
		const UiTokenValue value = effectiveUiTokenValue(appearanceEditorModel_.preset(),
			appearanceDraft_.themes[appearanceEditorModel_.preset()], appearanceEditorModel_.selectedToken());
		std::ostringstream text;
		text << std::setprecision(9) << (widgetId == "appearance_color_alpha" ? value.color.a : value.number);
		inlineEditWidgetId_ = widgetId;
		inlineEditBuffer_ = text.str();
		inlineEditAppearanceValue_ = true;
		inlineEditReplaceOnNextDigit_ = true;
		SDL_StartTextInput();
		updateInlineEditLabel();
		return;
	}
	if (widgetId != "bars_value" && widgetId != "time_value") return;
	inlineEditWidgetId_ = widgetId;
	inlineEditAppearanceValue_ = false;
	inlineEditBuffer_ = widgetId == "bars_value"
		? std::to_string(settings_.simulation.itemCount)
		: ([this]() { std::ostringstream value; value << std::setprecision(6) << settings_.simulation.durationSeconds; return value.str(); })();
	inlineEditReplaceOnNextDigit_ = true;
	SDL_StartTextInput();
	updateInlineEditLabel();
}

void ControlPanel::updateInlineEditLabel()
{
	if (inlineEditWidgetId_.empty()) return;
	if (inlineEditAppearanceValue_)
	{
		UiWidget* value = appearanceUi_.find(inlineEditWidgetId_);
		if (value) value->setLabel(inlineEditBuffer_ + "|");
		return;
	}
	UiWidget* widget = mainUi_.find(inlineEditWidgetId_);
	if (!widget) return;
	widget->setLabel(inlineEditBuffer_ + "|" + (inlineEditWidgetId_ == "bars_value" ? " bars" : " sec"));
}

bool ControlPanel::appendInlineEditText(const char* text)
{
	if (!text) return true;
	for (const char* character = text; *character; ++character)
	{
		const bool digit = *character >= '0' && *character <= '9';
		const bool decimal = *character == '.' && (inlineEditAppearanceValue_ || inlineEditWidgetId_ == "time_value");
		const bool sign = *character == '-' && inlineEditAppearanceValue_;
		if (!digit && !decimal && !sign) continue;
		if (inlineEditReplaceOnNextDigit_)
		{
			inlineEditBuffer_.clear();
			inlineEditReplaceOnNextDigit_ = false;
		}
		if (inlineEditBuffer_.size() < 10) inlineEditBuffer_ += *character;
	}
	updateInlineEditLabel();
	return true;
}

bool ControlPanel::commitInlineEdit(AppAction& action)
{
	if (inlineEditAppearanceValue_)
	{
		char* end = nullptr;
		const double parsed = std::strtod(inlineEditBuffer_.c_str(), &end);
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(appearanceEditorModel_.preset()), appearanceEditorModel_.selectedToken());
		if (!descriptor || !end || end == inlineEditBuffer_.c_str() || *end != '\0' || !std::isfinite(parsed) ||
			parsed < descriptor->minimum || parsed > descriptor->maximum)
		{
			appearanceError_ = descriptor ? "Enter a value between " + std::to_string(descriptor->minimum) + " and " +
				std::to_string(descriptor->maximum) + "." : "The selected appearance property is unavailable.";
			return false;
		}
		std::string error;
		UiTokenValue nextValue;
		if (inlineEditWidgetId_ == "appearance_color_alpha")
		{
			nextValue = effectiveUiTokenValue(appearanceEditorModel_.preset(),
				appearanceDraft_.themes[appearanceEditorModel_.preset()], appearanceEditorModel_.selectedToken());
			if (nextValue.type != UiTokenType::Color) return false;
			nextValue.color.a = static_cast<float>(parsed);
		}
		else nextValue = UiTokenValue::fromNumber(parsed);
		if (!setUiTokenValue(appearanceEditorModel_.preset(), appearanceDraft_.themes[appearanceEditorModel_.preset()],
			appearanceEditorModel_.selectedToken(), nextValue, &error))
		{
			appearanceError_ = error;
			return false;
		}
		inlineEditWidgetId_.clear(); inlineEditBuffer_.clear(); inlineEditAppearanceValue_ = false;
		inlineEditReplaceOnNextDigit_ = false; SDL_StopTextInput();
		appearanceError_.clear(); updateAppearanceEditorControls();
		return true;
	}
	if (inlineEditWidgetId_ == "bars_value")
	{
		int value = 0;
		if (!parseInlineInteger(inlineEditBuffer_, value)) return false;
		action = AppAction(AppActionKind::SetItemCount, 0.0f, value);
	}
	else if (inlineEditWidgetId_ == "time_value")
	{
		char* end = nullptr;
		const float value = std::strtof(inlineEditBuffer_.c_str(), &end);
		if (!end || end == inlineEditBuffer_.c_str() || *end != '\0' || !std::isfinite(value)) return false;
		action = AppAction(AppActionKind::SetDuration, value);
	}
	else return false;
	inlineEditWidgetId_.clear();
	inlineEditBuffer_.clear();
	inlineEditAppearanceValue_ = false;
	inlineEditReplaceOnNextDigit_ = false;
	SDL_StopTextInput();
	return true;
}

void ControlPanel::cancelInlineEdit()
{
	if (inlineEditWidgetId_.empty()) return;
	if (inlineEditAppearanceValue_)
	{
		inlineEditWidgetId_.clear(); inlineEditBuffer_.clear(); inlineEditAppearanceValue_ = false;
		inlineEditReplaceOnNextDigit_ = false; SDL_StopTextInput();
		updateAppearanceEditorControls();
		return;
	}
	const bool editingBarCount = inlineEditWidgetId_ == "bars_value";
	inlineEditWidgetId_.clear();
	inlineEditBuffer_.clear();
	inlineEditReplaceOnNextDigit_ = false;
	SDL_StopTextInput();
	UiWidget* widget = mainUi_.find(editingBarCount ? "bars_value" : "time_value");
	if (widget)
		widget->setLabel(editingBarCount
			? std::to_string(settings_.simulation.itemCount) + " bars"
			: formatDuration(settings_.simulation.durationSeconds));
}

void ControlPanel::openAppearanceEditor()
{
	if (appearanceEditorOpen_) return;
	appearanceWasSettingsOpen_ = settingsOpen_;
	appearanceDraft_ = settings_.appearance;
	appearanceEditorModel_.begin(appearanceDraft_.activePreset);
	appearanceError_.clear();
	settingsOpen_ = true;
	settingsPanelModel_.showPage(SettingsPanelPage::Appearance);
	appearanceEditorOpen_ = true;
	appearanceSearchFocused_ = false;
	mainUi_.closeCombo();
	settingsUi_.closeCombo();
	appearanceUi_.closeCombo();
	mainUi_.setInputCaptured(true);
	settingsUi_.setInputCaptured(false);
	appearanceUi_.setInputCaptured(true);
	refreshAppearanceTokens();
	updateAppearanceEditorControls();
	layoutAppearanceEditor();
}

void ControlPanel::closeAppearanceEditor(bool restore)
{
	if (!appearanceEditorOpen_) return;
	if (!inlineEditWidgetId_.empty()) cancelInlineEdit();
	if (appearanceSearchFocused_)
	{
		appearanceSearchFocused_ = false;
		SDL_StopTextInput();
	}
	if (restore)
	{
		appearanceDraft_ = settings_.appearance;
		appearanceEditorModel_.setPreset(appearanceDraft_.activePreset);
	}
	appearanceEditorOpen_ = false;
	appearanceUi_.closeCombo();
	appearanceUi_.setInputCaptured(false);
	settingsOpen_ = appearanceWasSettingsOpen_;
	settingsUi_.setInputCaptured(settingsOpen_);
	mainUi_.setInputCaptured(settingsOpen_);
	layoutCacheValid_ = false;
}

void ControlPanel::refreshAppearanceTokens()
{
	std::vector<std::string> options;
	const std::vector<AppearanceTokenOption>& tokenOptions = appearanceEditorModel_.options();
	for (std::size_t i = 0; i < tokenOptions.size(); ++i) options.push_back(tokenOptions[i].label);
	UiWidget* selector = appearanceUi_.find("appearance_token");
	if (selector)
	{
		selector->setOptions(options);
		selector->setSelectedIndex(std::max(0, appearanceEditorModel_.selectedIndex()));
		for (std::size_t i = 0; i < options.size(); ++i)
		{
			const std::string labelId = selector->id() + "#option" + std::to_string(i);
			if (labels_.find(labelId) == labels_.end()) labels_[labelId].reset(new TextLabel(options[i], "Courier New Bold", 12, { 255, 255, 255, 255 }));
			else labels_[labelId]->setContent(options[i]);
		}
	}
	updateAppearanceEditorControls();
}

void ControlPanel::updateAppearanceEditorControls()
{
	if (!appearanceEditorOpen_) return;
	const UiPresetId preset = appearanceEditorModel_.preset();
	const std::string& selectedToken = appearanceEditorModel_.selectedToken();
	const bool hasTokenOptions = !appearanceEditorModel_.options().empty();
	appearanceUi_.find("appearance_preset")->setSelectedIndex(static_cast<int>(preset));
	UiThemeCustomization& theme = appearanceDraft_.themes[preset];
	appearanceUi_.find("appearance_palette")->setSelectedIndex(static_cast<int>(theme.palette));
	appearanceUi_.find("appearance_category")->setSelectedIndex(appearanceEditorModel_.categoryFilter());
	appearanceUi_.find("appearance_search")->setLabel(appearanceEditorModel_.search().empty()
		? "Search tokens" : "Search: " + appearanceEditorModel_.search());
	UiWidget* token = appearanceUi_.find("appearance_token");
	if (token) token->setSelectedIndex(std::max(0, appearanceEditorModel_.selectedIndex()));

	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	const bool hasToken = descriptor != nullptr;
	const UiTokenValue value = hasToken ? effectiveUiTokenValue(preset, theme, selectedToken) : UiTokenValue();
	appearanceUi_.setWidgetVisible("appearance_token", hasTokenOptions);
	appearanceUi_.setWidgetVisible("appearance_value_down", hasToken && descriptor->type == UiTokenType::Number);
	appearanceUi_.setWidgetVisible("appearance_value", hasToken && descriptor->type == UiTokenType::Number);
	appearanceUi_.setWidgetVisible("appearance_value_up", hasToken && descriptor->type == UiTokenType::Number);
	appearanceUi_.setWidgetVisible("appearance_value_slider", hasToken && descriptor->type == UiTokenType::Number);
	appearanceUi_.setWidgetVisible("appearance_color", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_color_alpha", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_color_r", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_color_g", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_color_b", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_color_a", hasToken && descriptor->type == UiTokenType::Color);
	appearanceUi_.setWidgetVisible("appearance_boolean", hasToken && descriptor->type == UiTokenType::Boolean);
	appearanceUi_.setWidgetVisible("appearance_choice", hasToken && descriptor->type == UiTokenType::Choice);
	appearanceUi_.setWidgetVisible("appearance_font_family", hasToken && descriptor->type == UiTokenType::Font);
	appearanceUi_.setWidgetVisible("appearance_font_style", hasToken && descriptor->type == UiTokenType::Font);
	if (hasToken)
	{
		std::string description = appearanceTokenLabel(*descriptor);
		if (descriptor->type == UiTokenType::Color)
			description += " | Chart bars and marks use the same color tokens as the interface.";
		appearanceTokenDescription_->setContent(description);
	}
	else
		appearanceTokenDescription_->setContent("No appearance tokens match this search.");

	if (value.type == UiTokenType::Number)
	{
		appearanceUi_.find("appearance_value")->setLabel(tokenDisplayValue(value));
		const float minimum = static_cast<float>(descriptor->minimum);
		const float maximum = static_cast<float>(descriptor->maximum);
		const float step = maximum - minimum <= 1.0f ? 0.01f : 1.0f;
		appearanceUi_.find("appearance_value_slider")->setSliderRange(minimum, maximum, step, static_cast<float>(value.number));
	}
	else if (value.type == UiTokenType::Color)
	{
		appearanceUi_.find("appearance_color")->setLabel("Pick RGB | " + tokenDisplayValue(value));
		appearanceUi_.find("appearance_color_alpha")->setLabel("Alpha " + tokenDisplayValue(UiTokenValue::fromNumber(value.color.a)));
		appearanceUi_.find("appearance_color_r")->setSliderValue(value.color.r);
		appearanceUi_.find("appearance_color_g")->setSliderValue(value.color.g);
		appearanceUi_.find("appearance_color_b")->setSliderValue(value.color.b);
		appearanceUi_.find("appearance_color_a")->setSliderValue(value.color.a);
	}
	else if (value.type == UiTokenType::Boolean) appearanceUi_.find("appearance_boolean")->setLabel(value.boolean ? "Checked" : "Unchecked");
	else if (value.type == UiTokenType::Choice)
	{
		UiWidget* choice = appearanceUi_.find("appearance_choice");
		choice->setOptions(descriptor->choices);
		const std::vector<std::string>::const_iterator at = std::find(descriptor->choices.begin(), descriptor->choices.end(), value.text);
		choice->setSelectedIndex(at == descriptor->choices.end() ? 0 : static_cast<int>(at - descriptor->choices.begin()));
	}
	else if (value.type == UiTokenType::Font)
	{
		std::vector<std::string> families = fontFamilyNames();
		if (std::find(families.begin(), families.end(), value.font.family) == families.end()) families.push_back(value.font.family);
		UiWidget* family = appearanceUi_.find("appearance_font_family");
		family->setOptions(families);
		family->setSelectedIndex(static_cast<int>(std::find(families.begin(), families.end(), value.font.family) - families.begin()));
		std::vector<std::string> styles = fontStyleNames(value.font.family);
		if (styles.empty()) styles.push_back(value.font.style);
		else if (std::find(styles.begin(), styles.end(), value.font.style) == styles.end()) styles.push_back(value.font.style);
		UiWidget* style = appearanceUi_.find("appearance_font_style");
		style->setOptions(styles);
		style->setSelectedIndex(static_cast<int>(std::find(styles.begin(), styles.end(), value.font.style) - styles.begin()));
	}
	for (std::size_t i = 0; i < appearanceUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *appearanceUi_.widgets()[i];
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
		if (label != labels_.end())
			label->second->setContent(widget.label());
	}
	layoutAppearanceEditor();
}

void ControlPanel::layoutAppearanceEditor()
{
	if (!appearanceEditorOpen_ || width_ <= 0 || height_ <= 0) return;
	appearanceLayout_ = calculateAppearanceEditorLayout(static_cast<float>(width_), static_cast<float>(height_));
	if (!appearanceLayout_.valid) return;
	for (std::map<std::string, UiRect>::const_iterator control = appearanceLayout_.controls.begin();
		control != appearanceLayout_.controls.end(); ++control)
	{
		UiWidget* widget = appearanceUi_.find(control->first);
		if (widget) widget->setBounds(control->second);
	}
	const char* popupIds[] = { "appearance_category", "appearance_token", "appearance_choice", "appearance_font_family", "appearance_font_style" };
	for (std::size_t i = 0; i < sizeof(popupIds) / sizeof(popupIds[0]); ++i)
	{
		UiWidget* widget = appearanceUi_.find(popupIds[i]);
		if (widget)
			widget->setPopupAbove(widget->bounds().y + widget->bounds().height *
				(static_cast<float>(widget->visibleOptionCount()) + 1.0f) >
				appearanceLayout_.panelBounds.y + appearanceLayout_.panelBounds.height - 8.0f);
	}
	appearanceUi_.setModalBounds(appearanceLayout_.panelBounds, UiAction::ViewCommand::CloseAppearanceEditor);
}

void ControlPanel::drawAppearanceEditor()
{
	if (!appearanceEditorOpen_) return;
	const UiPresetId preset = appearanceEditorModel_.preset();
	const ThemePalette readable = paletteForAppearance(preset,
		defaultUiThemeCustomization(preset, PaletteId::Monochrome));
	drawingAppearanceEditor_ = true;
	const UiRect& panel = appearanceLayout_.panelBounds;
	submitBox(UiRect(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)),
		readable.overlayDim, MaterialId::ModalDim, DimLayer);
	submitBox(panel, readable.panel, PanelMaterial, SettingsLayer);
	submitBox(UiRect(panel.x, panel.y, panel.width, 2.0f),
		readable.accent, AccentMaterial, RenderLayer::SettingsAccent);
	drawText(*appearanceEditorTitle_, appearanceLayout_.titleBounds, 17.0f, readable.text, SettingsTextLayer);
	const float scale = appearanceLayout_.scale;
	const UiRect& controlClip = appearanceLayout_.contentClip;
	const ClipBounds clip(controlClip.x, controlClip.y, controlClip.width, controlClip.height);
	const UiTokenValue selectedValue = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], appearanceEditorModel_.selectedToken());
	if (selectedValue.type == UiTokenType::Color)
	{
		const UiColor& c = selectedValue.color;
		const glm::vec4 fill(c.r, c.g, c.b, c.a);
		submitBox(appearanceLayout_.colorSwatchBounds, fill, ButtonMaterial, RenderLayer::SettingsControls, clip);
		const float edge = std::max(1.0f, 1.0f * scale);
		submitBox(UiRect(appearanceLayout_.colorSwatchBounds.x, appearanceLayout_.colorSwatchBounds.y,
			appearanceLayout_.colorSwatchBounds.width, edge), readable.border, ButtonMaterial, RenderLayer::SettingsControls, clip);
		submitBox(UiRect(appearanceLayout_.colorSwatchBounds.x, appearanceLayout_.colorSwatchBounds.y + appearanceLayout_.colorSwatchBounds.height - edge,
			appearanceLayout_.colorSwatchBounds.width, edge), readable.border, ButtonMaterial, RenderLayer::SettingsControls, clip);
		submitBox(UiRect(appearanceLayout_.colorSwatchBounds.x, appearanceLayout_.colorSwatchBounds.y, edge,
			appearanceLayout_.colorSwatchBounds.height), readable.border, ButtonMaterial, RenderLayer::SettingsControls, clip);
		submitBox(UiRect(appearanceLayout_.colorSwatchBounds.x + appearanceLayout_.colorSwatchBounds.width - edge,
			appearanceLayout_.colorSwatchBounds.y, edge, appearanceLayout_.colorSwatchBounds.height), readable.border, ButtonMaterial, RenderLayer::SettingsControls, clip);
	}
	drawText(*appearanceTokenDescription_, appearanceLayout_.descriptionBounds, 12.0f, readable.mutedText, SettingsTextLayer, clip);
	const char* rowIds[] = { "appearance_preset", "appearance_palette", "appearance_category", "appearance_search", "appearance_token",
		"appearance_value_down", "appearance_value", "appearance_value_up", "appearance_value_slider",
		"appearance_color", "appearance_color_alpha", "appearance_color_r", "appearance_color_g", "appearance_color_b", "appearance_color_a", "appearance_boolean",
		"appearance_choice", "appearance_font_family", "appearance_font_style" };
	for (std::size_t i = 0; i < sizeof(rowIds) / sizeof(rowIds[0]); ++i)
	{
		const UiWidget* widget = appearanceUi_.find(rowIds[i]);
		if (!widget || !widget->visible()) continue;
		if (widget->kind() == UiWidgetKind::Slider)
		{
			drawSliderControl(*widget, readable, clip, RenderLayer::SettingsControls);
			continue;
		}
		submitBox(widget->bounds(), widget->enabled() ? readable.button : readable.disabledButton,
			widget->kind() == UiWidgetKind::ComboBox ? ValueMaterial : ButtonMaterial, RenderLayer::SettingsControls, clip);
		drawWidget(*widget, readable, RenderLayer::SettingsControls);
	}
	appearanceStatus_->setContent(appearanceError_.empty() ? "Changes preview behind this editor. Apply saves; Cancel discards." : appearanceError_);
	drawText(*appearanceStatus_, appearanceLayout_.statusBounds, 11.0f, appearanceError_.empty() ? readable.mutedText : readable.accent,
		SettingsTextLayer, ClipBounds(panel.x, panel.y, panel.width, panel.height));
	const char* actionIds[] = { "appearance_reset_token", "appearance_reset_theme", "appearance_import",
		"appearance_export", "appearance_apply", "appearance_cancel" };
	for (std::size_t i = 0; i < sizeof(actionIds) / sizeof(actionIds[0]); ++i)
	{
		const UiWidget* widget = appearanceUi_.find(actionIds[i]);
		if (!widget) continue;
		submitBox(widget->bounds(), widget->id() == "appearance_apply" ? readable.accent : readable.button,
			ButtonMaterial, RenderLayer::SettingsControls);
		drawWidget(*widget, readable, RenderLayer::SettingsControls);
	}
	if (appearanceUi_.hasOpenCombo())
		for (std::size_t i = 0; i < appearanceUi_.widgets().size(); ++i)
			if (appearanceUi_.widgets()[i]->visible() && appearanceUi_.widgets()[i]->expanded())
				drawWidget(*appearanceUi_.widgets()[i], readable, RenderLayer::SettingsDropdown, true);
	drawingAppearanceEditor_ = false;
}

bool ControlPanel::handleAppearanceEditorEvent(const SDL_Event& event, AppAction& action)
{
	if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
	{
		cancelInlineEdit();
		appearanceSearchFocused_ = false;
		SDL_StopTextInput();
		UiAction lost;
		appearanceUi_.handleEvent(UiInputEvent(UiInputType::FocusLost), lost);
		return true;
	}
	if (!inlineEditWidgetId_.empty() && inlineEditAppearanceValue_)
	{
		if (event.type == SDL_TEXTINPUT) return appendInlineEditText(event.text.text);
		if (event.type == SDL_KEYDOWN)
		{
			const SDL_Keycode key = event.key.keysym.sym;
			if (key == SDLK_TAB)
			{
				if (!commitInlineEdit(action)) return true;
				UiAction navigation;
				appearanceUi_.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab,
					(event.key.keysym.mod & KMOD_SHIFT) != 0), navigation);
				return true;
			}
			if (key == SDLK_ESCAPE) { cancelInlineEdit(); return true; }
			if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { commitInlineEdit(action); return true; }
			if (key == SDLK_BACKSPACE)
			{
				inlineEditReplaceOnNextDigit_ = false;
				if (!inlineEditBuffer_.empty()) inlineEditBuffer_.erase(inlineEditBuffer_.size() - 1);
				updateInlineEditLabel();
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEditBuffer_.clear(); inlineEditReplaceOnNextDigit_ = false; updateInlineEditLabel(); return true;
			}
			return true;
		}
	}
	if (appearanceSearchFocused_)
	{
		if (event.type == SDL_TEXTINPUT)
		{
			appearanceEditorModel_.appendSearch(event.text.text);
			refreshAppearanceTokens();
			return true;
		}
		if (event.type == SDL_KEYDOWN)
		{
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				if (appearanceUi_.hasOpenCombo())
				{
					UiAction closed; appearanceUi_.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Escape), closed);
					return true;
				}
				closeAppearanceEditor(true); return true;
			}
			if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
			{ appearanceSearchFocused_ = false; SDL_StopTextInput(); return true; }
			if (event.key.keysym.sym == SDLK_TAB)
			{
				appearanceSearchFocused_ = false; SDL_StopTextInput();
				UiAction navigation;
				appearanceUi_.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab,
					(event.key.keysym.mod & KMOD_SHIFT) != 0), navigation);
				return true;
			}
			if (event.key.keysym.sym == SDLK_BACKSPACE)
			{
				appearanceEditorModel_.eraseSearchCharacter();
				refreshAppearanceTokens(); return true;
			}
			if (event.key.keysym.sym == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{ appearanceEditorModel_.clearSearch(); refreshAppearanceTokens(); return true; }
			return true;
		}
	}
	if (appearanceSearchFocused_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		!inRect(appearanceUi_.find("appearance_search")->bounds(), event.button.x, event.button.y))
	{
		appearanceSearchFocused_ = false;
		SDL_StopTextInput();
	}
	if (!inlineEditWidgetId_.empty() && inlineEditAppearanceValue_ && event.type == SDL_MOUSEBUTTONDOWN &&
		!commitInlineEdit(action)) return true;
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !appearanceUi_.hasOpenCombo())
	{
		closeAppearanceEditor(true);
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
	else if (event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0; SDL_GetMouseState(&mouseX, &mouseY);
		input = UiInputEvent(UiInputType::Wheel, static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y);
	}
	else if (event.type == SDL_TEXTINPUT)
		input = UiInputEvent(UiInputType::TextInput, std::string(event.text.text));
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
		input = UiInputEvent(UiInputType::KeyDown, 0, 0, key, (event.key.keysym.mod & KMOD_SHIFT) != 0);
		input.control = (event.key.keysym.mod & KMOD_CTRL) != 0;
		input.alt = (event.key.keysym.mod & KMOD_ALT) != 0;
	}
	else translated = false;
	if (!translated) return true;
	if (event.type == SDL_MOUSEBUTTONDOWN)
	{
		const SDL_Keymod modifiers = SDL_GetModState();
		input.shift = (modifiers & KMOD_SHIFT) != 0;
		input.control = (modifiers & KMOD_CTRL) != 0;
		input.alt = (modifiers & KMOD_ALT) != 0;
	}
	UiAction uiAction;
	const bool consumed = appearanceUi_.handleEvent(input, uiAction);
	if (uiAction.viewCommand == UiAction::ViewCommand::CloseAppearanceEditor)
	{
		closeAppearanceEditor(true);
		return true;
	}
	if (consumed)
	{
		if (uiAction.event.controlId == "appearance_search" && uiAction.event.kind == UiEventKind::Activated)
		{
			appearanceSearchFocused_ = true;
			SDL_StartTextInput();
		}
		else if (uiAction.event.kind != UiEventKind::None)
			processAppearanceEditorAction(uiAction, action);
	}
	return consumed || appearanceEditorOpen_;
}

void ControlPanel::processAppearanceEditorAction(const UiAction& action, AppAction& output)
{
	const std::string& id = action.event.controlId;
	const UiPresetId preset = appearanceEditorModel_.preset();
	const std::string& selectedToken = appearanceEditorModel_.selectedToken();
	if (id == "appearance_preset")
	{
		const int lastPreset = static_cast<int>(uiPresetCatalog().size()) - 1;
		appearanceEditorModel_.setPreset(static_cast<UiPresetId>(std::max(0, std::min(action.event.selection, lastPreset))));
		appearanceDraft_.activePreset = appearanceEditorModel_.preset();
		appearanceUi_.closeCombo();
		refreshAppearanceTokens();
		appearanceError_.clear();
		return;
	}
	if (id == "appearance_palette")
	{
		UiThemeCustomization& theme = appearanceDraft_.themes[preset];
		const int lastPalette = static_cast<int>(paletteCatalog().size()) - 1;
		selectUiPalette(theme, static_cast<PaletteId>(std::max(0, std::min(action.event.selection, lastPalette))));
		appearanceError_.clear(); updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_category")
	{
		appearanceEditorModel_.setCategoryFilter(action.event.selection);
		appearanceUi_.closeCombo();
		refreshAppearanceTokens();
		appearanceError_.clear();
		return;
	}
	if (id == "appearance_token")
	{
		appearanceEditorModel_.selectOption(action.event.selection);
		appearanceError_.clear(); updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_search") return;
	if (action.event.kind == UiEventKind::ValueChanged &&
		(id == "appearance_value_slider" || id == "appearance_color_r" || id == "appearance_color_g" ||
		 id == "appearance_color_b" || id == "appearance_color_a"))
	{
		UiTokenValue value = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken);
		if (id == "appearance_value_slider" && value.type == UiTokenType::Number)
			value.number = action.event.value;
		else if (value.type == UiTokenType::Color)
		{
			if (id == "appearance_color_r") value.color.r = action.event.value;
			else if (id == "appearance_color_g") value.color.g = action.event.value;
			else if (id == "appearance_color_b") value.color.b = action.event.value;
			else if (id == "appearance_color_a") value.color.a = action.event.value;
		}
		else return;
		std::string error;
		if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken, value, &error))
			appearanceError_ = error;
		else appearanceError_.clear();
		updateAppearanceEditorControls();
		return;
	}
	if (id == "appearance_value") { beginInlineEdit(id); return; }
	if (id == "appearance_color_alpha") { beginInlineEdit(id); return; }
	if (id == "appearance_value_down") { adjustAppearanceNumber(-1.0f); return; }
	if (id == "appearance_value_up") { adjustAppearanceNumber(1.0f); return; }
	if (id == "appearance_color") { chooseAppearanceColor(); return; }
	if (id == "appearance_boolean")
	{
		const UiTokenValue before = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken);
		std::string error;
		if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken,
			UiTokenValue::fromBoolean(!before.boolean), &error)) appearanceError_ = error;
		else appearanceError_.clear();
		updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_choice")
	{
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
		if (descriptor && action.event.selection >= 0 && static_cast<std::size_t>(action.event.selection) < descriptor->choices.size())
		{
			std::string error;
			if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken,
				UiTokenValue::fromChoice(descriptor->choices[static_cast<std::size_t>(action.event.selection)]), &error)) appearanceError_ = error;
			else appearanceError_.clear();
		}
		updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_font_family" || id == "appearance_font_style")
	{
		const UiTokenValue current = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken);
		if (current.type == UiTokenType::Font)
		{
			UiFontReference font = current.font;
			const UiWidget* widget = appearanceUi_.find(id);
			if (widget && action.event.selection >= 0 && static_cast<std::size_t>(action.event.selection) < widget->options().size())
			{
				if (id == "appearance_font_family")
				{
					font.family = widget->options()[static_cast<std::size_t>(action.event.selection)];
					const std::vector<std::string> styles = fontStyleNames(font.family);
					if (!styles.empty()) font.style = styles[0];
				}
				else font.style = widget->options()[static_cast<std::size_t>(action.event.selection)];
				std::string error;
				if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken,
					UiTokenValue::fromFont(font), &error)) appearanceError_ = error;
				else appearanceError_.clear();
			}
		}
		updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_reset_token") { resetAppearanceToken(); return; }
	if (id == "appearance_reset_theme")
	{
		appearanceDraft_.themes[preset] = defaultUiThemeCustomization(preset);
		appearanceError_.clear(); updateAppearanceEditorControls(); return;
	}
	if (id == "appearance_import") { importAppearanceProfile(); return; }
	if (id == "appearance_export") { exportAppearanceProfile(); return; }
	if (id == "appearance_cancel") { closeAppearanceEditor(true); return; }
	if (id == "appearance_apply")
	{
		appearanceDraft_.activePreset = preset;
		output = AppAction(AppActionKind::ApplyAppearance);
		output.appearance.reset(new UiAppearancePreferences(appearanceDraft_));
	}
}

void ControlPanel::resetAppearanceToken()
{
	if (appearanceEditorModel_.selectedToken().empty()) return;
	appearanceDraft_.themes[appearanceEditorModel_.preset()].overrides.erase(appearanceEditorModel_.selectedToken());
	appearanceError_.clear(); updateAppearanceEditorControls();
}

void ControlPanel::adjustAppearanceNumber(float direction)
{
	const UiPresetId preset = appearanceEditorModel_.preset();
	const std::string& selectedToken = appearanceEditorModel_.selectedToken();
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	if (!descriptor || descriptor->type != UiTokenType::Number) return;
	const UiTokenValue current = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken);
	const double increment = descriptor->maximum - descriptor->minimum > 100.0 ? 1.0 : 0.5;
	std::string error;
	if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken,
		UiTokenValue::fromNumber(std::max(descriptor->minimum, std::min(descriptor->maximum, current.number + direction * increment))), &error))
		appearanceError_ = error;
	else appearanceError_.clear();
	updateAppearanceEditorControls();
}

void ControlPanel::chooseAppearanceColor()
{
	const UiPresetId preset = appearanceEditorModel_.preset();
	const std::string& selectedToken = appearanceEditorModel_.selectedToken();
	const UiTokenValue value = effectiveUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken);
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	if (!descriptor || descriptor->type != UiTokenType::Color || value.type != UiTokenType::Color) return;
	static COLORREF customColors[16] = {};
	CHOOSECOLORW dialog = {};
	dialog.lStructSize = sizeof(dialog);
	dialog.hwndOwner = window_.getWindow() ? GetActiveWindow() : nullptr;
	dialog.rgbResult = RGB(static_cast<BYTE>(value.color.r * 255.0f), static_cast<BYTE>(value.color.g * 255.0f), static_cast<BYTE>(value.color.b * 255.0f));
	dialog.lpCustColors = customColors;
	dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
	if (!ChooseColorW(&dialog)) return;
	UiColor color(static_cast<float>(GetRValue(dialog.rgbResult)) / 255.0f,
		static_cast<float>(GetGValue(dialog.rgbResult)) / 255.0f,
		static_cast<float>(GetBValue(dialog.rgbResult)) / 255.0f, value.color.a);
	std::string error;
	if (!setUiTokenValue(preset, appearanceDraft_.themes[preset], selectedToken,
		UiTokenValue::fromColor(color), &error)) appearanceError_ = error;
	else appearanceError_.clear();
	updateAppearanceEditorControls();
}

void ControlPanel::importAppearanceProfile()
{
	wchar_t path[MAX_PATH] = {};
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_.getWindow() ? GetActiveWindow() : nullptr;
	dialog.lpstrFilter = L"AGSS UI profiles (*.agss-ui.ini)\0*.agss-ui.ini\0INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0\0";
	dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH; dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetOpenFileNameW(&dialog)) return;
	std::string contents, error;
	if (!readUtf8File(path, contents, &error)) { appearanceError_ = error; return; }
	UiAppearanceProfile profile;
	if (!parseUiAppearanceProfile(contents, profile, &error)) { appearanceError_ = error; return; }
	appearanceDraft_.themes[profile.preset] = profile.theme;
	appearanceDraft_.activePreset = profile.preset;
	appearanceEditorModel_.setPreset(profile.preset);
	appearanceError_.clear();
	if (!profile.ignoredTokens.empty())
	{
		appearanceError_ = "Imported profile; ignored unknown tokens: ";
		for (std::size_t i = 0; i < profile.ignoredTokens.size(); ++i)
		{
			if (i) appearanceError_ += ", ";
			appearanceError_ += profile.ignoredTokens[i];
		}
	}
	refreshAppearanceTokens();
}

void ControlPanel::exportAppearanceProfile()
{
	wchar_t path[MAX_PATH] = L"appearance.agss-ui.ini";
	OPENFILENAMEW dialog = {};
	dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_.getWindow() ? GetActiveWindow() : nullptr;
	dialog.lpstrFilter = L"AGSS UI profiles (*.agss-ui.ini)\0*.agss-ui.ini\0\0";
	dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH; dialog.lpstrDefExt = L"agss-ui.ini";
	dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if (!GetSaveFileNameW(&dialog)) return;
	std::string error;
	const UiPresetId preset = appearanceEditorModel_.preset();
	const std::string contents = serializeUiAppearanceProfile(preset, appearanceDraft_.themes[preset]);
	if (!writeUtf8File(path, contents, &error)) appearanceError_ = error;
	else appearanceError_ = "Appearance profile exported.";
}

void ControlPanel::completeActionResult(const AppAction& action, const sortinglogic::CommandResult& result)
{
	if (action.kind == AppActionKind::SelectAlgorithm && algorithmBrowser_.open && algorithmBrowser_.pendingSelection)
	{
		algorithmBrowser_.pendingSelection = false;
		if (result.accepted) closeAlgorithmMenu();
		else algorithmBrowser_.status = result.message.empty() ? "Pause playback before changing algorithms." : result.message;
		return;
	}
	if (action.kind != AppActionKind::ApplyAppearance || !appearanceEditorOpen_) return;
	if (!result.accepted)
	{
		appearanceError_ = result.message.empty() ? "Could not save appearance. Check the settings file and try again." : result.message;
		return;
	}
	appearanceDraft_ = settings_.appearance;
	closeAppearanceEditor(false);
}

void ControlPanel::layoutAlgorithmMenu()
{
	if (!algorithmBrowser_.open || width_ <= 0 || height_ <= 0) return;
	algorithmBrowser_.layout(mainUi_.find("algorithm")->bounds(), width_, height_);
}

bool ControlPanel::handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action)
{
	UiInputEvent input(UiInputType::FocusLost);
	if (!translateAlgorithmBrowserEvent(event, input)) return true;
	const AlgorithmBrowserInputResult result = algorithmBrowser_.handleInput(
		input, screenModel_.setupEnabled, settings_.simulation.algorithm);
	if (result.closeRequested)
		closeAlgorithmMenu();
	if (result.selectionRequested)
		action = AppAction(AppActionKind::SelectAlgorithm, result.familyId, result.variantId);
	return true;
}

void ControlPanel::drawUiPaintCommand(const UiPaintCommand& command)
{
	const glm::vec4 color = toGlmColor(command.color);
	const ClipBounds clip = renderClip(command);
	const RenderLayer layer = renderLayerForUiPaint(command.layer);
	if (command.primitive == UiPaintPrimitive::Rectangle)
	{
		submitBox(command.bounds, color, materialForUiPaint(command.material), layer, clip);
		return;
	}
	if (command.primitive != UiPaintPrimitive::Text || command.elementId.empty()) return;
	TextLabel* label = nullptr;
	if (command.elementId == "#settings_title") label = settingsTitle_.get();
	else if (command.elementId == "#settings_footer")
		label = settingsPanelModel_.isPage(SettingsPanelPage::GeneralAudio) ? audioDeviceStatus_.get() : settingsSaveStatus_.get();
	else
	{
		if (!labels_[command.elementId])
			labels_[command.elementId].reset(new TextLabel(command.text, "Courier New Bold",
				std::max(8, static_cast<int>(std::lround(command.textHeight))), SDL_Color{ 255, 255, 255, 255 }));
		label = labels_[command.elementId].get();
	}
	if (!label) return;
	label->setContent(command.text);
	drawText(*label, command.bounds, command.textHeight, color, layer, clip);
}

void ControlPanel::drawAlgorithmMenu(const ThemePalette& palette)
{
	algorithmBrowser_.configureViewports();
	AlgorithmBrowserPalette viewPalette;
	viewPalette.background = toUiColor(palette.background);
	viewPalette.panel = toUiColor(palette.panel);
	viewPalette.button = toUiColor(palette.button);
	viewPalette.disabledButton = toUiColor(palette.disabledButton);
	viewPalette.valueButton = toUiColor(palette.valueButton);
	viewPalette.accent = toUiColor(palette.accent);
	viewPalette.text = toUiColor(palette.text);
	viewPalette.mutedText = toUiColor(palette.mutedText);
	viewPalette.overlayDim = toUiColor(palette.overlayDim);

	auto renderCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
	DispatchingUiPaintContext<decltype(renderCommand)> paintContext(renderCommand);
	algorithmBrowserView_.compose(algorithmBrowser_, viewPalette, screenModel_.setupEnabled,
		settings_.simulation.algorithm, width_, height_, paintContext);
}
