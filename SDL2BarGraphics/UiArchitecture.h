#pragma once

#include <map>
#include <string>
#include <vector>

#include "../SortingLogic/SortingLogic.h"
#include "UiLayer.h"
#include "UiAppearance.h"
#include "UserSettings.h"

enum class UiTransportState { NoTrace, Ready, Playing, Building, Queued, Preparing, Replay };

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

UiScreenModel buildUiScreenModel(const sortinglogic::RenderSnapshot& snapshot,
	const UserPreferences& preferences, bool playbackPending, const std::string& applicationStatus = std::string());

// Expose semantic Merge roles on the main array when its auxiliary buffer is hidden.
std::vector<sortinglogic::ActiveRole> rolesVisibleOnMainArray(const sortinglogic::RenderSnapshot& snapshot);

// Ctrl scales a step down by one decade; Shift scales it up. Pressing both
// (or neither) keeps the control's normal step size.
float stepDeltaWithModifiers(float baseStep, bool control, bool shift);

// Percentage of the event trace applied by the current cursor. A completed
// empty trace is still complete; a missing or building trace has no progress.
int traceProgressPercent(bool hasTrace, bool complete, std::size_t cursor, std::size_t eventCount);

struct UiSessionState
{
	int settingsPage;
	bool audioAdvanced;
	std::string algorithmSearch;
	std::string algorithmFamily;
	std::string algorithmVariant;
	int algorithmFamilySelection;
	int algorithmVariantSelection;
	float settingsScroll;
	int algorithmFamilyScroll;
	int algorithmVariantScroll;
	int selectedAudioBand;
	UiSessionState();
	void onPresetChanged();
};

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

enum class UiPaintPrimitive { Rectangle, Border, Text, Icon };
struct UiPaintCommand
{
	UiPaintPrimitive primitive;
	UiRect bounds;
	UiColor color;
	std::string text;
	UiPaintCommand(UiPaintPrimitive kind = UiPaintPrimitive::Rectangle, const UiRect& area = UiRect(),
		const UiColor& tint = UiColor(), const std::string& label = std::string())
		: primitive(kind), bounds(area), color(tint), text(label) {}
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
