#include "ControlPanel.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <set>
#include <sstream>

#include <SDL.h>

#include "AppearanceDialogAdapter.h"
#include "AlgorithmBrowserCatalog.h"
#include "LayoutMetrics.h"
#include "Renderer2D.h"
#include "Window.h"
#include "../SortingLogic/SortingLogic.h"
#include "UiActionRouting.h"
#include "SdlUiEventAdapter.h"
#include "SettingsScroll.h"
#include "SettingsPanelCatalog.h"
#include "WindowsFontCatalog.h"
#include "PlaybackTimebase.h"
#include "UiScreenModelAdapter.h"

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
		case UiPaintLayer::SettingsControls: return RenderLayer::SettingsControls;
		case UiPaintLayer::Text: return RenderLayer::SettingsDropdownText;
		case UiPaintLayer::SettingsText: return RenderLayer::SettingsText;
		case UiPaintLayer::MainControls: return RenderLayer::MainControls;
		case UiPaintLayer::MainControlsOverlay: return layerOffset(RenderLayer::MainControls, 1);
		case UiPaintLayer::MainText: return RenderLayer::MainText;
		}
		return RenderLayer::SettingsDropdown;
	}

	MaterialId materialForUiPaint(UiPaintMaterial material)
	{
		switch (material)
		{
		case UiPaintMaterial::Panel: return PanelMaterial;
		case UiPaintMaterial::Button: return ButtonMaterial;
		case UiPaintMaterial::Timeline: return TimelineMaterial;
		case UiPaintMaterial::TimelineThumb: return MaterialId::TimelineThumb;
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

	SdlUiEventContext sdlUiEventContext(const SDL_Event& event)
	{
		SdlUiEventContext context;
		if (event.type == SDL_MOUSEWHEEL)
		{
			int mouseX = 0;
			int mouseY = 0;
			SDL_GetMouseState(&mouseX, &mouseY);
			context.pointerX = static_cast<float>(mouseX);
			context.pointerY = static_cast<float>(mouseY);
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN)
		{
			const SDL_Keymod modifiers = SDL_GetModState();
			context.shift = (modifiers & KMOD_SHIFT) != 0;
			context.control = (modifiers & KMOD_CTRL) != 0;
			context.alt = (modifiers & KMOD_ALT) != 0;
		}
		return context;
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

	bool inRect(const UiRect& rect, int x, int y)
	{
		return rect.contains(static_cast<float>(x), static_cast<float>(y));
	}

	const char* inlineEditWidgetId(InlineEditTarget target)
	{
		switch (target)
		{
		case InlineEditTarget::ItemCount: return "bars_value";
		case InlineEditTarget::Duration: return "time_value";
		case InlineEditTarget::AppearanceNumber: return "appearance_value";
		case InlineEditTarget::AppearanceAlpha: return "appearance_color_alpha";
		case InlineEditTarget::None: return "";
		}
		return "";
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
	mainUi_(), appearanceEditorView_(settingsUiPresetOptions(), settingsPaletteOptions(), settings.appearance,
		paletteIndex(activeTheme(settings).palette)), overlayState_(), algorithmBrowser_(algorithmBrowserCatalog()), algorithmBrowserView_(), settingsPanelView_(), algorithmProfileView_(measureLogicalTextWidth), screenModel_(), layoutResult_(), renderPreferences_(settings), labels_(), paletteCaption_(new TextLabel("Palette", "Courier New Bold", 12, { 238, 246, 249, 255 })),
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
	headerBounds_(), gearBounds_(), draggingTimeline_(false),
	appearanceSearchFocused_(false), drawingAppearanceEditor_(false),
	chartViewport_(), width_(0), height_(0), layoutKey_(), layoutCacheValid_(false), laidOutPreset_(settings.appearance.activePreset), fontFallbackActive_(false),
	inlineEdit_()
{
	createWidgets();
	updateLayout();
}

ControlPanel::~ControlPanel()
{
	if (inlineEdit_.active() || algorithmBrowserOpen() || appearanceSearchFocused_) SDL_StopTextInput();
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
	mainUi_.addComboBox("domain", settingsInputDomainOptions(), static_cast<int>(settings_.simulation.domain), AppAction(AppActionKind::SetInputDomain));
	mainUi_.addComboBox("pattern", settingsInputPatternOptions(), static_cast<int>(settings_.simulation.pattern), AppAction(AppActionKind::SetInputPattern));
	mainUi_.addButton("seed_readout", "Seed");
	mainUi_.addButton("start_overlay", "Generate & Play", AppAction(AppActionKind::GenerateAndPlay));
	settingsPanelView_.initializeControls(settings_);
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
	for (std::size_t i = 0; i < mainUi_.widgets().size(); ++i)
	{
		const UiWidget& widget = *mainUi_.widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 16, { 238, 246, 249, 255 }));
	}
	for (std::size_t i = 0; i < appearanceEditorView_.controls().widgets().size(); ++i)
	{
		const UiWidget& widget = *appearanceEditorView_.controls().widgets()[i];
		labels_[widget.id()].reset(new TextLabel(widget.label(), "Courier New Bold", 15, { 255, 255, 255, 255 }));
	}
	for (std::size_t i = 0; i < settingsPanelView_.controls().widgets().size(); ++i)
	{
		const UiWidget& widget = *settingsPanelView_.controls().widgets()[i];
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
	for (std::map<std::string, std::string>::const_iterator i = settingsPanelView_.captions().begin(); i != settingsPanelView_.captions().end(); ++i)
		labels_[i->first + "#caption"].reset(new TextLabel(i->second, "Courier New Bold", 12, { 238, 246, 249, 255 }));
	synchronizeAppearanceEditorControls();
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
	for (std::size_t i = 0; i < settingsPanelView_.controls().widgets().size(); ++i)
	{
		UiWidget* widget = settingsPanelView_.controls().widgets()[i].get();
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
	const UiRect settingsPanelBounds(std::max(8.0f, static_cast<float>(width_) - panelWidth - 8.0f), 42.0f,
		panelWidth, std::min(panelHeight, std::max(200.0f, static_cast<float>(height_) - 50.0f)));
	settingsPanelView_.setPanelBounds(settingsPanelBounds, overlayState_.is(UiOverlayId::Settings));
	layoutAlgorithmMenu();
	layoutSettingsControls();
	layoutMainComboPopups();
	if (appearanceEditorOpen()) layoutAppearanceEditor();
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
	if (settingsPanelView_.panelBounds().width <= 0.0f) return;
	const sortinglogic::AlgorithmDescriptor* selected = sortinglogic::algorithmDescriptor(
		static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
	const SettingsPanelAlgorithmPresentation selectedPresentation = settingsPanelAlgorithmPresentation(selected);
	const std::vector<std::string> active = settingsPanelControlIds(
		settingsPanelView_.page(), settingsPanelView_.audioGroup(), &selectedPresentation);
	const float scrollbarThickness = themeNumber(renderPreferences_, "scrollbar.thickness", 8.0f);
	settingsPanelView_.layoutControls(active, scrollbarThickness);
}

void ControlPanel::updateAudioControls()
{
	const SoundEngine::ReconstructedParameters& a = settings_.reconstructedAudio;
	auto slider = [this](const char* id, float value) { settingsPanelView_.controls().find(id)->setSliderValue(value); };
	auto selection = [this](const char* id, int value) { settingsPanelView_.controls().find(id)->setSelectedIndex(value); };
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
	settingsPanelView_.controls().setWidgetEnabled("recon_release", a.idlePolicy == SoundEngine::ReconstructedIdlePolicy::ReleaseSelection);
	settingsPanelView_.controls().setWidgetEnabled("recon_cutoff", a.lowPassEnabled);
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
		UiWidget* widget = settingsPanelView_.controls().find(id);
		if (!widget) continue;
		widget->setSliderValue(offsets.decibels[index]);
		widget->setAction(AppAction(AppActionKind::SetVariantAudioLevel, algorithm.familyId, algorithm.variantId,
			offsets.decibels[index], channel));
		std::ostringstream caption;
		caption << algorithmAudioChannelName(level) << " (family " << std::fixed << std::setprecision(1)
			<< std::showpos << inherited.decibels[index] << " dB)";
		settingsPanelView_.captions()[id] = caption.str();
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(id);
		if (label != labels_.end()) label->second->setContent(caption.str());
	}
	settingsPanelView_.controls().find("algorithm_audio_reset")->setAction(AppAction(AppActionKind::ResetVariantAudioLevels,
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
	settingsPanelView_.controls().find("preset")->setSelectedIndex(static_cast<int>(renderPreferences_.appearance.activePreset));
	settingsPanelView_.controls().find("palette")->setSelectedIndex(paletteIndex(activeTheme(renderPreferences_).palette));
	settingsPanelView_.controls().find("spacing")->setSelectedIndex(spacingIndex(settings_.visual.spacing));
	settingsPanelView_.controls().find("domain")->setSelectedIndex(static_cast<int>(settings_.simulation.domain));
	settingsPanelView_.controls().find("pattern")->setSelectedIndex(static_cast<int>(settings_.simulation.pattern));
	settingsPanelView_.controls().find("playback_timing")->setSelectedIndex(static_cast<int>(settings_.simulation.playbackTimingMode));
	settingsPanelView_.controls().find("merge_buffer_visualization")->setSelectedIndex(settings_.simulation.mergeBufferVisualizationEnabled ? 1 : 0);
	settingsPanelView_.controls().find("completion_scan")->setSelectedIndex(settings_.simulation.completionScanEnabled ? 1 : 0);
	settingsPanelView_.controls().setWidgetEnabled("domain", !busy);
	settingsPanelView_.controls().setWidgetEnabled("pattern", !busy);
	settingsPanelView_.controls().setWidgetEnabled("playback_timing", !busy);
	settingsPanelView_.controls().setWidgetEnabled("merge_buffer_visualization", !busy && snapshot.algorithm == sortinglogic::SortAlgorithm::Merge);
	settingsPanelView_.controls().setWidgetEnabled("completion_scan", !busy);
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
			const std::string id = settingsAlgorithmOptionControlId(algorithmDescriptor.familyId,
				algorithmDescriptor.variantId, groupId);
			UiWidget* widget = settingsPanelView_.controls().find(id);
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
			settingsPanelView_.controls().setWidgetEnabled(id, !busy && snapshot.algorithm == algorithmDescriptor.id);
		}
	}
	for (int pattern = static_cast<int>(sortinglogic::InputPattern::Random);
		pattern <= static_cast<int>(sortinglogic::InputPattern::AndrutReference); ++pattern)
		settingsPanelView_.controls().find("pattern")->setOptionEnabled(static_cast<std::size_t>(pattern),
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
		if (!widget) widget = settingsPanelView_.controls().find(i->first);
		if (widget)
		{
			std::string text = widget->label();
			if (widget->kind() == UiWidgetKind::Slider)
			{
				std::ostringstream value;
				value << settingsPanelView_.captions()[i->first] << "  " << std::fixed
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
			detailText << playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount);
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
			detailText << playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount);
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
	settingsTitle_->setContent(settingsTabTitle(settingsPanelView_.page(), settings_));
	const UiTokenValue requestedFont = effectiveUiTokenValue(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_), "typeface.primary");
	const std::string saveStatus = fontFallbackActive_ && requestedFont.type == UiTokenType::Font
		? "Font " + requestedFont.font.family + " " + requestedFont.font.style + " unavailable; using bundled Courier New."
		: displayedSaveStatus_.empty() ? "Preferences save automatically." : displayedSaveStatus_;
	settingsSaveStatus_->setContent(saveStatus);
}

bool ControlPanel::handleEvent(const SDL_Event& event, AppAction& action)
{
	if (!appearanceEditorOpen() && !algorithmBrowserOpen() && !appearanceSearchFocused_ && !inlineEdit_.active() &&
		event.type == SDL_KEYDOWN && !event.key.repeat &&
		event.key.keysym.sym == SDLK_F12 && (event.key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT)) == (KMOD_CTRL | KMOD_SHIFT))
	{
		openAppearanceEditor();
		return true;
	}
	renderPreferences_ = settings_;
	if (appearanceEditorOpen()) renderPreferences_.appearance = appearanceEditorView_.session().draft();
	updateLayout();
	if (appearanceEditorOpen())
		return handleAppearanceEditorEvent(event, action);
	if (algorithmBrowserOpen())
		return handleAlgorithmMenuEvent(event, action);
	if (!settingsOpen() && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
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
			if (inlineEdit_.active() && targetWidgetId == inlineEditWidgetId(inlineEdit_.target())) return true;
			if (inlineEdit_.active() && !commitInlineEdit(action)) return true;
			beginInlineEdit(targetWidgetId);
			return true;
		}
		if (inlineEdit_.active())
		{
			if (!commitInlineEdit(action)) return true;
			return true;
		}
	}
	if (inlineEdit_.active())
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
				return commitInlineEdit(action) || inlineEdit_.active();
			if (key == SDLK_BACKSPACE)
			{
				inlineEdit_.backspace();
				updateInlineEditLabel();
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEdit_.clear();
				updateInlineEditLabel();
				return true;
			}
			return true;
		}
	}
	if (overlayState_.is(UiOverlayId::Settings) && event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if (settingsPanelView_.handleScrollWheel(static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y))
			layoutSettingsControls();
		return true;
	}
	if (!settingsOpen() && event.type == SDL_MOUSEWHEEL)
	{
		int mouseX = 0, mouseY = 0;
		SDL_GetMouseState(&mouseX, &mouseY);
		if (algorithmProfileView_.handleWheel(static_cast<float>(mouseX), static_cast<float>(mouseY), event.wheel.y))
			return true;
	}
	if (!settingsOpen() && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		algorithmProfileView_.pointerDown(static_cast<float>(event.button.x), static_cast<float>(event.button.y)))
	{
		return true;
	}
	if (!settingsOpen() && event.type == SDL_MOUSEMOTION &&
		algorithmProfileView_.pointerMove(static_cast<float>(event.motion.y)))
	{
		return true;
	}
	if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT && algorithmProfileView_.pointerUp())
	{
		return true;
	}
	if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
		algorithmProfileView_.pointerUp();
	if (!settingsOpen() && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
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
		inRect(timelineBounds_, event.button.x, event.button.y) && settings_.visual.panelExpanded && !settingsOpen())
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
	UiInputEvent input(UiInputType::FocusLost);
	if (!translateSdlUiEvent(event, input, SdlUiKeyProfile::Controls, sdlUiEventContext(event)))
		return settingsOpen() || action.kind != AppActionKind::None;
	UiAction uiAction;
	const bool consumed = (settingsOpen() ? settingsPanelView_.controls() : mainUi_).handleEvent(input, uiAction);
	if (consumed && (uiAction.viewCommand != UiAction::ViewCommand::None || uiAction.appAction.kind != AppActionKind::None ||
		uiAction.event.kind != UiEventKind::None))
	{
		AppAction processedAction;
		processAction(uiAction, processedAction);
		if (processedAction.kind != AppActionKind::None || action.kind == AppActionKind::None)
			action = processedAction;
	}
	return consumed || settingsOpen() || action.kind != AppActionKind::None;
}

void ControlPanel::processAction(const UiAction& action, AppAction& output)
{
	if (action.event.kind == UiEventKind::SelectionChanged &&
		(action.event.controlId == "menu_file" || action.event.controlId == "menu_view" || action.event.controlId == "menu_tools"))
	{
		UiWidget* menu = mainUi_.find(action.event.controlId);
		if (menu) menu->setSelectedIndex(0);
		const UiAction command = ui::mainMenuSelectionAction(action.event.controlId, action.event.selection);
		if (command.viewCommand != UiAction::ViewCommand::None)
			processAction(command, output);
		else
			output = command.appAction;
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
		if (!overlayState_.openSettings()) return;
		updateOverlayInputCapture();
		mainUi_.closeCombo();
		return;
	}
	if (action.viewCommand == UiAction::ViewCommand::CloseSettings)
	{
		closeSettings();
		return;
	}
	if (settingsPanelView_.handleNavigation(action.viewCommand))
	{
		layoutSettingsControls();
		return;
	}
	if (action.appAction.kind == AppActionKind::SelectAlgorithm ||
		action.appAction.kind == AppActionKind::SetShellGapSequence ||
		action.appAction.kind == AppActionKind::SetAlgorithmOption)
	{
		output = ui::normalizeCatalogAction(action.appAction);
		return;
	}
	if (action.appAction.kind == AppActionKind::SelectAudioBand)
	{
		settingsPanelView_.selectAudioBand(action.appAction.selection, static_cast<int>(SoundEngine::BandCount));
		updateAudioControls();
		layoutSettingsControls();
	}
	const AppAction controlAction = ui::mainControlActivationAction(action.event);
	if (controlAction.kind != AppActionKind::None)
	{
		output = controlAction;
		return;
	}
	if (action.appAction.kind == AppActionKind::SetUiPreset)
	{
		mainUi_.closeCombo();
		settingsPanelView_.controls().closeCombo();
		draggingTimeline_ = false;
		cancelInlineEdit();
		updateOverlayInputCapture();
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
	if (overlayState_.is(UiOverlayId::Settings)) drawSettingsPanel(palette, audioStatus);
	if (algorithmBrowserOpen()) drawAlgorithmMenu(palette);
	if (appearanceEditorOpen()) drawAppearanceEditor();
}

void ControlPanel::prepareFrame(const sortinglogic::RenderSnapshot& snapshot,
	const std::string& persistenceStatus, bool playbackPending)
{
	renderPreferences_ = settings_;
	if (appearanceEditorOpen()) renderPreferences_.appearance = appearanceEditorView_.session().draft();
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
	algorithmProfileView_.beginFrame();
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
		AlgorithmProfilePalette profilePalette;
		profilePalette.background = toUiColor(palette.background);
		profilePalette.panel = toUiColor(palette.panel);
		profilePalette.border = toUiColor(palette.border);
		profilePalette.timelineRail = toUiColor(palette.timelineRail);
		profilePalette.accent = toUiColor(palette.accent);
		profilePalette.text = toUiColor(palette.text);
		profilePalette.mutedText = toUiColor(palette.mutedText);
		const UiRect seed = mainUi_.find("seed_readout")->bounds();
		auto renderProfileCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
		DispatchingUiPaintContext<decltype(renderProfileCommand)> paintContext(renderProfileCommand);
		algorithmProfileView_.compose(screenModel_.algorithm, screenModel_.algorithmCapabilities,
			true, width_, layoutResult_.setupRail, seed, resolveTextStyle(13.0f), resolveTextStyle(11.0f),
			profilePalette, paintContext);
	}
	if (snapshot.hasTrace || snapshot.buildingTrace || classicVga)
	{
		const glm::vec4 rail = palette.timelineRail;
		submitBox(timelineBounds_, rail, TimelineMaterial, UiLayer);
		const double timelineDuration = settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence
			? playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount) : snapshot.durationSeconds;
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
	chrome.panelBounds = settingsPanelView_.panelBounds();
	chrome.contentBounds = settingsPanelView_.contentBounds();
	chrome.title = settingsTitle_->text();
	SettingsPanelPalette chromePalette;
	chromePalette.panel = toUiColor(palette.panel);
	chromePalette.accent = toUiColor(palette.accent);
	chromePalette.text = toUiColor(palette.text);
	chromePalette.mutedText = toUiColor(palette.mutedText);
	chromePalette.background = toUiColor(palette.background);
	chromePalette.overlayDim = toUiColor(palette.overlayDim);
	if (settingsPanelView_.isPage(SettingsPanelPage::GeneralAudio))
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
		const UiRect& panelBounds = settingsPanelView_.panelBounds();
		chrome.footerBounds = UiRect(panelBounds.x + 16.0f,
			panelBounds.y + panelBounds.height - 62.0f,
			panelBounds.width - 130.0f, 16.0f);
	}
	else
	{
		chrome.footer = settingsSaveStatus_->text();
		const UiRect& panelBounds = settingsPanelView_.panelBounds();
		chrome.footerBounds = UiRect(panelBounds.x + 16.0f,
			panelBounds.y + panelBounds.height - 58.0f,
			panelBounds.width - 120.0f, 16.0f);
	}
	auto renderCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
	DispatchingUiPaintContext<decltype(renderCommand)> paintContext(renderCommand);
	settingsPanelView_.compose(chrome, chromePalette, paintContext);
	const UiWidget& appearanceTab = *settingsPanelView_.controls().find("appearance_tab");
	const UiWidget& algorithmTab = *settingsPanelView_.controls().find("algorithm_tab");
	const UiWidget& audioTab = *settingsPanelView_.controls().find("audio_tab");
	const RenderLayer controlLayer = RenderLayer::SettingsControls;
	const UiWidget* tabs[] = { &appearanceTab, &algorithmTab, &audioTab };
	for (int tab = 0; tab < 3; ++tab)
	{
		submitBox(tabs[tab]->bounds(), tab == settingsPanelView_.pageIndex() ? palette.accent : palette.button, ButtonMaterial, controlLayer);
		drawWidget(*tabs[tab], palette, controlLayer);
	}
	const UiRect activeTab = tabs[settingsPanelView_.pageIndex()]->bounds();
	submitBox(UiRect(activeTab.x, activeTab.y + activeTab.height - 2.0f, activeTab.width, 2.0f),
		palette.accent, AccentMaterial, layerOffset(controlLayer, 1));
	if (settingsPanelView_.isPage(SettingsPanelPage::GeneralAudio))
	{
		const UiWidget* audioGroups[] = { settingsPanelView_.controls().find("audio_basic_tab"), settingsPanelView_.controls().find("audio_advanced_tab") };
		for (int i = 0; i < 2; ++i)
		{
			submitBox(audioGroups[i]->bounds(), ((settingsPanelView_.audioGroup() == SettingsAudioGroup::Advanced) == (i == 1)) ? palette.accent : palette.button,
				ButtonMaterial, controlLayer);
			drawWidget(*audioGroups[i], palette, controlLayer);
		}
	}
	for (std::size_t i = 0; i < settingsPanelView_.controls().widgets().size(); ++i)
	{
		const UiWidget& widget = *settingsPanelView_.controls().widgets()[i];
		if (!widget.visible() || widget.id() == "appearance_tab" || widget.id() == "algorithm_tab" ||
			widget.id() == "audio_tab" || widget.id() == "audio_basic_tab" || widget.id() == "audio_advanced_tab" ||
			widget.id() == "settings_close" || widget.id() == "audio_retry") continue;
		drawSettingsControl(widget, palette);
	}
	drawWidget(*settingsPanelView_.controls().find("settings_close"), palette, RenderLayer::SettingsControls);
	if (settingsPanelView_.isPage(SettingsPanelPage::GeneralAudio))
	{
		drawWidget(*settingsPanelView_.controls().find("audio_retry"), palette, RenderLayer::SettingsControls);
	}
	for (std::size_t i = 0; i < settingsPanelView_.controls().widgets().size(); ++i)
		if (settingsPanelView_.controls().widgets()[i]->visible() && settingsPanelView_.controls().widgets()[i]->expanded())
			drawWidget(*settingsPanelView_.controls().widgets()[i], palette, RenderLayer::SettingsDropdown, true);
}

void ControlPanel::drawSettingsControl(const UiWidget& widget, const ThemePalette& palette)
{
	const UiRect& contentBounds = settingsPanelView_.contentBounds();
	const ClipBounds clip(contentBounds.x, contentBounds.y, contentBounds.width, contentBounds.height);
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
	const UiPresetId preset = drawingAppearanceEditor_ ? appearanceEditorView_.session().model().preset() : renderPreferences_.appearance.activePreset;
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
		const glm::vec4 color = settingsOpen() ? palette.accent : palette.text;
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
	if (!overlayState_.closeSettings()) return;
	updateOverlayInputCapture();
	settingsPanelView_.controls().closeCombo();
}

void ControlPanel::updateOverlayInputCapture()
{
	mainUi_.setInputCaptured(overlayState_.is(UiOverlayId::AppearanceEditor));
	settingsPanelView_.setModalActive(overlayState_.is(UiOverlayId::Settings));
	appearanceEditorView_.controls().setInputCaptured(overlayState_.is(UiOverlayId::AppearanceEditor));
}

void ControlPanel::openAlgorithmMenu()
{
	if (!overlayState_.openAlgorithmBrowser()) return;
	const sortinglogic::AlgorithmDescriptor* current = sortinglogic::algorithmDescriptor(
		static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
	algorithmBrowser_.begin(current ? current->familyId : sortinglogic::FamilyId());
	mainUi_.closeCombo();
	updateOverlayInputCapture();
	SDL_StartTextInput();
	layoutAlgorithmMenu();
}

void ControlPanel::closeAlgorithmMenu()
{
	if (!overlayState_.closeAlgorithmBrowser()) return;
	algorithmBrowser_.reset();
	updateOverlayInputCapture();
	SDL_StopTextInput();
}

void ControlPanel::beginInlineEdit(const std::string& widgetId)
{
	if ((widgetId == "appearance_value" || widgetId == "appearance_color_alpha") && appearanceEditorOpen())
	{
		const InlineEditTarget target = widgetId == "appearance_value"
			? InlineEditTarget::AppearanceNumber : InlineEditTarget::AppearanceAlpha;
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(appearanceEditorView_.session().model().preset()), appearanceEditorView_.session().model().selectedToken());
		if (!descriptor || (target == InlineEditTarget::AppearanceNumber && descriptor->type != UiTokenType::Number) ||
			(target == InlineEditTarget::AppearanceAlpha && descriptor->type != UiTokenType::Color)) return;
		const UiTokenValue value = effectiveUiTokenValue(appearanceEditorView_.session().model().preset(),
			appearanceEditorView_.session().draft().themes[appearanceEditorView_.session().model().preset()], appearanceEditorView_.session().model().selectedToken());
		std::ostringstream text;
		text << std::setprecision(9) << (target == InlineEditTarget::AppearanceAlpha ? value.color.a : value.number);
		inlineEdit_.begin(target, text.str());
		SDL_StartTextInput();
		updateInlineEditLabel();
		return;
	}
	if (widgetId != "bars_value" && widgetId != "time_value") return;
	const InlineEditTarget target = widgetId == "bars_value"
		? InlineEditTarget::ItemCount : InlineEditTarget::Duration;
	const std::string initialText = target == InlineEditTarget::ItemCount
		? std::to_string(settings_.simulation.itemCount)
		: ([this]() { std::ostringstream value; value << std::setprecision(6) << settings_.simulation.durationSeconds; return value.str(); })();
	inlineEdit_.begin(target, initialText);
	SDL_StartTextInput();
	updateInlineEditLabel();
}

void ControlPanel::updateInlineEditLabel()
{
	if (!inlineEdit_.active()) return;
	const InlineEditTarget target = inlineEdit_.target();
	const char* widgetId = inlineEditWidgetId(target);
	if (inlineEdit_.isAppearanceEdit())
	{
		UiWidget* value = appearanceEditorView_.controls().find(widgetId);
		if (value) value->setLabel(inlineEdit_.text() + "|");
		return;
	}
	UiWidget* widget = mainUi_.find(widgetId);
	if (!widget) return;
	widget->setLabel(inlineEdit_.text() + "|" + (target == InlineEditTarget::ItemCount ? " bars" : " sec"));
}

bool ControlPanel::appendInlineEditText(const char* text)
{
	if (!inlineEdit_.appendText(text)) return false;
	updateInlineEditLabel();
	return true;
}

bool ControlPanel::commitInlineEdit(AppAction& action)
{
	if (!inlineEdit_.active()) return false;
	const InlineEditTarget target = inlineEdit_.target();
	if (inlineEdit_.isAppearanceEdit())
	{
		double parsed = 0.0;
		const bool parsedSuccessfully = inlineEdit_.tryParseDouble(parsed);
		const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(appearanceEditorView_.session().model().preset()), appearanceEditorView_.session().model().selectedToken());
		if (!descriptor || !parsedSuccessfully ||
			parsed < descriptor->minimum || parsed > descriptor->maximum)
		{
			appearanceEditorView_.session().error() = descriptor ? "Enter a value between " + std::to_string(descriptor->minimum) + " and " +
				std::to_string(descriptor->maximum) + "." : "The selected appearance property is unavailable.";
			return false;
		}
		std::string error;
		UiTokenValue nextValue;
		if (target == InlineEditTarget::AppearanceAlpha)
		{
			nextValue = effectiveUiTokenValue(appearanceEditorView_.session().model().preset(),
				appearanceEditorView_.session().draft().themes[appearanceEditorView_.session().model().preset()], appearanceEditorView_.session().model().selectedToken());
			if (nextValue.type != UiTokenType::Color) return false;
			nextValue.color.a = static_cast<float>(parsed);
		}
		else nextValue = UiTokenValue::fromNumber(parsed);
		if (!setUiTokenValue(appearanceEditorView_.session().model().preset(), appearanceEditorView_.session().draft().themes[appearanceEditorView_.session().model().preset()],
			appearanceEditorView_.session().model().selectedToken(), nextValue, &error))
		{
			appearanceEditorView_.session().error() = error;
			return false;
		}
		inlineEdit_.finish();
		SDL_StopTextInput();
		appearanceEditorView_.session().error().clear(); synchronizeAppearanceEditorControls();
		return true;
	}
	if (target == InlineEditTarget::ItemCount)
	{
		int value = 0;
		if (!inlineEdit_.tryParseInteger(value)) return false;
		action = AppAction(AppActionKind::SetItemCount, 0.0f, value);
	}
	else if (target == InlineEditTarget::Duration)
	{
		float value = 0.0f;
		if (!inlineEdit_.tryParseFloat(value)) return false;
		action = AppAction(AppActionKind::SetDuration, value);
	}
	else return false;
	inlineEdit_.finish();
	SDL_StopTextInput();
	return true;
}

void ControlPanel::cancelInlineEdit()
{
	if (!inlineEdit_.active()) return;
	const InlineEditTarget target = inlineEdit_.target();
	if (inlineEdit_.isAppearanceEdit())
	{
		inlineEdit_.finish();
		SDL_StopTextInput();
		synchronizeAppearanceEditorControls();
		return;
	}
	const bool editingBarCount = target == InlineEditTarget::ItemCount;
	inlineEdit_.finish();
	SDL_StopTextInput();
	UiWidget* widget = mainUi_.find(editingBarCount ? "bars_value" : "time_value");
	if (widget)
		widget->setLabel(editingBarCount
			? std::to_string(settings_.simulation.itemCount) + " bars"
			: formatDuration(settings_.simulation.durationSeconds));
}

void ControlPanel::openAppearanceEditor()
{
	if (appearanceEditorOpen()) return;
	if (algorithmBrowserOpen()) closeAlgorithmMenu();
	if (!overlayState_.openAppearanceEditor()) return;
	appearanceEditorView_.begin(settings_.appearance);
	settingsPanelView_.handleNavigation(UiAction::ViewCommand::ShowAppearanceSettings);
	appearanceSearchFocused_ = false;
	mainUi_.closeCombo();
	settingsPanelView_.controls().closeCombo();
	appearanceEditorView_.controls().closeCombo();
	updateOverlayInputCapture();
	synchronizeAppearanceEditorControls();
}

void ControlPanel::closeAppearanceEditor(bool restore)
{
	if (!appearanceEditorOpen()) return;
	if (inlineEdit_.active()) cancelInlineEdit();
	if (appearanceSearchFocused_)
	{
		appearanceSearchFocused_ = false;
		SDL_StopTextInput();
	}
	if (restore)
		appearanceEditorView_.restore();
	if (!overlayState_.closeAppearanceEditor()) return;
	appearanceEditorView_.controls().closeCombo();
	updateOverlayInputCapture();
	layoutCacheValid_ = false;
}

void ControlPanel::synchronizeAppearanceEditorControls()
{
	appearanceEditorView_.synchronizeControls(fontFamilyNames, fontStyleNames);
	const UiWidget* token = appearanceEditorView_.controls().find("appearance_token");
	if (token)
	{
		for (std::size_t i = 0; i < token->options().size(); ++i)
		{
			const std::string labelId = token->id() + "#option" + std::to_string(i);
			if (labels_.find(labelId) == labels_.end())
				labels_[labelId].reset(new TextLabel(token->options()[i], "Courier New Bold", 12, { 255, 255, 255, 255 }));
			else labels_[labelId]->setContent(token->options()[i]);
		}
	}
	for (std::size_t i = 0; i < appearanceEditorView_.controls().widgets().size(); ++i)
	{
		const UiWidget& widget = *appearanceEditorView_.controls().widgets()[i];
		std::map<std::string, std::unique_ptr<TextLabel> >::iterator label = labels_.find(widget.id());
		if (label != labels_.end()) label->second->setContent(widget.label());
	}
	layoutAppearanceEditor();
}

void ControlPanel::layoutAppearanceEditor()
{
	if (!appearanceEditorOpen() || width_ <= 0 || height_ <= 0) return;
	appearanceEditorView_.updateLayout(width_, height_);
}

void ControlPanel::drawAppearanceEditor()
{
	if (!appearanceEditorOpen()) return;
	const UiPresetId preset = appearanceEditorView_.session().model().preset();
	const ThemePalette readable = paletteForAppearance(preset,
		defaultUiThemeCustomization(preset, PaletteId::Monochrome));
	drawingAppearanceEditor_ = true;
	const UiRect& controlClip = appearanceEditorView_.layoutResult().contentClip;
	const ClipBounds clip(controlClip.x, controlClip.y, controlClip.width, controlClip.height);
	const UiTokenValue selectedValue = effectiveUiTokenValue(preset, appearanceEditorView_.session().draft().themes[preset], appearanceEditorView_.session().model().selectedToken());
	AppearanceEditorPalette viewPalette;
	viewPalette.panel = toUiColor(readable.panel);
	viewPalette.border = toUiColor(readable.border);
	viewPalette.accent = toUiColor(readable.accent);
	viewPalette.text = toUiColor(readable.text);
	viewPalette.mutedText = toUiColor(readable.mutedText);
	viewPalette.overlayDim = toUiColor(readable.overlayDim);
	const std::string status = appearanceEditorView_.session().error().empty()
		? "Changes preview behind this editor. Apply saves; Cancel discards."
		: appearanceEditorView_.session().error();
	auto renderCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
	DispatchingUiPaintContext<decltype(renderCommand)> paintContext(renderCommand);
	appearanceEditorView_.compose(width_, height_, status, !appearanceEditorView_.session().error().empty(),
		selectedValue, viewPalette, paintContext);
	const char* rowIds[] = { "appearance_preset", "appearance_palette", "appearance_category", "appearance_search", "appearance_token",
		"appearance_value_down", "appearance_value", "appearance_value_up", "appearance_value_slider",
		"appearance_color", "appearance_color_alpha", "appearance_color_r", "appearance_color_g", "appearance_color_b", "appearance_color_a", "appearance_boolean",
		"appearance_choice", "appearance_font_family", "appearance_font_style" };
	for (std::size_t i = 0; i < sizeof(rowIds) / sizeof(rowIds[0]); ++i)
	{
		const UiWidget* widget = appearanceEditorView_.controls().find(rowIds[i]);
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
	const char* actionIds[] = { "appearance_reset_token", "appearance_reset_theme", "appearance_import",
		"appearance_export", "appearance_apply", "appearance_cancel" };
	for (std::size_t i = 0; i < sizeof(actionIds) / sizeof(actionIds[0]); ++i)
	{
		const UiWidget* widget = appearanceEditorView_.controls().find(actionIds[i]);
		if (!widget) continue;
		submitBox(widget->bounds(), widget->id() == "appearance_apply" ? readable.accent : readable.button,
			ButtonMaterial, RenderLayer::SettingsControls);
		drawWidget(*widget, readable, RenderLayer::SettingsControls);
	}
	if (appearanceEditorView_.controls().hasOpenCombo())
		for (std::size_t i = 0; i < appearanceEditorView_.controls().widgets().size(); ++i)
			if (appearanceEditorView_.controls().widgets()[i]->visible() && appearanceEditorView_.controls().widgets()[i]->expanded())
				drawWidget(*appearanceEditorView_.controls().widgets()[i], readable, RenderLayer::SettingsDropdown, true);
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
		appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::FocusLost), lost);
		return true;
	}
	if (inlineEdit_.active() && inlineEdit_.isAppearanceEdit())
	{
		if (event.type == SDL_TEXTINPUT) return appendInlineEditText(event.text.text);
		if (event.type == SDL_KEYDOWN)
		{
			const SDL_Keycode key = event.key.keysym.sym;
			if (key == SDLK_TAB)
			{
				if (!commitInlineEdit(action)) return true;
				UiAction navigation;
				appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab,
					(event.key.keysym.mod & KMOD_SHIFT) != 0), navigation);
				return true;
			}
			if (key == SDLK_ESCAPE) { cancelInlineEdit(); return true; }
			if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { commitInlineEdit(action); return true; }
			if (key == SDLK_BACKSPACE)
			{
				inlineEdit_.backspace();
				updateInlineEditLabel();
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEdit_.clear(); updateInlineEditLabel(); return true;
			}
			return true;
		}
	}
	if (appearanceSearchFocused_)
	{
		if (event.type == SDL_TEXTINPUT)
		{
			appearanceEditorView_.session().model().appendSearch(event.text.text);
			synchronizeAppearanceEditorControls();
			return true;
		}
		if (event.type == SDL_KEYDOWN)
		{
			if (event.key.keysym.sym == SDLK_ESCAPE)
			{
				if (appearanceEditorView_.controls().hasOpenCombo())
				{
					UiAction closed; appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Escape), closed);
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
				appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab,
					(event.key.keysym.mod & KMOD_SHIFT) != 0), navigation);
				return true;
			}
			if (event.key.keysym.sym == SDLK_BACKSPACE)
			{
				appearanceEditorView_.session().model().eraseSearchCharacter();
				synchronizeAppearanceEditorControls(); return true;
			}
			if (event.key.keysym.sym == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{ appearanceEditorView_.session().model().clearSearch(); synchronizeAppearanceEditorControls(); return true; }
			return true;
		}
	}
	if (appearanceSearchFocused_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		!inRect(appearanceEditorView_.controls().find("appearance_search")->bounds(), event.button.x, event.button.y))
	{
		appearanceSearchFocused_ = false;
		SDL_StopTextInput();
	}
	if (inlineEdit_.active() && inlineEdit_.isAppearanceEdit() && event.type == SDL_MOUSEBUTTONDOWN &&
		!commitInlineEdit(action)) return true;
	if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && !appearanceEditorView_.controls().hasOpenCombo())
	{
		closeAppearanceEditor(true);
		return true;
	}
	UiInputEvent input(UiInputType::FocusLost);
	if (!translateSdlUiEvent(event, input, SdlUiKeyProfile::Controls, sdlUiEventContext(event))) return true;
	UiAction uiAction;
	const bool consumed = appearanceEditorView_.controls().handleEvent(input, uiAction);
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
	return consumed || appearanceEditorOpen();
}

void ControlPanel::processAppearanceEditorAction(const UiAction& action, AppAction& output)
{
	const AppearanceEditorActionResult result = appearanceEditorView_.handleAction(action, fontStyleNames);
	if (!result.handled) return;
	if (result.requiresSynchronization) synchronizeAppearanceEditorControls();
	switch (result.effect)
	{
	case AppearanceEditorEffect::BeginNumericEdit: beginInlineEdit("appearance_value"); break;
	case AppearanceEditorEffect::BeginAlphaEdit: beginInlineEdit("appearance_color_alpha"); break;
	case AppearanceEditorEffect::ChooseColor: chooseAppearanceColor(); break;
	case AppearanceEditorEffect::ImportProfile: importAppearanceProfile(); break;
	case AppearanceEditorEffect::ExportProfile: exportAppearanceProfile(); break;
	case AppearanceEditorEffect::Cancel: closeAppearanceEditor(true); break;
	case AppearanceEditorEffect::Apply:
		output = AppAction(AppActionKind::ApplyAppearance);
		output.appearance.reset(new UiAppearancePreferences(appearanceEditorView_.session().draft()));
		break;
	case AppearanceEditorEffect::None: break;
	}
}

void ControlPanel::chooseAppearanceColor()
{
	const UiPresetId preset = appearanceEditorView_.session().model().preset();
	const std::string& selectedToken = appearanceEditorView_.session().model().selectedToken();
	const UiTokenValue value = effectiveUiTokenValue(preset, appearanceEditorView_.session().draft().themes[preset], selectedToken);
	const UiTokenDescriptor* descriptor = findUiToken(uiPresetDefinition(preset), selectedToken);
	if (!descriptor || descriptor->type != UiTokenType::Color || value.type != UiTokenType::Color) return;
	UiColor color;
	if (!AppearanceDialogAdapter::chooseColor(window_, value.color, color)) return;
	std::string error;
	if (!setUiTokenValue(preset, appearanceEditorView_.session().draft().themes[preset], selectedToken,
		UiTokenValue::fromColor(color), &error)) appearanceEditorView_.session().error() = error;
	else appearanceEditorView_.session().error().clear();
	synchronizeAppearanceEditorControls();
}

void ControlPanel::importAppearanceProfile()
{
	const AppearanceDialogResult result = AppearanceDialogAdapter::importProfile(window_);
	if (result.status == AppearanceDialogStatus::Cancelled) return;
	if (result.status == AppearanceDialogStatus::Failed) { appearanceEditorView_.session().error() = result.error; return; }
	const UiAppearanceProfile& profile = result.profile;
	appearanceEditorView_.session().draft().themes[profile.preset] = profile.theme;
	appearanceEditorView_.session().draft().activePreset = profile.preset;
	appearanceEditorView_.session().model().setPreset(profile.preset);
	appearanceEditorView_.session().error().clear();
	if (!profile.ignoredTokens.empty())
	{
		appearanceEditorView_.session().error() = "Imported profile; ignored unknown tokens: ";
		for (std::size_t i = 0; i < profile.ignoredTokens.size(); ++i)
		{
			if (i) appearanceEditorView_.session().error() += ", ";
			appearanceEditorView_.session().error() += profile.ignoredTokens[i];
		}
	}
	synchronizeAppearanceEditorControls();
}

void ControlPanel::exportAppearanceProfile()
{
	const UiPresetId preset = appearanceEditorView_.session().model().preset();
	const AppearanceDialogResult result = AppearanceDialogAdapter::exportProfile(window_, preset,
		appearanceEditorView_.session().draft().themes[preset]);
	if (result.status == AppearanceDialogStatus::Cancelled) return;
	appearanceEditorView_.session().error() = result.status == AppearanceDialogStatus::Failed
		? result.error : "Appearance profile exported.";
}

void ControlPanel::completeActionResult(const AppAction& action, const sortinglogic::CommandResult& result)
{
	if (action.kind == AppActionKind::SelectAlgorithm && algorithmBrowserOpen() && algorithmBrowser_.pendingSelection)
	{
		algorithmBrowser_.pendingSelection = false;
		if (result.accepted) closeAlgorithmMenu();
		else algorithmBrowser_.status = result.message.empty() ? "Pause playback before changing algorithms." : result.message;
		return;
	}
	if (action.kind != AppActionKind::ApplyAppearance || !appearanceEditorOpen()) return;
	if (!result.accepted)
	{
		appearanceEditorView_.session().error() = result.message.empty() ? "Could not save appearance. Check the settings file and try again." : result.message;
		return;
	}
	appearanceEditorView_.accept(settings_.appearance);
	closeAppearanceEditor(false);
}

void ControlPanel::layoutAlgorithmMenu()
{
	if (!algorithmBrowserOpen() || width_ <= 0 || height_ <= 0) return;
	algorithmBrowser_.layout(mainUi_.find("algorithm")->bounds(), width_, height_);
}

bool ControlPanel::handleAlgorithmMenuEvent(const SDL_Event& event, AppAction& action)
{
	UiInputEvent input(UiInputType::FocusLost);
	if (!translateSdlUiEvent(event, input, SdlUiKeyProfile::Search, sdlUiEventContext(event))) return true;
	const AlgorithmBrowserInputResult result = algorithmBrowser_.handleInput(
		input, screenModel_.setupEnabled, settings_.simulation.familyId, settings_.simulation.variantId);
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
		label = settingsPanelView_.isPage(SettingsPanelPage::GeneralAudio) ? audioDeviceStatus_.get() : settingsSaveStatus_.get();
	else
	{
		if (!labels_[command.elementId])
			labels_[command.elementId].reset(new TextLabel(command.text, "Courier New Bold",
				std::max(8, static_cast<int>(std::lround(command.textHeight))), SDL_Color{ 255, 255, 255, 255 }));
		label = labels_[command.elementId].get();
	}
	if (!label) return;
	label->setContent(command.text);
	const TextHorizontalAlignment alignment = command.textAlignment == UiTextAlignment::Left ? TextHorizontalAlignment::Left :
		(command.textAlignment == UiTextAlignment::Right ? TextHorizontalAlignment::Right : TextHorizontalAlignment::Center);
	const TextOverflowPolicy overflow = command.textOverflow == UiTextOverflow::Clamp ? TextOverflowPolicy::Clamp :
		(command.textOverflow == UiTextOverflow::Marquee ? TextOverflowPolicy::Marquee : TextOverflowPolicy::Fit);
	drawText(*label, command.bounds, command.textHeight, color, layer, clip, overflow, alignment);
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
		settings_.simulation.familyId, settings_.simulation.variantId, width_, height_, paintContext);
}
