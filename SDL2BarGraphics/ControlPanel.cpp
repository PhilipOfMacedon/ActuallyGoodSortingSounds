#include "ControlPanel.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
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
		case UiPaintLayer::SettingsControlsOverlay: return layerOffset(RenderLayer::SettingsControls, 1);
		case UiPaintLayer::Text: return RenderLayer::SettingsDropdownText;
		case UiPaintLayer::SettingsText: return RenderLayer::SettingsText;
		case UiPaintLayer::MainControls: return RenderLayer::MainControls;
		case UiPaintLayer::MainControlsOverlay: return layerOffset(RenderLayer::MainControls, 1);
		case UiPaintLayer::MainControlsTop: return layerOffset(RenderLayer::MainControls, 2);
		case UiPaintLayer::MainDropdown: return RenderLayer::Dropdown;
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
		case UiPaintMaterial::PixelIcon: return MaterialId::PixelIcon;
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

	int paletteIndex(PaletteId id) { return static_cast<int>(id); }
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
	inlineEditView_(), mainControlsView_(), timelineView_(), appearanceEditorView_(settingsUiPresetOptions(), settingsPaletteOptions(), settings.appearance,
		paletteIndex(activeTheme(settings).palette)), overlayState_(), algorithmBrowser_(algorithmBrowserCatalog()), algorithmBrowserView_(), settingsPanelView_(), algorithmProfileView_(measureLogicalTextWidth), screenModel_(), layoutResult_(), renderPreferences_(settings), labels_(), paletteCaption_(new TextLabel("Palette", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	spacingCaption_(new TextLabel("Bar spacing", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	title_(new TextLabel("Sorting Simulation | deterministic event trace", "Courier New Bold", 18, { 238, 246, 249, 255 })),
	detail_(new TextLabel("No trace", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	status_(new TextLabel("Ready.", "Courier New Bold", 14, { 238, 246, 249, 255 })),
	collapsedSummary_(new TextLabel("Insertion Sort - 0%", "Courier New Bold", 20, { 255, 255, 255, 255 })),
	settingsTitle_(new TextLabel("Settings", "Courier New Bold", 17, { 238, 246, 249, 255 })),
	settingsSaveStatus_(new TextLabel("Preferences save automatically.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	audioDeviceStatus_(new TextLabel("Audio device is not initialized.", "Courier New Bold", 12, { 238, 246, 249, 255 })),
	displayedSaveStatus_(),
	headerBounds_(), gearBounds_(),
	drawingAppearanceEditor_(false),
	chartViewport_(), width_(0), height_(0), layoutKey_(), layoutCacheValid_(false), laidOutPreset_(settings.appearance.activePreset), fontFallbackActive_(false)
{
	createWidgets();
	updateLayout();
	layoutInteractiveViews();
}

ControlPanel::~ControlPanel()
{
	if (inlineEditView_.active() || algorithmBrowserOpen() || appearanceEditorView_.searchFocused()) SDL_StopTextInput();
}

bool ControlPanel::isReady() const
{
	return renderer_.isReady();
}

void ControlPanel::createWidgets()
{
	mainControlsView_.initializeControls(settings_,
		algorithmLabel(static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm)));
	settingsPanelView_.initializeControls(settings_);
	for (std::size_t i = 0; i < mainControlsView_.controlCount(); ++i)
	{
		const UiWidget& widget = *mainControlsView_.controlAt(i);
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
	for (std::size_t i = 0; i < mainControlsView_.controlCount(); ++i)
	{
		const UiWidget& widget = *mainControlsView_.controlAt(i);
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
	if (layoutCacheValid_ && sameLayoutInputs(layoutKey_, currentKey)) return;
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
	controlIds = mainControlsView_.controlIds();
	layoutResult_ = calculateUiLayout(preset, layoutInput, controlIds);
	mainControlsView_.applyLayout(preset, layoutResult_, width_, settings_.visual.panelExpanded);
	headerBounds_ = layoutResult_.header;
	timelineView_.applyLayout(layoutResult_);
	chartViewport_ = layoutResult_.chartViewport;
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
	gearBounds_ = mainControlsView_.findControl("settings")->bounds();

	const float panelWidth = std::min(520.0f, std::max(180.0f, static_cast<float>(width_) - 32.0f));
	const float panelHeight = std::min(650.0f, std::max(300.0f, static_cast<float>(height_) - 54.0f));
	const UiRect settingsPanelBounds(std::max(8.0f, static_cast<float>(width_) - panelWidth - 8.0f), 42.0f,
		panelWidth, std::min(panelHeight, std::max(200.0f, static_cast<float>(height_) - 50.0f)));
	settingsPanelView_.setPanelBounds(settingsPanelBounds, overlayState_.is(UiOverlayId::Settings));
	if (appearanceEditorOpen()) layoutAppearanceEditor();
}

void ControlPanel::layoutSettingsControls()
{
	if (settingsPanelView_.panelBounds().width <= 0.0f) return;
	const float scrollbarThickness = themeNumber(renderPreferences_, "scrollbar.thickness", 8.0f);
	settingsPanelView_.layoutForAlgorithmSelection(settings_.simulation.familyId,
		settings_.simulation.variantId, scrollbarThickness);
}

void ControlPanel::layoutInteractiveViews()
{
	if (width_ <= 0 || height_ <= 0) return;
	layoutSettingsControls();
	const ResolvedTextStyle popupTextStyle = resolveTextStyle(13.0f);
	mainControlsView_.layoutComboPopups(width_, height_, [popupTextStyle](const std::string& text)
	{
		const glm::vec2 measured = TextLabel::measureText(text, popupTextStyle);
		return measured.y > 0.0f ? measured.x * popupTextStyle.logicalHeight / measured.y : 0.0f;
	});
	layoutAlgorithmMenu();
}

void ControlPanel::updateWidgetStates(const sortinglogic::RenderSnapshot& snapshot, bool playbackPending)
{
	screenModel_ = buildUiScreenModel(snapshot, renderPreferences_, playbackPending, displayedSaveStatus_);
	const bool busy = !screenModel_.setupEnabled;
	mainControlsView_.synchronize(settings_, renderPreferences_.appearance.activePreset, snapshot,
		screenModel_, algorithmLabel(snapshot.algorithm));
	settingsPanelView_.synchronizeControls(settings_, renderPreferences_, snapshot, busy);
	inlineEditView_.synchronizePresentation(mainControlsView_, appearanceEditorView_);
}

void ControlPanel::syncLabels(const sortinglogic::RenderSnapshot& snapshot)
{
	for (std::map<std::string, std::unique_ptr<TextLabel> >::iterator i = labels_.begin(); i != labels_.end(); ++i)
	{
		const UiWidget* widget = mainControlsView_.findControl(i->first);
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
	mainControlsView_.synchronizeText(settings_, renderPreferences_.appearance.activePreset,
		snapshot, screenModel_, SDL_GetTicks());
	const MainControlsText& mainText = mainControlsView_.text();
	title_->setContent(mainText.title);
	detail_->setContent(mainText.detail);
	status_->setContent(mainText.status);
	collapsedSummary_->setContent(mainText.collapsedSummary);
	settingsTitle_->setContent(settingsTabTitle(settingsPanelView_.page(), settings_));
	const UiTokenValue requestedFont = effectiveUiTokenValue(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_), "typeface.primary");
	const std::string saveStatus = fontFallbackActive_ && requestedFont.type == UiTokenType::Font
		? "Font " + requestedFont.font.family + " " + requestedFont.font.style + " unavailable; using bundled Courier New."
		: displayedSaveStatus_.empty() ? "Preferences save automatically." : displayedSaveStatus_;
	settingsSaveStatus_->setContent(saveStatus);
}

bool ControlPanel::handleEvent(const SDL_Event& event, AppAction& action)
{
	if (!appearanceEditorOpen() && !algorithmBrowserOpen() && !appearanceEditorView_.searchFocused() && !inlineEditView_.active() &&
		event.type == SDL_KEYDOWN && !event.key.repeat &&
		event.key.keysym.sym == SDLK_F12 && (event.key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT)) == (KMOD_CTRL | KMOD_SHIFT))
	{
		openAppearanceEditor();
		return true;
	}
	renderPreferences_ = settings_;
	if (appearanceEditorOpen()) renderPreferences_.appearance = appearanceEditorView_.session().draft();
	updateLayout();
	layoutInteractiveViews();
	if (appearanceEditorOpen())
		return handleAppearanceEditorEvent(event, action);
	if (algorithmBrowserOpen())
		return handleAlgorithmMenuEvent(event, action);
	if (!settingsOpen() && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		const UiWidget* bars = mainControlsView_.findControl("bars_value");
		const UiWidget* duration = mainControlsView_.findControl("time_value");
		std::string targetWidgetId;
		if (bars && bars->visible() && bars->enabled() && inRect(bars->bounds(), event.button.x, event.button.y))
			targetWidgetId = "bars_value";
		else if (duration && duration->visible() && duration->enabled() && inRect(duration->bounds(), event.button.x, event.button.y))
			targetWidgetId = "time_value";
		if (!targetWidgetId.empty())
		{
			if (inlineEditView_.editsControl(targetWidgetId)) return true;
			if (inlineEditView_.active() && !commitInlineEdit(action)) return true;
			beginInlineEdit(targetWidgetId);
			return true;
		}
		if (inlineEditView_.active())
		{
			if (!commitInlineEdit(action)) return true;
			return true;
		}
	}
	if (inlineEditView_.active())
	{
		if (event.type == SDL_TEXTINPUT)
			return inlineEditView_.appendText(event.text.text, mainControlsView_, appearanceEditorView_);
		if (event.type == SDL_KEYDOWN)
		{
			const SDL_Keycode key = event.key.keysym.sym;
			if (key == SDLK_ESCAPE)
			{
				cancelInlineEdit();
				return true;
			}
			if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
				return commitInlineEdit(action) || inlineEditView_.active();
			if (key == SDLK_BACKSPACE)
			{
				inlineEditView_.backspace(mainControlsView_, appearanceEditorView_);
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEditView_.clear(mainControlsView_, appearanceEditorView_);
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
	if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		timelineView_.pointerDown(static_cast<float>(event.button.x), static_cast<float>(event.button.y),
			settings_.visual.panelExpanded && !settingsOpen(), action)) return true;
	if (event.type == SDL_MOUSEMOTION && timelineView_.pointerMove(static_cast<float>(event.motion.x), action)) return true;
	if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
		timelineView_.pointerUp(static_cast<float>(event.button.x), action)) return true;
	UiInputEvent input(UiInputType::FocusLost);
	if (!translateSdlUiEvent(event, input, SdlUiKeyProfile::Controls, sdlUiEventContext(event)))
		return settingsOpen() || action.kind != AppActionKind::None;
	UiAction uiAction;
	const bool consumed = settingsOpen() ? settingsPanelView_.controls().handleEvent(input, uiAction) :
		mainControlsView_.handleInput(input, uiAction);
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
		mainControlsView_.resetMenuSelection(action.event.controlId);
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
		mainControlsView_.closeCombo();
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
		settingsPanelView_.synchronizeAudioControls(settings_, nullptr);
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
		mainControlsView_.closeCombo();
		settingsPanelView_.controls().closeCombo();
		timelineView_.cancelDrag();
		cancelInlineEdit();
		updateOverlayInputCapture();
		layoutCacheValid_ = false;
	}
	output = action.appAction;
}

void ControlPanel::render(const sortinglogic::RenderSnapshot& snapshot, const std::string& audioStatus)
{
	if (!isReady()) return;
	const ThemePalette palette = paletteForAppearance(renderPreferences_.appearance.activePreset, activeTheme(renderPreferences_));
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
	layoutInteractiveViews();
	if (isReady()) syncLabels(snapshot);
}

void ControlPanel::drawMainPanel(const sortinglogic::RenderSnapshot& snapshot, const ThemePalette& palette)
{
	const UiPresetId preset = renderPreferences_.appearance.activePreset;
	const bool classicVga = preset == UiPresetId::FlatVgaDos || preset == UiPresetId::ClassicWindows95;
	const bool windows95 = preset == UiPresetId::ClassicWindows95;
	algorithmProfileView_.beginFrame();
	const UiRect screen(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
	auto renderMainControl = [this, &palette](const UiPaintCommand& command)
	{
		drawMainControlPaintCommand(command, palette);
	};
	DispatchingUiPaintContext<decltype(renderMainControl)> paintContext(renderMainControl);
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
	mainControlsView_.compose(MainControlsPaintSection::HeaderButtons, screen, paintContext);
	float titleLeft = 48.0f;
	float titleRight = static_cast<float>(width_) - 54.0f;
	if (classicVga && !windows95 && mainControlsView_.findControl("menu_tools")->visible())
		titleLeft = mainControlsView_.findControl("menu_tools")->bounds().x + mainControlsView_.findControl("menu_tools")->bounds().width + 12.0f;
	if (!mainControlsView_.findControl("settings")->visible()) titleRight = static_cast<float>(width_) - 12.0f;
	UiRect titleBounds(titleLeft, 5.0f, std::max(1.0f, titleRight - titleLeft), 22.0f);
	if (!windows95) drawText(*title_, titleBounds, 17.0f, palette.text, TextLayer, ClipBounds(), TextOverflowPolicy::Marquee);
	mainControlsView_.compose(MainControlsPaintSection::Menus, screen, paintContext);
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
		if (mainControlsView_.findControl("start_overlay")->visible())
		{
			const UiRect chart = chartViewport_;
			const UiRect button = mainControlsView_.findControl("start_overlay")->bounds();
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
			mainControlsView_.compose(MainControlsPaintSection::CollapsedStart, screen, paintContext);
		}
		// The collapsed branch returns before the full control composition. Keep
		// menu popups in the upper overlay pass so the chart cannot hide them.
		mainControlsView_.compose(MainControlsPaintSection::Popups, screen, paintContext);
		return;
	}
	mainControlsView_.compose(MainControlsPaintSection::ExpandedControls, screen, paintContext);
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
		const UiRect seed = mainControlsView_.findControl("seed_readout")->bounds();
		auto renderProfileCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
		DispatchingUiPaintContext<decltype(renderProfileCommand)> paintContext(renderProfileCommand);
		algorithmProfileView_.compose(screenModel_.algorithm, screenModel_.algorithmCapabilities,
			true, width_, layoutResult_.setupRail, seed, resolveTextStyle(13.0f), resolveTextStyle(11.0f),
			profilePalette, paintContext);
	}
	if (snapshot.hasTrace || snapshot.buildingTrace || classicVga)
	{
		const double timelineDuration = settings_.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence
			? playbacktimebase::durationForReferenceEvents(snapshot.audioEligibleEventCount) : snapshot.durationSeconds;
		TimelineViewPalette timelinePalette;
		timelinePalette.rail = toUiColor(palette.timelineRail);
		timelinePalette.accent = toUiColor(palette.accent);
		timelinePalette.thumb = toUiColor(palette.timelineThumb);
		timelinePalette.button = toUiColor(palette.button);
		timelinePalette.text = toUiColor(palette.text);
		timelinePalette.border = toUiColor(palette.border);
		timelinePalette.brightEdge = windows95 ? UiColor(0.96f, 0.96f, 0.96f, 1.0f) : timelinePalette.text;
		timelinePalette.darkEdge = windows95 ? UiColor(0.25f, 0.25f, 0.25f, 1.0f) : timelinePalette.border;
		auto renderTimelineCommand = [this](const UiPaintCommand& command) { drawUiPaintCommand(command); };
		DispatchingUiPaintContext<decltype(renderTimelineCommand)> timelinePaintContext(renderTimelineCommand);
		timelineView_.compose(settings_.visual.panelExpanded, classicVga, windows95, timelineDuration,
			snapshot, timelinePalette, screen, timelinePaintContext);
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
		const float detailY = timelineView_.trackBounds().y + 14.0f;
		drawText(*detail_, UiRect(16.0f, detailY, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.text, TextLayer);
		drawText(*status_, UiRect(16.0f, detailY + 19.0f, static_cast<float>(width_) - 32.0f, 18.0f), 13.0f, palette.mutedText, TextLayer);
	}
	// Open combo options are submitted last within the UI layer, preserving their painter order over the chart.
	mainControlsView_.compose(MainControlsPaintSection::Popups, screen, paintContext);
}

void ControlPanel::drawMainControlPaintCommand(const UiPaintCommand& command, const ThemePalette& palette)
{
	if (command.primitive != UiPaintPrimitive::Control) return;
	const UiWidget* widget = mainControlsView_.findControl(command.elementId);
	if (!widget || !widget->visible()) return;
	drawWidget(*widget, palette, renderLayerForUiPaint(command.layer), command.popup);
}

void ControlPanel::drawSettingsPanel(const ThemePalette& palette, const std::string& audioStatus)
{
	std::string footer;
	SettingsPanelPalette chromePalette;
	chromePalette.panel = toUiColor(palette.panel);
	chromePalette.accent = toUiColor(palette.accent);
	chromePalette.button = toUiColor(palette.button);
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
		footer = profileStatus;
	}
	else footer = settingsSaveStatus_->text();
	auto renderCommand = [this, &palette](const UiPaintCommand& command)
	{
		if (command.primitive == UiPaintPrimitive::Control)
			drawSettingsPaintCommand(command, palette);
		else drawUiPaintCommand(command);
	};
	DispatchingUiPaintContext<decltype(renderCommand)> paintContext(renderCommand);
	settingsPanelView_.compose(width_, height_, settingsTitle_->text(), footer, chromePalette, paintContext);
}

void ControlPanel::drawSettingsPaintCommand(const UiPaintCommand& command, const ThemePalette& palette)
{
	const UiWidget* widget = settingsPanelView_.controls().find(command.elementId);
	if (!widget || !widget->visible()) return;
	const RenderLayer layer = renderLayerForUiPaint(command.layer);
	const ClipBounds clip = renderClip(command);
	if (command.popup)
	{
		drawWidget(*widget, palette, layer, true);
		return;
	}
	if (command.material == UiPaintMaterial::Button || command.material == UiPaintMaterial::Accent)
	{
		submitBox(command.bounds, toGlmColor(command.color), materialForUiPaint(command.material), layer, clip);
		drawWidget(*widget, palette, layer);
		return;
	}
	if (widget->kind() == UiWidgetKind::Button)
		drawWidget(*widget, palette, layer);
	else
		drawSettingsControl(*widget, palette);
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
	mainControlsView_.setInputCaptured(overlayState_.is(UiOverlayId::AppearanceEditor));
	settingsPanelView_.setModalActive(overlayState_.is(UiOverlayId::Settings));
	appearanceEditorView_.controls().setInputCaptured(overlayState_.is(UiOverlayId::AppearanceEditor));
}

void ControlPanel::openAlgorithmMenu()
{
	if (!overlayState_.openAlgorithmBrowser()) return;
	const sortinglogic::AlgorithmDescriptor* current = sortinglogic::algorithmDescriptor(
		static_cast<sortinglogic::SortAlgorithm>(settings_.simulation.algorithm));
	algorithmBrowser_.begin(current ? current->familyId : sortinglogic::FamilyId());
	mainControlsView_.closeCombo();
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
	const bool appearanceControl = widgetId == "appearance_value" || widgetId == "appearance_color_alpha";
	if (appearanceControl && !appearanceEditorOpen()) return;
	if (!inlineEditView_.beginForControl(widgetId, settings_.simulation.itemCount,
		settings_.simulation.durationSeconds, mainControlsView_, appearanceEditorView_)) return;
	SDL_StartTextInput();
}

bool ControlPanel::commitInlineEdit(AppAction& action)
{
	bool appearanceNeedsSynchronization = false;
	if (!inlineEditView_.commit(mainControlsView_, appearanceEditorView_, action, appearanceNeedsSynchronization)) return false;
	SDL_StopTextInput();
	if (appearanceNeedsSynchronization) synchronizeAppearanceEditorControls();
	return true;
}

void ControlPanel::cancelInlineEdit()
{
	const InlineEditTarget cancelledTarget = inlineEditView_.cancel(mainControlsView_,
		settings_.simulation.itemCount, settings_.simulation.durationSeconds);
	if (cancelledTarget == InlineEditTarget::None) return;
	SDL_StopTextInput();
	if (cancelledTarget == InlineEditTarget::AppearanceNumber || cancelledTarget == InlineEditTarget::AppearanceAlpha)
		synchronizeAppearanceEditorControls();
}

void ControlPanel::openAppearanceEditor()
{
	if (appearanceEditorOpen()) return;
	if (algorithmBrowserOpen()) closeAlgorithmMenu();
	if (!overlayState_.openAppearanceEditor()) return;
	appearanceEditorView_.begin(settings_.appearance);
	settingsPanelView_.handleNavigation(UiAction::ViewCommand::ShowAppearanceSettings);
	mainControlsView_.closeCombo();
	settingsPanelView_.controls().closeCombo();
	appearanceEditorView_.controls().closeCombo();
	updateOverlayInputCapture();
	synchronizeAppearanceEditorControls();
}

void ControlPanel::closeAppearanceEditor(bool restore)
{
	if (!appearanceEditorOpen()) return;
	if (inlineEditView_.active()) cancelInlineEdit();
	if (appearanceEditorView_.searchFocused())
	{
		appearanceEditorView_.blurSearch();
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
		appearanceEditorView_.blurSearch();
		SDL_StopTextInput();
		UiAction lost;
		appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::FocusLost), lost);
		return true;
	}
	if (inlineEditView_.active() && inlineEditView_.isAppearanceEdit())
	{
		if (event.type == SDL_TEXTINPUT)
			return inlineEditView_.appendText(event.text.text, mainControlsView_, appearanceEditorView_);
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
				inlineEditView_.backspace(mainControlsView_, appearanceEditorView_);
				return true;
			}
			if (key == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{
				inlineEditView_.clear(mainControlsView_, appearanceEditorView_); return true;
			}
			return true;
		}
	}
	if (appearanceEditorView_.searchFocused())
	{
		if (event.type == SDL_TEXTINPUT)
		{
			appearanceEditorView_.appendSearchText(event.text.text);
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
			{ appearanceEditorView_.blurSearch(); SDL_StopTextInput(); return true; }
			if (event.key.keysym.sym == SDLK_TAB)
			{
				appearanceEditorView_.blurSearch(); SDL_StopTextInput();
				UiAction navigation;
				appearanceEditorView_.controls().handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab,
					(event.key.keysym.mod & KMOD_SHIFT) != 0), navigation);
				return true;
			}
			if (event.key.keysym.sym == SDLK_BACKSPACE)
			{
				appearanceEditorView_.eraseSearchCharacter();
				synchronizeAppearanceEditorControls(); return true;
			}
			if (event.key.keysym.sym == SDLK_a && (event.key.keysym.mod & KMOD_CTRL) != 0)
			{ appearanceEditorView_.clearSearch(); synchronizeAppearanceEditorControls(); return true; }
			return true;
		}
	}
	if (appearanceEditorView_.searchFocused() && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT &&
		!inRect(appearanceEditorView_.controls().find("appearance_search")->bounds(), event.button.x, event.button.y))
	{
		appearanceEditorView_.blurSearch();
		SDL_StopTextInput();
	}
	if (inlineEditView_.active() && inlineEditView_.isAppearanceEdit() && event.type == SDL_MOUSEBUTTONDOWN &&
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
			appearanceEditorView_.focusSearch();
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
	algorithmBrowser_.layout(mainControlsView_.findControl("algorithm")->bounds(), width_, height_);
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
