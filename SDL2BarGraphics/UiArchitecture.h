#pragma once

#include <map>
#include <string>
#include <vector>

#include "UiLayer.h"
#include "UiAppearance.h"

enum class UiTransportState { NoTrace, Ready, Playing, Building, Queued, Preparing, Replay };

enum class UiOverlayId { None, Settings, AlgorithmBrowser, AppearanceEditor };

// Keeps modal ownership in one place. The appearance editor may be opened from
// Settings and returns there when dismissed; all other overlays are exclusive.
class UiOverlayState
{
public:
	UiOverlayState();

	UiOverlayId active() const { return active_; }
	bool is(UiOverlayId overlay) const { return active_ == overlay; }
	bool settingsSurfaceOpen() const;
	bool modalOpen() const { return active_ != UiOverlayId::None; }

	bool openSettings();
	bool closeSettings();
	bool openAlgorithmBrowser();
	bool closeAlgorithmBrowser();
	bool openAppearanceEditor();
	bool closeAppearanceEditor();

private:
	UiOverlayId active_;
	UiOverlayId appearanceReturnOverlay_;
};

struct UiTelemetryValue
{
	std::string key;
	std::string caption;
	std::string value;
	UiTelemetryValue(const std::string& stableKey = std::string(), const std::string& title = std::string(),
		const std::string& displayedValue = std::string()) : key(stableKey), caption(title), value(displayedValue) {}
};

struct UiAlgorithmCapabilities
{
	bool available;
	bool stable;
	bool inPlace;
	bool supportsNormalizedFloat;
	bool supportsIntegerKeys;
	std::size_t safeMaximumItemCount;
	std::string timeComplexity;
	std::string spaceComplexity;
	std::string behavior;
	std::string compatibilityMessage;
	std::vector<UiTelemetryValue> metrics;
	UiAlgorithmCapabilities() : available(false), stable(false), inPlace(false), supportsNormalizedFloat(false),
		supportsIntegerKeys(false), safeMaximumItemCount(0), timeComplexity(), spaceComplexity(), behavior(),
		compatibilityMessage(), metrics() {}
};

struct UiScreenModel
{
	UiPresetId preset;
	UiTransportState transport;
	bool setupEnabled;
	bool runEnabled;
	bool playEnabled;
	bool countEditable;
	bool durationEditable;
	bool descending;
	bool hasTrace;
	bool buildingTrace;
	bool playing;
	bool completed;
	std::size_t itemCount;
	std::size_t cursor;
	std::size_t eventCount;
	std::size_t audioEligibleEventCount;
	std::uint32_t seed;
	std::string algorithm;
	std::string inputDomain;
	std::string inputPattern;
	std::string shellGapSequence;
	std::string direction;
	std::string playCaption;
	std::string durationCaption;
	std::string status;
	UiAlgorithmCapabilities algorithmCapabilities;
	std::vector<UiTelemetryValue> telemetry;
	UiScreenModel();
};

// Percentage of the event trace applied by the current cursor. A completed
// empty trace is still complete; a missing or building trace has no progress.
int traceProgressPercent(bool hasTrace, bool complete, std::size_t cursor, std::size_t eventCount);

struct UiLayoutInput
{
	float width;
	float height;
	bool expanded;
	float headerHeight;
	float menuHeight;
	float margin;
	float gap;
	float controlHeight;
	float railWidth;
	float transportHeight;
	float chartInset;
	float contentGap;
	float timelineArrowSize;
	UiLayoutInput();
};

struct UiLayoutResult
{
	UiRect header;
	UiRect menuBar;
	UiRect setupRail;
	UiRect transportArea;
	UiRect timeline;
	UiRect timelineTrack;
	UiRect timelineLeftArrow;
	UiRect timelineRightArrow;
	UiRect chartViewport;
	UiRect telemetry;
	std::map<std::string, UiRect> controlBounds;
	std::map<std::string, UiRect> popupBounds;
	std::map<std::string, std::vector<UiRect> > interactiveSubregions;
	std::map<std::string, UiRect> clips;
	std::vector<std::string> focusOrder;
	UiRect control(const std::string& id) const;
};

UiLayoutResult calculateUiLayout(UiPresetId preset, const UiLayoutInput& input,
	const std::vector<std::string>& controlIds);

struct UiControlPaintStyle
{
	UiColor fill;
	UiColor text;
	UiColor border;
	float borderWidth;
	float cornerRadius;
	bool drawArrow;
	bool squareIndicator;
	UiControlPaintStyle();
};

UiControlPaintStyle resolveUiControlPaintStyle(UiPresetId preset, const UiThemeCustomization& theme,
	const std::string& role, bool enabled, bool hovered, bool pressed, bool selected,
	const std::map<std::string, UiColor>& resolvedPalette);

enum class UiPaintPrimitive { Rectangle, Border, Text, Icon, Control };
enum class UiPaintMaterial { Solid, Panel, Button, Timeline, TimelineThumb, PixelIcon, Popup, Accent, ModalDim };
enum class UiPaintLayer { Backdrop, Panel, Accent, Content, SettingsControls, SettingsControlsOverlay, Text, SettingsText,
	MainControls, MainControlsOverlay, MainControlsTop, MainDropdown, MainText };
enum class UiTextAlignment { Center, Left, Right };
enum class UiTextOverflow { Fit, Clamp, Marquee };
struct UiPaintCommand
{
	UiPaintPrimitive primitive;
	UiRect bounds;
	UiColor color;
	std::string text;
	std::string elementId;
	UiPaintMaterial material;
	UiPaintLayer layer;
	UiRect clip;
	bool hasClip;
	float textHeight;
	UiTextAlignment textAlignment;
	UiTextOverflow textOverflow;
	bool popup;
	UiPaintCommand(UiPaintPrimitive kind = UiPaintPrimitive::Rectangle, const UiRect& area = UiRect(),
		const UiColor& tint = UiColor(), const std::string& label = std::string())
		: primitive(kind), bounds(area), color(tint), text(label), elementId(), material(UiPaintMaterial::Solid),
			layer(UiPaintLayer::Content), clip(), hasClip(false), textHeight(12.0f),
			textAlignment(UiTextAlignment::Center), textOverflow(UiTextOverflow::Fit), popup(false) {}
	UiPaintCommand(UiPaintPrimitive kind, const UiRect& area, const UiColor& tint, const std::string& label,
		const std::string& stableElementId, UiPaintMaterial paintMaterial, UiPaintLayer paintLayer,
		const UiRect& clipBounds, bool clipped, float targetTextHeight, bool isPopup = false)
		: primitive(kind), bounds(area), color(tint), text(label), elementId(stableElementId), material(paintMaterial),
			layer(paintLayer), clip(clipBounds), hasClip(clipped), textHeight(targetTextHeight),
			textAlignment(UiTextAlignment::Center), textOverflow(UiTextOverflow::Fit), popup(isPopup) {}
};

class UiPaintContext
{
public:
	virtual ~UiPaintContext() {}
	virtual void submit(const UiPaintCommand& command) = 0;
};

class RecordingUiPaintContext : public UiPaintContext
{
public:
	void submit(const UiPaintCommand& command) override { commands_.push_back(command); }
	const std::vector<UiPaintCommand>& commands() const { return commands_; }
	void clear() { commands_.clear(); }
private:
	std::vector<UiPaintCommand> commands_;
};
