#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <iostream>
#include <cmath>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

#include "../SDL2BarGraphics/LayoutMetrics.h"
#include "../SDL2BarGraphics/BarCountSteps.h"
#include "../SDL2BarGraphics/AppController.h"
#include "../SDL2BarGraphics/AudioPresentationBridge.h"
#include "../SDL2BarGraphics/RenderPipeline.h"
#include "../SDL2BarGraphics/SettingsScroll.h"
#include "../SDL2BarGraphics/UiLayer.h"
#include "../SDL2BarGraphics/UserSettings.h"

namespace
{
	int failures = 0;

	void check(bool condition, const char* message)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}

	void waitForAudioTimeline(AudioPresentationBridge& bridge, const sortinglogic::Simulation& simulation,
		const SoundEngine::Parameters& parameters, LatestAudioStateMailbox& mailbox,
		const SoundEngine::ReconstructedParameters& reconstructed = SoundEngine::ReconstructedParameters(),
		SoundEngine::PlaybackTimingMode mode = SoundEngine::PlaybackTimingMode::FixedDuration)
	{
		for (unsigned int attempt = 0; attempt < 5000; ++attempt)
		{
			bridge.update(simulation, simulation.snapshot(), parameters, mailbox, reconstructed, mode);
			if (!bridge.timelinePreparing()) return;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	class MemorySettings : public ISettingsPersistence
	{
	public:
		MemorySettings() : value(), writes(0) {}
		UserPreferences load() const override { return value; }
		bool save(const UserPreferences& settings, std::string* error = nullptr) const override
		{
			value = settings;
			++writes;
			if (error) error->clear();
			return true;
		}
		mutable UserPreferences value;
		mutable int writes;
	};

	void writeFile(const std::wstring& path, const std::string& text)
	{
		HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			check(false, "create test settings file");
			return;
		}
		DWORD written = 0;
		const BOOL result = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
		check(result && written == text.size(), "write test settings file");
		CloseHandle(file);
	}

	void testSpacing()
	{
		const float slot80 = 968.0f / 80.0f;
		const BarSlotMetrics none = calculateBarSlotMetrics(slot80, BarSpacingMode::None);
		const BarSlotMetrics compact = calculateBarSlotMetrics(slot80, BarSpacingMode::Compact);
		const BarSlotMetrics comfortable = calculateBarSlotMetrics(slot80, BarSpacingMode::Comfortable);
		check(none.gutter == 0.0f && none.barWidth == slot80, "None spacing removes the visual gutter");
		check(compact.gutter == slot80 * 0.16f, "Compact preserves the existing adaptive gutter at 80 bars");
		check(comfortable.gutter > compact.gutter, "Comfortable is wider than Compact at 80 bars");

		const float slot800 = 968.0f / 800.0f;
		const BarSlotMetrics denseCompact = calculateBarSlotMetrics(slot800, BarSpacingMode::Compact);
		const BarSlotMetrics denseComfortable = calculateBarSlotMetrics(slot800, BarSpacingMode::Comfortable);
		check(denseComfortable.gutter > denseCompact.gutter, "Comfortable remains distinct at 800 bars");
		check(denseComfortable.barWidth > slot800 * 0.70f, "800-bar Comfortable mode keeps most of each slot visible");

		const ResponsivePanelLayout widePanel = calculatePanelLayout(1000, true);
		const ResponsivePanelLayout narrowPanel = calculatePanelLayout(640, true);
		const ResponsivePanelLayout collapsedPanel = calculatePanelLayout(1000, false);
		check(widePanel.wide && widePanel.rowCount == 2 && widePanel.chartTop == 184.0f, "wide panel uses two rows and leaves more chart space");
		check(!narrowPanel.wide && narrowPanel.rowCount == 4 && narrowPanel.chartTop > widePanel.chartTop, "narrow panel reflows into four rows");
		check(collapsedPanel.chartTop < widePanel.chartTop, "collapsed panel reclaims chart height");
		const LayoutCacheKey base(1000, 700, 2000, 1400, true);
		check(sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2000, 1400, true)),
			"layout cache stays valid when logical size, drawable size, and panel state are unchanged");
		check(!sameLayoutInputs(base, LayoutCacheKey(900, 700, 1800, 1400, true)) &&
			!sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2500, 1750, true)) &&
			!sameLayoutInputs(base, LayoutCacheKey(1000, 700, 2000, 1400, false)),
			"layout cache invalidates on resize, DPI change, or persisted panel-state change");
	}

	void testBarCountSteps()
	{
		check(adjustBarCount(2, true, false, 2, 800) == 3, "Bars + increments by one from the minimum");
		check(adjustBarCount(2, true, true, 2, 800) == 10, "Bars ++ snaps the minimum to the next decade");
		check(adjustBarCount(10, true, true, 2, 800) == 20, "Bars ++ advances from a clean decade");
		check(adjustBarCount(12, true, true, 2, 800) == 20, "Bars ++ advances to the next decade from an offset count");
		check(adjustBarCount(12, false, true, 2, 800) == 10, "Bars -- returns to the previous decade");
		check(adjustBarCount(10, false, true, 2, 800) == 2, "Bars -- respects the minimum below the first decade");
		check(adjustBarCount(3, false, false, 2, 800) == 2, "Bars - clamps at the minimum");
		check(adjustBarCount(799, true, true, 2, 800) == 800, "Bars ++ clamps at the maximum");
		check(adjustBarCount(800, true, false, 2, 800) == 800, "Bars + clamps at the maximum");
	}

	void testRenderPipelineContracts()
	{
		const MaterialTable materials;
		const MaterialState solid = materials.stateFor(MaterialId::Bar);
		const MaterialState text = materials.stateFor(MaterialId::Text);
		const MaterialState dim = materials.stateFor(MaterialId::ModalDim);
		check(solid.blendMode == BlendMode::Opaque && solid.textureBinding == TextureBinding::None,
			"bar material has explicit opaque and texture-free state");
		check(text.blendMode == BlendMode::Alpha && text.textureBinding == TextureBinding::CommandTexture,
			"text material binds the submitting label texture with alpha blending");
		check(dim.blendMode == BlendMode::Alpha && dim.textureBinding == TextureBinding::None,
			"modal dim material uses alpha blending without a texture");
		MaterialTable extensibleMaterials;
		const MaterialId custom = static_cast<MaterialId>(2001);
		extensibleMaterials.registerMaterial(custom, MaterialState(BlendMode::Alpha, TextureBinding::CommandTexture));
		check(extensibleMaterials.stateFor(custom).blendMode == BlendMode::Alpha,
			"renderer material table accepts an additional material contract");
		const RenderBatchKey first(RenderLayer::MainControls, MaterialId::Button, 0);
		const RenderBatchKey same(RenderLayer::MainControls, MaterialId::Button, 0);
		const RenderBatchKey otherTexture(RenderLayer::MainControls, MaterialId::Button, 42);
		const RenderBatchKey otherLayer(RenderLayer::MainText, MaterialId::Button, 0);
		const RenderBatchKey otherClip(RenderLayer::MainControls, MaterialId::Button, 0, true, 0, 0, 20, 20);
		check(compatibleBatch(first, same), "adjacent commands with matching material state can batch");
		check(!compatibleBatch(first, otherTexture) && !compatibleBatch(first, otherLayer),
			"texture and painter layer changes break a batch");
		check(!compatibleBatch(first, otherClip), "clip state changes break a batch");
		check(static_cast<unsigned int>(RenderLayer::ChartBars) < static_cast<unsigned int>(RenderLayer::MainControls) &&
			static_cast<unsigned int>(RenderLayer::MainControls) < static_cast<unsigned int>(RenderLayer::ModalDim),
			"typed layers preserve chart, control, and modal painter order");
		check(painterLayerBefore(RenderLayer::ChartBars, RenderLayer::MainControls) &&
			!painterLayerBefore(RenderLayer::MainControls, RenderLayer::ChartBars),
			"renderer queue comparator orders chart before controls and controls before modal layers");
	}

	void testLayoutAndWidgets()
	{
		const float afterWheel = settingsScrollOffsetAfterWheel(0.0f, -1, 48.0f, 28.0f * 48.0f, 400.0f);
		check(afterWheel == 144.0f && settingsScrollOffsetAfterWheel(afterWheel, 1, 48.0f, 28.0f * 48.0f, 400.0f) == 0.0f,
			"audio settings scrolling advances and reverses by three rows per wheel notch");
		check(settingsScrollOffsetAfterWheel(5000.0f, -1, 48.0f, 28.0f * 48.0f, 400.0f) == 944.0f,
			"audio settings scrolling clamps at the content end");

		UIRoot root;
	root.addButton("action", "Action", AppAction(AppActionKind::Run));
	root.addComboBox("choice", std::vector<std::string>{ "One", "Two", "Three" }, 0,
		AppAction(AppActionKind::SelectAlgorithm));
		root.addContainer("panel", UiContainerKind::Panel);
		root.addContainer("row", UiContainerKind::Row, "panel");
		root.attachWidget("action", "row");
		root.attachWidget("choice", "row");
		root.layoutContainer("panel", UiRect(0.0f, 0.0f, 240.0f, 60.0f));
		root.layoutContainer("row", UiRect(10.0f, 20.0f, 210.0f, 30.0f), 10.0f);
		check(root.find("action")->bounds().x == 10.0f, "row layout starts at the requested inset");
		check(root.find("action")->bounds().width == 100.0f, "row layout distributes width after gaps");
		check(root.find("choice")->bounds().x == 120.0f, "row layout places the next widget after its gap");

		UiAction action;
		const UiRect combo = root.find("choice")->bounds();
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action), "pointer opens a combo box");
		check(root.hasOpenCombo(), "combo box retains its expanded state");
		const UiRect secondOption = root.find("choice")->optionBounds(1);
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, secondOption.x + 5.0f, secondOption.y + 5.0f), action), "pointer selects a dropdown item");
		check(action.appAction.kind == AppActionKind::SelectAlgorithm && action.appAction.selection == 1,
			"dropdown emits a typed selected action");
		check(!root.hasOpenCombo(), "selection closes the dropdown");
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action),
			"visible combo can be reopened after selection");
		root.setWidgetVisible("choice", true);
		check(root.hasOpenCombo(), "reapplying visible state preserves an open combo during layout refresh");
		root.closeCombo();

		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), action), "Tab moves keyboard focus");
		check(root.find("action")->focused(), "first widget receives keyboard focus");
		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action), "Enter activates a focused button");
		check(action.appAction.kind == AppActionKind::Run, "focused button emits a typed action");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Tab), action);
		check(root.find("choice")->focused(), "Tab advances to the next widget");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action);
		check(root.hasOpenCombo(), "Enter opens a focused combo box");
		root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Down), action);
		check(action.appAction.kind == AppActionKind::SelectAlgorithm && action.appAction.selection == 2,
			"arrow keys emit the selected typed action");
		root.setWidgetEnabled("choice", false);
		check(!root.find("choice")->expanded(), "disabling a combo closes it");
		root.setWidgetEnabled("choice", true);
		root.handleEvent(UiInputEvent(UiInputType::PointerDown, combo.x + 5.0f, combo.y + 5.0f), action);
		root.setWidgetEnabled("choice", false);
		check(!root.hasOpenCombo(), "disabling an open combo releases root pointer capture");
		root.setInputCaptured(true);
		root.setModalBounds(UiRect(100.0f, 100.0f, 120.0f, 80.0f), UiAction::ViewCommand::CloseSettings);
		check(root.handleEvent(UiInputEvent(UiInputType::PointerDown, 500.0f, 500.0f), action), "modal root captures pointer input outside widgets");
		check(action.viewCommand == UiAction::ViewCommand::CloseSettings,
			"clicking outside the modal emits its typed dismissal action");
		check(root.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Escape), action), "Escape is captured by the modal root");
		check(action.viewCommand == UiAction::ViewCommand::CloseSettings,
			"Escape emits the modal dismissal action");
		UIRoot tabRoot;
		tabRoot.addButton("appearance", "Appearance", AppAction(), UiAction::ViewCommand::ShowAppearanceSettings);
		tabRoot.addButton("audio", "Audio", AppAction(), UiAction::ViewCommand::ShowAudioSettings);
		tabRoot.find("appearance")->setBounds(UiRect(0.0f, 0.0f, 80.0f, 24.0f));
		tabRoot.find("audio")->setBounds(UiRect(90.0f, 0.0f, 80.0f, 24.0f));
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 10.0f, 10.0f), action);
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 10.0f, 10.0f), action);
		check(action.viewCommand == UiAction::ViewCommand::ShowAppearanceSettings,
			"Appearance tab emits a typed view-selection command");
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 100.0f, 10.0f), action);
		tabRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 100.0f, 10.0f), action);
		check(action.viewCommand == UiAction::ViewCommand::ShowAudioSettings,
			"Audio tab emits a typed view-selection command");
		UIRoot capabilityRoot;
		capabilityRoot.addComboBox("capability", std::vector<std::string>{ "Float", "Integer only", "Float too" }, 0,
			AppAction(AppActionKind::SelectAlgorithm));
		capabilityRoot.find("capability")->setBounds(UiRect(10.0f, 10.0f, 120.0f, 24.0f));
		capabilityRoot.find("capability")->setOptionEnabled(1, false);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 15.0f, 15.0f), action);
		const UiRect disabledOption = capabilityRoot.find("capability")->optionBounds(1);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, disabledOption.x + 4.0f, disabledOption.y + 4.0f), action);
		check(capabilityRoot.find("capability")->selectedIndex() == 0,
			"combo box does not allow choosing an incompatible option");
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Enter), action);
		capabilityRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Down), action);
		check(capabilityRoot.find("capability")->selectedIndex() == 2,
			"keyboard navigation skips disabled options");

		UIRoot sliderRoot;
		sliderRoot.addSlider("gain", "Gain", 0.0f, 100.0f, 1.0f, 25.0f,
			AppAction(AppActionKind::SetAudioParameter, 0.0f, 0, AudioParameterId::MasterLevel));
		sliderRoot.find("gain")->setBounds(UiRect(100.0f, 200.0f, 200.0f, 40.0f));
		check(sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 200.0f, 220.0f), action),
			"slider captures pointer down");
		check(action.appAction.kind == AppActionKind::SetAudioParameter && action.appAction.value == 50.0f,
			"slider pointer down emits its normalized parameter value");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerMove, 260.0f, 500.0f), action);
		check(action.appAction.value == 80.0f, "captured slider continues tracking the pointer outside its row");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::PointerUp, 260.0f, 500.0f), action);
		check(sliderRoot.find("gain")->value() == 80.0f, "pointer release ends slider capture without losing the value");
		sliderRoot.handleEvent(UiInputEvent(UiInputType::KeyDown, 0, 0, UiKey::Right), action);
		check(sliderRoot.find("gain")->value() == 81.0f && action.appAction.value == 81.0f,
			"focused slider supports keyboard adjustment");

		std::vector<std::string> sixtyBands;
		for (int i = 0; i < 60; ++i) sixtyBands.push_back("band " + std::to_string(i));
		UIRoot bandRoot;
		bandRoot.addComboBox("band", sixtyBands, 59, AppAction(AppActionKind::SelectAudioBand));
		bandRoot.find("band")->setBounds(UiRect(20.0f, 100.0f, 240.0f, 24.0f));
		bandRoot.find("band")->setPopupAbove(true);
		const UiRect visibleBand = bandRoot.find("band")->optionBounds(55);
		check(visibleBand.y < 100.0f && bandRoot.find("band")->optionBounds(0).width == 0.0f,
			"long selectors expose a bounded visible option window near the current selection");
		bandRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, 25.0f, 105.0f), action);
		bandRoot.handleEvent(UiInputEvent(UiInputType::PointerDown, visibleBand.x + 4.0f, visibleBand.y + 4.0f), action);
		check(action.appAction.kind == AppActionKind::SelectAudioBand && action.appAction.selection == 55,
			"a high frequency band remains directly selectable in the bounded dropdown");
	}

	void testSettingsStore()
	{
		wchar_t temporary[MAX_PATH] = {};
		const DWORD tempCount = GetTempPathW(MAX_PATH, temporary);
		check(tempCount > 0 && tempCount < MAX_PATH, "resolve temporary directory");
		const std::wstring path = std::wstring(temporary) + L"agss-framework-tests-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
		DeleteFileW(path.c_str());
		SettingsStore store(path);
		const UserSettings defaults = store.load();
		check(defaults.visual.palette == PaletteId::Cyan && defaults.visual.spacing == BarSpacingMode::Compact, "missing settings use the documented appearance defaults");
		check(defaults.visual.panelExpanded && defaults.simulation.itemCount == 80 && defaults.simulation.durationSeconds == 10, "missing settings use the documented layout and run defaults");
		check(!defaults.simulation.descending && defaults.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion),
			"missing settings use ascending Insertion Sort");
		check(defaults.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::FixedDuration,
			"fixed-duration playback remains the default timing mode");
		check(defaults.version == 6 && defaults.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			defaults.simulation.familyId == "insertion-methods" && defaults.simulation.variantId == "insertion" &&
			defaults.simulation.profile == sortinglogic::InputProfile::NormalizedFloat &&
			defaults.simulation.shellGapSequence == sortinglogic::ShellGapSequence::Halving,
			"new settings use stable algorithm IDs and showcase defaults");
		check(defaults.audio.masterLevel == 0.5f && defaults.audio.bedLevel == 0.15f &&
			defaults.audio.bedHarmonicExponent == 1.5f && defaults.audio.bedComb.enabled && defaults.audio.bedComb.applyPhase,
			"missing settings use the selected preview audio profile");
		check(defaults.audio.attackMs == 8.0f && defaults.audio.releaseMs == 8.0f &&
			defaults.audio.tauBelowBins == 0.5f && defaults.audio.tauAboveBins == 0.5f &&
			defaults.audio.chorusMix == 0.0f, "audio default envelope and chorus profile matches the selected preview");
		check(defaults.reconstructedAudio.masterLevel == 1.0 &&
			std::fabs(defaults.reconstructedAudio.minimumFrequencyHz - 104.0322699094767) < 1e-12 &&
			std::fabs(defaults.reconstructedAudio.maximumFrequencyHz - 4054.6605862015895) < 1e-12 &&
			std::fabs(defaults.reconstructedAudio.secondOperandDelayMicroseconds - 225.66891905936825) < 1e-9,
			"version-6 defaults use the calibrated reconstructed burst profile");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			check(defaults.audio.bandTrimDb[i] == 0.0f, "every per-band trim defaults to zero");

		UserSettings saved;
		saved.visual.palette = PaletteId::Light;
		saved.visual.spacing = BarSpacingMode::Comfortable;
		saved.visual.panelExpanded = false;
		saved.simulation.itemCount = 800;
		saved.simulation.durationSeconds = 120;
		saved.simulation.playbackTimingMode = SoundEngine::PlaybackTimingMode::ReferenceCadence;
		saved.simulation.descending = true;
		saved.simulation.algorithm = static_cast<int>(sortinglogic::SortAlgorithm::Merge);
		saved.simulation.familyId = "merge-sort";
		saved.simulation.variantId = "merge";
		saved.simulation.profile = sortinglogic::InputProfile::IntegerKeys;
		saved.simulation.domain = sortinglogic::InputDomain::SignedIntegerKeys;
		saved.simulation.pattern = sortinglogic::InputPattern::NearlySorted;
		saved.simulation.shellGapSequence = sortinglogic::ShellGapSequence::Ciura;
		saved.simulation.mergeWritebackMode = sortinglogic::MergeWritebackMode::AnimatedWrites;
		saved.simulation.mergeExecutionMode = sortinglogic::MergeExecutionMode::TopDown;
		saved.algorithmOptions[algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence")] = "ciura";
		saved.algorithmOptions[algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode")] = "top_down";
		saved.algorithmOptions[algorithmOptionSettingsKey("merge-sort", "merge", "writeback_mode")] = "animated_writes";
		saved.audio.masterLevel = 0.73f;
		saved.audio.bedLevel = 0.42f;
		saved.audio.bedHarmonicExponent = 2.25f;
		saved.audio.includeSub300HzBed = true;
		saved.audio.bedComb.enabled = false;
		saved.audio.bedComb.delayMicroseconds = 350.0f;
		saved.audio.bedComb.applyPhase = false;
		saved.audio.bedComb.phaseOffsetDegrees = -42.0f;
		saved.audio.bedComb.wetAtLowFrequency = 0.45f;
		saved.audio.bedComb.wetAtHighFrequency = 0.34f;
		saved.audio.bedComb.fadeStartHz = 7300.0f;
		saved.audio.bedComb.fadeEndHz = 16000.0f;
		saved.audio.bedComb.fadeExponent = 2.4f;
		saved.audio.comparisonBoostDb = 8.25f;
		saved.audio.heldKeyBoostDb = 11.5f;
		saved.audio.tauBelowBins = 0.85f;
		saved.audio.tauAboveBins = 1.2f;
		saved.audio.chorusMix = 0.17f;
		saved.audio.chorusRateHz = 2.75f;
		saved.audio.chorusDepthMs = 4.1f;
		saved.audio.chorusWeighting = SoundEngine::ChorusWeighting::ComparisonEnvelope;
		saved.audio.attackMs = 13.0f;
		saved.audio.releaseMs = 40.0f;
		saved.audio.mappingMode = SoundEngine::MappingMode::EqualBandSlots;
		saved.audio.comparisonSpreadMode = SoundEngine::ComparisonSpreadMode::PositionScaled;
		saved.audio.enabled = false;
		saved.reconstructedAudio.minimumFrequencyHz = 120.0;
		saved.reconstructedAudio.maximumFrequencyHz = 3900.0;
		saved.reconstructedAudio.excitationClockHz = 350.0;
		saved.reconstructedAudio.carrierCycles = 7.25;
		saved.reconstructedAudio.tukeyFraction = 0.12;
		saved.reconstructedAudio.secondOperandDelayMicroseconds = 900.0;
		saved.reconstructedAudio.masterLevel = 0.73;
		saved.reconstructedAudio.specialReleaseMilliseconds = 800.0;
		saved.reconstructedAudio.lowPassCutoffHz = 8200.0;
		saved.reconstructedAudio.audioEnabled = false;
		saved.reconstructedAudio.lowPassEnabled = true;
		saved.reconstructedAudio.mapping = SoundEngine::ReconstructedMapping::LogFrequency;
		saved.reconstructedAudio.idlePolicy = SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
		saved.audio.accentRouting[static_cast<std::size_t>(SoundEngine::AccentCategory::Probe)] = false;
		saved.audio.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] = 0.23f;
		saved.audio.accentGain[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] = 0.31f;
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			saved.audio.bandTrimDb[i] = -30.0f + 0.5f * static_cast<float>(i);
		saved.audio.bandTrimDb[0] = -12.5f;
		saved.audio.bandTrimDb[59] = 4.0f;
		AlgorithmAudioLevels mergeFamilyLevels = defaultFamilyAudioLevels(saved.audio);
		mergeFamilyLevels.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = 9.5f;
		saved.familyAudioDefaults["merge-sort"] = mergeFamilyLevels;
		AlgorithmAudioLevels mergeOffsets;
		mergeOffsets.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] = -12.0f;
		mergeOffsets.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::AuxiliaryAccent)] = -6.0f;
		saved.variantAudioOffsets[variantAudioSettingsKey("merge-sort", "merge")] = mergeOffsets;
		std::string error;
		check(store.save(saved, &error), "save a valid settings file");
		SettingsStore restartedStore(path);
		const UserSettings loaded = restartedStore.load();
		check(loaded.visual.palette == saved.visual.palette && loaded.visual.spacing == saved.visual.spacing, "settings round-trip palette and spacing");
		check(loaded.visual.panelExpanded == saved.visual.panelExpanded && loaded.simulation.itemCount == saved.simulation.itemCount, "settings round-trip panel state and item count");
		check(loaded.simulation.durationSeconds == saved.simulation.durationSeconds && loaded.simulation.descending == saved.simulation.descending, "settings round-trip duration and direction");
		check(loaded.simulation.playbackTimingMode == saved.simulation.playbackTimingMode,
			"version-6 settings round-trip playback timing mode");
		check(loaded.simulation.algorithm == saved.simulation.algorithm, "settings round-trip selected algorithm");
		check(loaded.simulation.familyId == saved.simulation.familyId && loaded.simulation.variantId == saved.simulation.variantId,
			"settings round-trip stable family and variant IDs");
		check(loaded.simulation.profile == saved.simulation.profile && loaded.simulation.shellGapSequence == saved.simulation.shellGapSequence,
			"settings round-trip profile and Shell gap sequence");
		check(loaded.simulation.domain == saved.simulation.domain && loaded.simulation.pattern == saved.simulation.pattern,
			"settings round-trip signed domain and input pattern");
		check(loaded.simulation.mergeWritebackMode == saved.simulation.mergeWritebackMode,
			"settings round-trip configurable Merge Sort writeback mode");
		check(loaded.simulation.mergeExecutionMode == saved.simulation.mergeExecutionMode,
			"settings round-trip top-down and bottom-up Merge execution mode");
		check(loaded.algorithmOptions.find(algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence")) != loaded.algorithmOptions.end() &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("insertion-methods", "shell", "gap_sequence"))->second == "ciura" &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode"))->second == "top_down" &&
			loaded.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "writeback_mode"))->second == "animated_writes",
			"algorithm option values round-trip by stable family, variant, and option IDs");
		check(loaded.familyAudioDefaults.find("merge-sort") != loaded.familyAudioDefaults.end() &&
			loaded.familyAudioDefaults.find("merge-sort")->second.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 9.5f &&
			loaded.variantAudioOffsets.find(variantAudioSettingsKey("merge-sort", "merge")) != loaded.variantAudioOffsets.end() &&
			loaded.variantAudioOffsets.find(variantAudioSettingsKey("merge-sort", "merge"))->second.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == -12.0f,
			"settings round-trip family audio defaults and stable per-variant dB offsets");
		const sortinglogic::AlgorithmDescriptor* mergeDescriptor = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		const SoundEngine::Parameters effectiveMergeAudio = effectiveAudioParameters(loaded, *mergeDescriptor);
		check(std::fabs(effectiveMergeAudio.comparisonBoostDb - (-2.5f)) < 1e-5f,
			"variant comparison slider applies its dB offset relative to the family level");
		check(loaded.audio.masterLevel == saved.audio.masterLevel && loaded.audio.bedLevel == saved.audio.bedLevel &&
			loaded.audio.bedHarmonicExponent == saved.audio.bedHarmonicExponent &&
			loaded.audio.includeSub300HzBed == saved.audio.includeSub300HzBed,
			"settings round-trip master, bed, spectral rolloff, and low-bed toggle");
		check(loaded.audio.bedComb.enabled == saved.audio.bedComb.enabled && loaded.audio.bedComb.applyPhase == saved.audio.bedComb.applyPhase &&
			loaded.audio.bedComb.delayMicroseconds == saved.audio.bedComb.delayMicroseconds &&
			loaded.audio.bedComb.phaseOffsetDegrees == saved.audio.bedComb.phaseOffsetDegrees &&
			loaded.audio.bedComb.wetAtLowFrequency == saved.audio.bedComb.wetAtLowFrequency &&
			loaded.audio.bedComb.wetAtHighFrequency == saved.audio.bedComb.wetAtHighFrequency &&
			loaded.audio.bedComb.fadeStartHz == saved.audio.bedComb.fadeStartHz &&
			loaded.audio.bedComb.fadeEndHz == saved.audio.bedComb.fadeEndHz &&
			loaded.audio.bedComb.fadeExponent == saved.audio.bedComb.fadeExponent,
			"settings round-trip every comb-filter parameter");
		check(loaded.audio.comparisonBoostDb == saved.audio.comparisonBoostDb && loaded.audio.heldKeyBoostDb == saved.audio.heldKeyBoostDb &&
			loaded.audio.tauBelowBins == saved.audio.tauBelowBins && loaded.audio.tauAboveBins == saved.audio.tauAboveBins,
			"settings round-trip comparison gains and asymmetric envelope constants");
		check(loaded.audio.chorusMix == saved.audio.chorusMix && loaded.audio.chorusRateHz == saved.audio.chorusRateHz &&
			loaded.audio.chorusDepthMs == saved.audio.chorusDepthMs && loaded.audio.chorusWeighting == saved.audio.chorusWeighting &&
			loaded.audio.attackMs == saved.audio.attackMs && loaded.audio.releaseMs == saved.audio.releaseMs,
			"settings round-trip chorus and envelope controls");
		check(loaded.audio.mappingMode == saved.audio.mappingMode && loaded.audio.comparisonSpreadMode == saved.audio.comparisonSpreadMode &&
			loaded.audio.enabled == saved.audio.enabled, "settings round-trip mapping, spread, and synthesis enable");
		check(loaded.audio.accentRouting == saved.audio.accentRouting && loaded.audio.accentGain == saved.audio.accentGain,
			"settings round-trip semantic-event routing and gains");
		for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
			check(loaded.audio.bandTrimDb[i] == saved.audio.bandTrimDb[i], "settings round-trip every band trim");
		check(loaded.reconstructedAudio.minimumFrequencyHz == saved.reconstructedAudio.minimumFrequencyHz &&
			loaded.reconstructedAudio.maximumFrequencyHz == saved.reconstructedAudio.maximumFrequencyHz &&
			loaded.reconstructedAudio.excitationClockHz == saved.reconstructedAudio.excitationClockHz &&
			loaded.reconstructedAudio.carrierCycles == saved.reconstructedAudio.carrierCycles &&
			loaded.reconstructedAudio.tukeyFraction == saved.reconstructedAudio.tukeyFraction &&
			loaded.reconstructedAudio.secondOperandDelayMicroseconds == saved.reconstructedAudio.secondOperandDelayMicroseconds &&
			loaded.reconstructedAudio.masterLevel == saved.reconstructedAudio.masterLevel &&
			loaded.reconstructedAudio.specialReleaseMilliseconds == saved.reconstructedAudio.specialReleaseMilliseconds &&
			loaded.reconstructedAudio.lowPassCutoffHz == saved.reconstructedAudio.lowPassCutoffHz &&
			loaded.reconstructedAudio.audioEnabled == saved.reconstructedAudio.audioEnabled &&
			loaded.reconstructedAudio.lowPassEnabled == saved.reconstructedAudio.lowPassEnabled &&
			loaded.reconstructedAudio.mapping == saved.reconstructedAudio.mapping &&
			loaded.reconstructedAudio.idlePolicy == saved.reconstructedAudio.idlePolicy,
			"version-6 settings round-trip every reconstructed synthesis parameter");

		writeFile(path, "version=1\npalette=light\nspacing=none\npanel_expanded=0\nitem_count=123\nduration_seconds=42\ndescending=1\nalgorithm=0\n");
		const UserPreferences migrated = store.load();
		check(migrated.version == 6 && migrated.visual.palette == PaletteId::Light && migrated.visual.spacing == BarSpacingMode::None &&
			migrated.simulation.itemCount == 123 && migrated.simulation.durationSeconds == 42 && migrated.simulation.descending,
			"version-1 settings migrate without losing appearance or simulation values");
		check(migrated.simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::FixedDuration,
			"older settings default to fixed-duration playback");
		check(migrated.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			migrated.simulation.profile == sortinglogic::InputProfile::NormalizedFloat &&
			migrated.simulation.shellGapSequence == sortinglogic::ShellGapSequence::Halving,
			"version-1 migration establishes showcase defaults safely");
		check(migrated.audio.masterLevel == defaults.audio.masterLevel && migrated.audio.bedComb.enabled == defaults.audio.bedComb.enabled,
			"version-1 settings receive the selected profile audio defaults");
		writeFile(path, "version=2\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=0\naudio_master=0.61\n");
		const UserPreferences migratedV2 = store.load();
		check(migratedV2.version == 6 && migratedV2.visual.palette == PaletteId::Amber && migratedV2.audio.masterLevel == 0.61f &&
			migratedV2.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			migratedV2.simulation.profile == sortinglogic::InputProfile::NormalizedFloat,
			"version-2 settings retain existing values and migrate new fields");
		writeFile(path, "version=3\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=999999\nprofile=integer_keys\nshell_gap_sequence=tokuda\n");
		const UserPreferences unknownAlgorithm = store.load();
		check(unknownAlgorithm.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion) &&
			unknownAlgorithm.simulation.profile == sortinglogic::InputProfile::IntegerKeys &&
			unknownAlgorithm.simulation.shellGapSequence == sortinglogic::ShellGapSequence::Tokuda,
			"unknown saved algorithm IDs fall back while valid new settings survive");
		writeFile(path, "version=4\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=made-up\nvariant_id=missing\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserPreferences unknownStableId = store.load();
		check(unknownStableId.simulation.familyId == "insertion-methods" && unknownStableId.simulation.variantId == "insertion" &&
			unknownStableId.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Insertion),
			"unknown version-4 family and variant IDs fall back to the safe catalog default");
		writeFile(path, "version=4\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=shell\nvariant_id=standard\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserPreferences legacyV4Family = store.load();
		check(legacyV4Family.simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Shell) &&
			legacyV4Family.simulation.familyId == "insertion-methods" && legacyV4Family.simulation.variantId == "shell",
			"previous version-4 per-algorithm IDs migrate into the grouped family catalog");
		writeFile(path, "version=4\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\naudio_comparison_boost_db=10.5\naudio_held_key_boost_db=4.5\naudio_accent_gain_00=0.12\naudio_accent_gain_01=0.18\naudio_accent_gain_02=0.21\naudio_accent_gain_03=0.09\naudio_accent_route_01=0\n");
		const UserPreferences migratedV4Audio = store.load();
		const AlgorithmAudioLevels inheritedV4 = familyAudioLevels(migratedV4Audio, "insertion-methods");
		check(migratedV4Audio.simulation.familyId == "insertion-methods" && migratedV4Audio.simulation.variantId == "insertion" &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 0.0f &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::HeldKeyRole)] == 0.0f,
			"version-4 legacy gains migrate to neutral defaults for the reconstructed sound model");
		check(inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::ProbeAccent)] == 0.0f &&
			inheritedV4.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::MainChangeAccent)] == 0.0f,
			"version-4 accent settings do not leak into reconstructed comparison output");
		writeFile(path, "version=5\npalette=cyan\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nvariant_audio.insertion-methods.insertion.comparison=nan\n");
		const UserPreferences malformedVariantAudio = store.load();
		check(malformedVariantAudio.isValid() && variantAudioLevels(malformedVariantAudio, "insertion-methods", "insertion")
			.decibels[static_cast<std::size_t>(AlgorithmAudioChannel::Comparison)] == 0.0f,
			"malformed v5 variant audio values safely fall back to the inherited level");
		const std::wstring migrationBackup = path + L".pre-v6.bak";
		DeleteFileW(migrationBackup.c_str());
		UserPreferences migratingV5 = store.load();
		migratingV5.visual.palette = PaletteId::Light;
		check(store.save(migratingV5, &error) && GetFileAttributesW(migrationBackup.c_str()) != INVALID_FILE_ATTRIBUTES,
			"first successful version-6 save preserves the previous version-5 settings file");
		SettingsStore backupStore(migrationBackup);
		check(backupStore.load().visual.palette == PaletteId::Cyan && store.load().visual.palette == PaletteId::Light,
			"migration backup retains the old values while the active settings file upgrades");
		const UserPreferences roundTripVersion6 = store.load();
		check(store.save(roundTripVersion6, &error) && backupStore.load().visual.palette == PaletteId::Cyan,
			"later version-6 saves do not replace the one-time migration backup");
		writeFile(path, "version=5\npalette=amber\nspacing=compact\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=merge-sort\nvariant_id=merge\nprofile=normalized_float\nmerge_execution_mode=bottom_up\noption.merge-sort.merge.execution_mode=unknown_mode\n");
		const UserPreferences malformedOption = store.load();
		check(malformedOption.isValid() && malformedOption.simulation.mergeExecutionMode == sortinglogic::MergeExecutionMode::BottomUp &&
			malformedOption.algorithmOptions.find(algorithmOptionSettingsKey("merge-sort", "merge", "execution_mode")) == malformedOption.algorithmOptions.end(),
			"unknown v5 option IDs safely fall back to the catalog and typed default");

		writeFile(path, "version=garbage\npalette=banana\n");
		const UserSettings malformed = store.load();
		check(malformed.visual.palette == PaletteId::Cyan && malformed.simulation.itemCount == 80, "malformed settings safely fall back to defaults");
		writeFile(path, "version=3\npalette=light\nspacing=comfortable\npanel_expanded=1\nitem_count=80\nduration_seconds=10\ndescending=0\nalgorithm=1001\nprofile=normalized_float\nshell_gap_sequence=halving\naudio_master=nan\n");
		const UserPreferences malformedAudio = store.load();
		check(malformedAudio.visual.palette == PaletteId::Light && malformedAudio.audio.masterLevel == defaults.audio.masterLevel,
			"malformed audio values fall back to audio defaults without discarding valid app preferences");
		writeFile(path, "version=6\npalette=light\nspacing=comfortable\npanel_expanded=0\nitem_count=80\nduration_seconds=10\ndescending=0\nfamily_id=insertion-methods\nvariant_id=insertion\nprofile=normalized_float\nreconstructed_master=nan\nreconstructed_minimum_hz=9000\n");
		const UserPreferences malformedReconstructed = store.load();
		check(malformedReconstructed.visual.palette == PaletteId::Light &&
			malformedReconstructed.reconstructedAudio.masterLevel == defaults.reconstructedAudio.masterLevel &&
			malformedReconstructed.reconstructedAudio.minimumFrequencyHz == defaults.reconstructedAudio.minimumFrequencyHz,
			"malformed reconstructed audio settings fall back without discarding valid appearance settings");
		writeFile(path, "version=3\npalette=light\nspacing=comfortable\npanel_expanded=0\nitem_count=900\nduration_seconds=10\ndescending=1\nalgorithm=1001\nprofile=normalized_float\nshell_gap_sequence=halving\n");
		const UserSettings invalidRange = store.load();
		check(invalidRange.visual.palette == PaletteId::Cyan && invalidRange.visual.spacing == BarSpacingMode::Compact, "out-of-range settings safely fall back to defaults");
		DeleteFileW(path.c_str());
		check(store.load().simulation.itemCount == 80, "deleted settings return defaults");
	}

	void testAppControllerDispatchAndPersistence()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(persistence.writes == 0, "loading preferences does not write them back at startup");

		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		app.dispatch(AppAction(AppActionKind::PreviousEvent));
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.5f));
		check(persistence.writes == 0, "rejected transport and timeline actions never persist settings");

		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Cyan)));
		check(persistence.writes == 0, "reselecting the current palette does not write settings");
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		check(persistence.writes == 1 && persistence.value.visual.palette == PaletteId::Amber,
			"actual palette changes persist exactly once");
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		check(persistence.writes == 1, "repeating an unchanged preference does not write again");
		app.dispatch(AppAction(AppActionKind::SetSpacing, 0.0f, static_cast<int>(BarSpacingMode::None)));
		app.dispatch(AppAction(AppActionKind::TogglePanelExpanded));
		check(persistence.writes == 3 && !persistence.value.visual.panelExpanded,
			"spacing and panel state changes persist through the controller");
		const sortinglogic::CommandResult andrutArrangement = app.dispatch(AppAction(AppActionKind::SetInputPattern,
			0.0f, static_cast<int>(sortinglogic::InputPattern::AndrutReference)));
		check(andrutArrangement.accepted && app.snapshot().itemCount == 100 &&
			app.preferences().simulation.domain == sortinglogic::InputDomain::NormalizedFloats &&
			app.preferences().simulation.pattern == sortinglogic::InputPattern::AndrutReference,
			"selecting the Andrut arrangement immediately selects its 100-bar normalized input");
		check(persistence.writes == 4, "Andrut arrangement persists its item count and input profile as one preference change");
		app.dispatch(AppAction(AppActionKind::SetInputPattern, 0.0f, static_cast<int>(sortinglogic::InputPattern::Random)));

		app.dispatch(AppAction(AppActionKind::SetDirection, 0.0f, 1));
		app.dispatch(AppAction(AppActionKind::IncreaseCountFine));
		app.dispatch(AppAction(AppActionKind::IncreaseDuration));
		check(persistence.writes == 8 && persistence.value.simulation.itemCount == 101 &&
			persistence.value.simulation.durationSeconds == 11 && persistence.value.simulation.descending,
			"simulation configuration changes update the saved preference snapshot");
		app.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.63f, 0, AudioParameterId::MasterLevel));
		check(persistence.writes == 9 && persistence.value.audio.masterLevel == 0.63f,
			"audio preference changes persist immediately through the app controller");
		app.dispatch(AppAction(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::MasterLevel, 0.67f));
		check(persistence.writes == 10 && std::fabs(persistence.value.reconstructedAudio.masterLevel - 0.67) < 1e-6,
			"reconstructed audio preferences persist immediately through the app controller");
		app.dispatch(AppAction(AppActionKind::SelectAudioBand, 0.0f, 20));
		app.dispatch(AppAction(AppActionKind::RetryAudioDevice));
		check(persistence.writes == 10, "band selection and device retry do not write user settings");

		const sortinglogic::CommandResult run = app.dispatch(AppAction(AppActionKind::Run));
		check(run.accepted && persistence.writes == 10, "Run begins trace construction without an unnecessary settings write");
		const sortinglogic::CommandResult busy = app.dispatch(AppAction(AppActionKind::InstaInvert));
		check(!busy.accepted && persistence.writes == 10, "busy setup command is rejected without persistence");
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "controller receives and publishes the worker result on update");
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.5f));
		app.dispatch(AppAction(AppActionKind::NextEvent));
		app.dispatch(AppAction(AppActionKind::Reset));
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(persistence.writes == 10, "scrubbing, stepping, reset, and playback never write settings");
		app.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.71f, 0, AudioParameterId::BedLevel));
		check(app.snapshot().playing && persistence.writes == 11 && persistence.value.audio.bedLevel == 0.71f,
			"audio parameters remain adjustable and persist while trace playback is active");
		app.update(0.25f);
		const std::size_t progressedCursor = app.snapshot().cursor;
		check(app.snapshot().playing && progressedCursor > 0,
			"playback advances while live audio settings are changed");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(!app.snapshot().playing && app.snapshot().cursor == progressedCursor,
			"Pause stops playback without changing the current event cursor");
		app.dispatch(AppAction(AppActionKind::NextEvent));
		check(app.snapshot().cursor == progressedCursor + 1 && !app.snapshot().playing,
			"single-event stepping remains available while paused");
		app.dispatch(AppAction(AppActionKind::SeekFraction, 0.4f));
		const std::size_t seekCursor = static_cast<std::size_t>(0.4f * static_cast<double>(app.snapshot().eventCount));
		check(app.snapshot().cursor == seekCursor && !app.snapshot().playing,
			"timeline seeking reconstructs its destination while remaining paused");
		app.dispatch(AppAction(AppActionKind::Reset));
		check(app.snapshot().cursor == 0 && !app.snapshot().playing && persistence.writes == 11,
			"Reset restores the event cursor and array baseline without persisting transport state");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(app.snapshot().playing, "playback can restart after Reset");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
		check(!app.snapshot().playing && persistence.writes == 11,
			"pausing after restart does not persist transport state");

		wchar_t temporary[MAX_PATH] = {};
		const DWORD tempCount = GetTempPathW(MAX_PATH, temporary);
		check(tempCount > 0 && tempCount < MAX_PATH, "resolve temporary directory for app restart persistence");
		const std::wstring restartPath = std::wstring(temporary) + L"agss-app-restart-tests-" +
			std::to_wstring(GetCurrentProcessId()) + L".ini";
		DeleteFileW(restartPath.c_str());
		{
			SettingsStore firstStore(restartPath);
			AppController firstRun(firstStore);
			firstRun.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Light)));
			firstRun.dispatch(AppAction(AppActionKind::SetAudioParameter, 0.68f, 0, AudioParameterId::MasterLevel));
		}
		{
			SettingsStore restartedStore(restartPath);
			AppController restartedApp(restartedStore);
			check(restartedApp.preferences().visual.palette == PaletteId::Light &&
				restartedApp.preferences().audio.masterLevel == 0.68f,
				"controller preferences survive settings-file reload in a fresh app instance");
		}
		DeleteFileW(restartPath.c_str());
	}

	void testRestoreReconstructedDefaultsScope()
	{
		MemorySettings persistence;
		AppController app(persistence);
		app.dispatch(AppAction(AppActionKind::SetPalette, 0.0f, static_cast<int>(PaletteId::Amber)));
		app.dispatch(AppAction(AppActionKind::IncreaseCountFine));
		app.dispatch(AppAction(AppActionKind::SetDirection, 0.0f, 1));
		app.dispatch(AppAction(AppActionKind::SetReconstructedAudioParameter,
			ReconstructedAudioParameterId::MinimumFrequencyHz, 140.0f));
		app.dispatch(AppAction(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -12.0f,
			static_cast<int>(AlgorithmAudioChannel::Comparison)));
		const int writesBeforeRestore = persistence.writes;
		const sortinglogic::CommandResult restored = app.dispatch(AppAction(AppActionKind::RestoreReconstructedDefaults));
		const SoundEngine::ReconstructedParameters defaults;
		check(restored.accepted && persistence.writes == writesBeforeRestore + 1,
			"restoring reconstructed defaults persists the changed audio profile once");
		check(app.preferences().reconstructedAudio.minimumFrequencyHz == defaults.minimumFrequencyHz &&
			app.preferences().variantAudioOffsets.empty() && app.preferences().familyAudioDefaults.empty(),
			"restoring reconstructed defaults clears timbre edits and algorithm gain overrides");
		check(app.preferences().visual.palette == PaletteId::Amber && app.snapshot().itemCount == 81 &&
			app.snapshot().direction == sortinglogic::SortDirection::Descending,
			"restoring reconstructed defaults preserves appearance and simulation configuration");
	}

	void testAudioPresentationBridgeAndMailbox()
	{
		LatestAudioStateMailbox mailbox;
		AudioPresentationState latest;
		for (std::uint64_t revision = 1; revision <= 100; ++revision)
		{
			AudioPresentationState state;
			state.revision = revision;
			state.parameters.masterLevel = static_cast<float>(revision) / 100.0f;
			mailbox.publish(state);
		}
		check(mailbox.consumeLatest(latest) && latest.revision == 100 && latest.parameters.masterLevel == 1.0f,
			"latest-state mailbox coalesces pending updates to the newest complete snapshot");
		check(!mailbox.consumeLatest(latest), "mailbox does not replay stale snapshots after consumption");

		sortinglogic::Simulation simulation(5, 0x1234u);
		simulation.prepareInstaSort();
		sortinglogic::TraceBuildRequest request;
		check(simulation.beginTraceBuild(request).accepted, "test simulation starts a trace build");
		sortinglogic::TraceBuildResult built;
		built.succeeded = true;
		built.trace = sortinglogic::SortTraceBuilder::build(request);
		simulation.completeTraceBuild(std::move(built));
		AudioPresentationBridge bridge;
		LatestAudioStateMailbox traceMailbox;
		SoundEngine::Parameters parameters;
		sortinglogic::RenderSnapshot snapshot = simulation.snapshot();
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		waitForAudioTimeline(bridge, simulation, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"loaded trace publishes the continuous bed with no initial comparison focus");
		check(latest.timeline && latest.timeline->ready() && latest.timeline->sourceEventCount() == snapshot.eventCount &&
			latest.timeline->eligibleEventCount() == simulation.currentTrace()->comparisonEventIndices.size() &&
			latest.timeline->mode() == SoundEngine::PlaybackTimingMode::FixedDuration,
			"trace publication includes an immutable sound timeline with all source events and eligible comparisons");
		check(latest.transport.generation == latest.timelineGeneration && !latest.transport.playing &&
			!latest.transport.holdSelection,
			"initial audio transport installs paused at cursor zero without starting comparison births");
		const std::shared_ptr<const sortinglogic::SortTrace> trace = simulation.currentTrace();
		std::size_t firstCompareCursor = 0;
		for (std::size_t i = 0; i < trace->events.size(); ++i)
			if (trace->events[i].type == sortinglogic::SemanticEventType::Compare) { firstCompareCursor = i + 1; break; }
		check(firstCompareCursor > 0, "Insertion Sort trace contains comparisons for audio mapping");
		simulation.seekTo(firstCompareCursor);
		snapshot = simulation.snapshot();
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		waitForAudioTimeline(bridge, simulation, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second &&
			latest.comparison.secondRole == SoundEngine::ComparisonRole::HeldKey,
			"comparison event maps both items and preserves the held-key participant");
		check(latest.seekState && latest.seekState->valid &&
			latest.seekState->targetCursor == firstCompareCursor &&
			latest.seekState->timelineGeneration == latest.timelineGeneration,
			"paused cursor selection publishes a generation-tagged prepared seek snapshot for callback installation");
		SoundEngine::AudioTimelineEvent timelineComparison;
		check(latest.timeline->comparisonAtCursor(firstCompareCursor, timelineComparison) &&
			timelineComparison.sourceCursor == firstCompareCursor && latest.transport.holdSelection &&
			latest.transport.sourceCursor == firstCompareCursor,
			"paused event stepping keeps an exact cursor-selected comparison in the prepared audio timeline");
		check(latest.burstComparison.firstPosition == latest.comparison.firstPosition &&
			latest.burstComparison.secondPosition == latest.comparison.secondPosition &&
			latest.burstComparison.secondRole == static_cast<std::uint8_t>(SoundEngine::ComparisonRole::HeldKey),
			"the reconstructed audio bridge preserves normalized values and semantic roles");
		const sortinglogic::SemanticEvent& firstCompare = trace->events[firstCompareCursor - 1];
		float firstValue = 0.0f, secondValue = 0.0f, minimum = trace->input[0].value, maximum = minimum;
		for (std::size_t i = 0; i < trace->input.size(); ++i)
		{
			minimum = minimum < trace->input[i].value ? minimum : trace->input[i].value;
			maximum = maximum > trace->input[i].value ? maximum : trace->input[i].value;
			if (trace->input[i].stableId == firstCompare.firstItemId) firstValue = trace->input[i].value;
			if (trace->input[i].stableId == firstCompare.secondItemId) secondValue = trace->input[i].value;
		}
		check(std::fabs(latest.comparison.firstPosition - SoundEngine::normalizeValue(firstValue, minimum, maximum)) < 1e-6f &&
			std::fabs(latest.comparison.secondPosition - SoundEngine::normalizeValue(secondValue, minimum, maximum)) < 1e-6f,
			"comparison values normalize against the prepared input range");
		sortinglogic::Simulation gnome(8, 0x6A04u);
		check(gnome.selectAlgorithm(sortinglogic::SortAlgorithm::Gnome).accepted, "Gnome Sort selects for its reconstructed operand-order check");
		gnome.prepareInstaSort();
		sortinglogic::TraceBuildRequest gnomeRequest;
		check(gnome.beginTraceBuild(gnomeRequest).accepted, "Gnome trace starts for audio operand-order mapping");
		sortinglogic::TraceBuildResult gnomeBuilt;
		gnomeBuilt.succeeded = true;
		gnomeBuilt.trace = sortinglogic::SortTraceBuilder::build(gnomeRequest);
		gnome.completeTraceBuild(std::move(gnomeBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> gnomeTrace = gnome.currentTrace();
		std::size_t gnomeCompareCursor = 0;
		while (gnomeCompareCursor < gnomeTrace->events.size() &&
			gnomeTrace->events[gnomeCompareCursor].type != sortinglogic::SemanticEventType::Compare) ++gnomeCompareCursor;
		if (gnomeCompareCursor < gnomeTrace->events.size())
		{
			gnome.seekTo(gnomeCompareCursor + 1);
			AudioPresentationBridge gnomeBridge;
			LatestAudioStateMailbox gnomeMailbox;
			gnomeBridge.update(gnome, gnome.snapshot(), parameters, gnomeMailbox);
			waitForAudioTimeline(gnomeBridge, gnome, parameters, gnomeMailbox);
			AudioPresentationState gnomeState;
			const bool gnomePublished = gnomeMailbox.consumeLatest(gnomeState);
			check(gnomePublished &&
				gnomeState.burstComparison.firstPosition == gnomeState.comparison.secondPosition &&
				gnomeState.burstComparison.secondPosition == gnomeState.comparison.firstPosition,
				"Gnome current audio operands reverse according to catalog metadata");
			const bool gnomeTimelineLookup = gnomeState.timeline &&
				gnomeState.timeline->comparisonAtCursor(gnomeCompareCursor + 1, timelineComparison);
			check(gnomeTimelineLookup && timelineComparison.sourceCursor == gnomeCompareCursor + 1,
				"Gnome prepared timeline retains the eligible comparison at its source cursor");
			check(gnomeTimelineLookup &&
				std::fabs(timelineComparison.firstPosition - gnomeState.burstComparison.firstPosition) < 1.0e-6 &&
				std::fabs(timelineComparison.secondPosition - gnomeState.burstComparison.secondPosition) < 1.0e-6,
				"Gnome timeline operands use the catalog's audio order");
		}
		SoundEngine::ReconstructedParameters releasePreferences;
		releasePreferences.idlePolicy = SoundEngine::ReconstructedIdlePolicy::ReleaseSelection;
		bridge.update(simulation, snapshot, parameters, traceMailbox, releasePreferences);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison && latest.releaseSelection && !latest.transportPlaying,
			"paused inspection publishes the configured special-release policy to the audio callback");
		const SoundEngine::PreparedAudioTimeline* stableTimeline = latest.timeline;
		parameters.heldKeyBoostDb = 0.0f;
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 0.0f &&
			latest.timeline == stableTimeline && std::fabs(latest.timelineGains.heldKeyGain - 1.0) < 1.0e-9 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio bridge applies live held-key gain changes without rebuilding the immutable timeline");
		parameters.heldKeyBoostDb = 18.0f;
		bridge.update(simulation, snapshot, parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.parameters.heldKeyBoostDb == 18.0f &&
			std::fabs(latest.timelineGains.heldKeyGain - std::pow(10.0, 18.0 / 20.0)) < 1.0e-6 &&
			latest.comparison.heldKey == SoundEngine::HeldKeyParticipant::Second,
			"audio callback mailbox receives the full held-key offset range for a held comparison");

		const std::size_t laterCursor = trace->events.size() / 2;
		simulation.seekTo(laterCursor);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] > 0,
			"coalesced frame summary includes movement events crossed during playback");
		simulation.seekTo(firstCompareCursor);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.hasComparison &&
			latest.eventAccents.counts[static_cast<std::size_t>(SoundEngine::AccentCategory::Movement)] == 0 &&
			std::fabs(latest.comparison.firstPosition - SoundEngine::normalizeValue(firstValue, minimum, maximum)) < 1e-6f,
			"rewinding restores the comparison selected by the destination cursor");
		simulation.seekTo(trace->events.size());
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"completed trace retains the bed and clears comparison focus");
		simulation.reset();
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"reset retains the loaded bed and clears the comparison");

		sortinglogic::Simulation counting(32, 0xC017u, sortinglogic::InputProfile::IntegerKeys);
		check(counting.selectAlgorithm(sortinglogic::SortAlgorithm::Counting).accepted,
			"Counting Sort accepts an integer-key test input for pitch normalization");
		sortinglogic::TraceBuildRequest countingRequest;
		check(counting.beginTraceBuild(countingRequest).accepted, "Counting trace starts for workspace pitch mapping");
		sortinglogic::TraceBuildResult countingBuilt;
		countingBuilt.succeeded = true;
		countingBuilt.trace = sortinglogic::SortTraceBuilder::build(countingRequest);
		counting.completeTraceBuild(std::move(countingBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> countingTrace = counting.currentTrace();
		std::size_t firstCountingProbe = countingTrace->events.size();
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::Probe) { firstCountingProbe = i; break; }
		if (firstCountingProbe < countingTrace->events.size() && countingTrace->comparisonEventIndices.empty())
		{
			counting.seekTo(firstCountingProbe + 1);
			AudioPresentationBridge probeBridge;
			LatestAudioStateMailbox probeMailbox;
			probeBridge.update(counting, counting.snapshot(), parameters, probeMailbox);
			AudioPresentationState probeState;
			check(probeMailbox.consumeLatest(probeState) && probeState.hasComparison && probeState.burstComparison.probeOnly &&
				probeState.burstComparison.firstRole == static_cast<std::uint8_t>(SoundEngine::ComparisonRole::ProbedItem),
				"comparison-free distribution traces sonify real item probes as single-participant notes");
		}
		std::size_t counterWorkspaceSize = 0;
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::ResizeCounterWorkspace)
				counterWorkspaceSize = countingTrace->events[i].afterSize;
		std::size_t upperBucketWrite = countingTrace->events.size();
		for (std::size_t i = 0; i < countingTrace->events.size(); ++i)
			if (countingTrace->events[i].type == sortinglogic::SemanticEventType::CounterWrite && counterWorkspaceSize > 1 &&
				countingTrace->events[i].firstIndex == counterWorkspaceSize - 1)
			{ upperBucketWrite = i; break; }
		check(counterWorkspaceSize > 1 && upperBucketWrite < countingTrace->events.size(),
			"Counting trace exposes a write at the end of its counter workspace");
		if (upperBucketWrite < countingTrace->events.size())
		{
			counting.seekTo(upperBucketWrite + 1);
			AudioPresentationBridge countingBridge;
			LatestAudioStateMailbox countingMailbox;
			countingBridge.update(counting, counting.snapshot(), parameters, countingMailbox);
			check(countingMailbox.consumeLatest(latest) &&
				std::fabs(latest.eventAccents.representativePositions[static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution)] - 1.0f) < 1e-6f,
				"counter accents normalize against the counter workspace range rather than the primary array");
		}
		simulation.prepareShuffle(0x4321u);
		bridge.update(simulation, simulation.snapshot(), parameters, traceMailbox);
		check(traceMailbox.consumeLatest(latest) && !latest.traceLoaded && !latest.hasComparison,
			"preparing a replacement input fades the loaded trace bed out");

		sortinglogic::Simulation dense(800, 0x87654321u);
		check(dense.setDuration(1.0f).accepted, "800-item integration playback uses the minimum run duration");
		dense.prepareInstaInvert();
		sortinglogic::TraceBuildRequest denseRequest;
		check(dense.beginTraceBuild(denseRequest).accepted, "800-item reverse trace starts for audio-path stress coverage");
		sortinglogic::TraceBuildResult denseBuilt;
		denseBuilt.succeeded = true;
		denseBuilt.trace = sortinglogic::SortTraceBuilder::build(denseRequest);
		dense.completeTraceBuild(std::move(denseBuilt));
		const std::shared_ptr<const sortinglogic::SortTrace> denseTrace = dense.currentTrace();
		check(denseTrace && denseTrace->events.size() > 319600, "800-item reverse trace contains dense ordered comparison events");
		AudioPresentationBridge denseBridge;
		LatestAudioStateMailbox denseMailbox;
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		denseMailbox.consumeLatest(latest);
		const std::size_t middleCursor = denseTrace->events.size() / 2;
		dense.seekTo(middleCursor);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"dense 800-item seek publishes the current comparison without waiting for skipped events");
		const std::uint64_t middleRevision = latest.revision;
		dense.seekTo(denseTrace->events.size() * 3 / 4);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.revision > middleRevision && latest.hasComparison,
			"dense forward playback replaces the prior mailbox state with the latest comparison");
		dense.seekTo(denseTrace->events.size());
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(denseMailbox.consumeLatest(latest) && latest.traceLoaded && !latest.hasComparison,
			"dense completion leaves only the bed in the audio state");
		dense.reset();
		dense.togglePlayback();
		dense.advance(0.5f);
		const sortinglogic::RenderSnapshot halfway = dense.snapshot();
		denseBridge.update(dense, halfway, parameters, denseMailbox);
		check(halfway.playing && halfway.cursor == denseTrace->events.size() / 2 &&
			denseMailbox.consumeLatest(latest) && latest.traceLoaded && latest.hasComparison,
			"800-item playback reaches the halfway comparison without an audio-event backlog");
		dense.advance(0.5f);
		denseBridge.update(dense, dense.snapshot(), parameters, denseMailbox);
		check(dense.snapshot().complete && denseMailbox.consumeLatest(latest) &&
			latest.traceLoaded && !latest.hasComparison,
			"800-item playback completes on schedule and leaves the harmonic bed running");
	}

	void testAccentAudioActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const std::size_t distribution = static_cast<std::size_t>(SoundEngine::AccentCategory::Distribution);
		const sortinglogic::CommandResult changed = app.dispatch(AppAction(AppActionKind::SetAudioParameter,
			0.27f, 0, AudioParameterId::AccentGain, static_cast<int>(distribution)));
		check(changed.accepted && persistence.writes == 1 &&
			std::fabs(persistence.value.audio.accentGain[distribution] - 0.27f) < 1e-6f,
			"indexed semantic-event gain action updates and persists its category");
		const sortinglogic::CommandResult muted = app.dispatch(AppAction(AppActionKind::SetAudioParameter,
			0.0f, 0, AudioParameterId::AccentRouting, static_cast<int>(distribution)));
		check(muted.accepted && persistence.writes == 2 && !persistence.value.audio.accentRouting[distribution],
			"indexed semantic-event routing action persists the category toggle");
	}

	void testVariantAudioLevels()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const SoundEngine::Parameters inherited = app.effectiveAudio();
		check(std::fabs(inherited.comparisonBoostDb) < 1e-6f,
			"a new variant inherits the reconstructed model's neutral family comparison level");
		const AppAction offset(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -12.0f,
			static_cast<int>(AlgorithmAudioChannel::Comparison));
		check(app.dispatch(offset).accepted && persistence.writes == 1 &&
			std::fabs(app.effectiveAudio().comparisonBoostDb - (inherited.comparisonBoostDb - 12.0f)) < 1e-5f,
			"variant operation levels apply a live dB offset and persist by stable IDs");
		const AppAction muteHeld(AppActionKind::SetVariantAudioLevel, "insertion-methods", "insertion", -60.0f,
			static_cast<int>(AlgorithmAudioChannel::HeldKeyRole));
		check(app.dispatch(muteHeld).accepted && app.effectiveAudio().heldKeyBoostDb <= -53.9f,
			"the -60 dB variant offset effectively mutes held-key emphasis while preserving inheritance");
		check(app.dispatch(AppAction(AppActionKind::ResetVariantAudioLevels, "insertion-methods", "insertion")).accepted &&
			persistence.writes == 3 && std::fabs(app.effectiveAudio().comparisonBoostDb - inherited.comparisonBoostDb) < 1e-6f,
			"reset removes variant overrides and restores family defaults");
	}

	void testShowcaseActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const std::uint32_t seed = app.snapshot().inputSeed;
		check(app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::IntegerKeys))).accepted,
			"integer profile can be selected from the app action layer");
		check(persistence.writes == 1 && app.snapshot().inputSeed == seed &&
			app.snapshot().profile == sortinglogic::InputProfile::IntegerKeys,
			"profile change persists and regenerates from the existing seed");
		sortinglogic::Simulation expected(80, seed, sortinglogic::InputProfile::IntegerKeys);
		bool generatedMatches = expected.preparedInput().size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; generatedMatches && i < expected.preparedInput().size(); ++i)
			generatedMatches = expected.preparedInput()[i].stableId == app.simulation().preparedInput()[i].stableId &&
				expected.preparedInput()[i].integerKey == app.simulation().preparedInput()[i].integerKey;
		check(generatedMatches, "profile generation matches a fresh simulation using the saved seed");

		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "distribution-sorts", "counting")).accepted,
			"Counting Sort is selectable for integer keys");
		check(app.preferences().simulation.algorithm == static_cast<int>(sortinglogic::SortAlgorithm::Counting) &&
			app.preferences().simulation.familyId == "distribution-sorts" && app.preferences().simulation.variantId == "counting",
			"selected algorithm persists with stable family and variant IDs");
		check(app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::NormalizedFloat))).accepted,
			"profile can change while setup is idle");
		check(app.snapshot().algorithm == sortinglogic::SortAlgorithm::Counting && !app.snapshot().runCompatible,
			"incompatible selection is retained after a profile change");
		const sortinglogic::CommandResult rejectedRun = app.dispatch(AppAction(AppActionKind::Run));
		check(!rejectedRun.accepted && rejectedRun.message.find("Normalized floats") != std::string::npos,
			"incompatible Run command explains the required profile");
		sortinglogic::Simulation expectedFloat(80, seed, sortinglogic::InputProfile::NormalizedFloat);
		bool sameFloatInput = expectedFloat.preparedInput().size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; sameFloatInput && i < expectedFloat.preparedInput().size(); ++i)
			sameFloatInput = expectedFloat.preparedInput()[i].stableId == app.simulation().preparedInput()[i].stableId &&
				expectedFloat.preparedInput()[i].value == app.simulation().preparedInput()[i].value &&
				!app.simulation().preparedInput()[i].hasIntegerKey;
		check(sameFloatInput, "profile regeneration is deterministic for the retained seed");
		const std::vector<sortinglogic::SortItem> floatInput = app.simulation().preparedInput();
		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "merge-sort", "merge")).accepted,
			"a compatible comparison algorithm can replace an incompatible selection");
		bool preservedAlgorithmInput = floatInput.size() == app.simulation().preparedInput().size();
		for (std::size_t i = 0; preservedAlgorithmInput && i < floatInput.size(); ++i)
			preservedAlgorithmInput = floatInput[i].stableId == app.simulation().preparedInput()[i].stableId &&
				floatInput[i].value == app.simulation().preparedInput()[i].value;
		check(preservedAlgorithmInput, "changing algorithms keeps the prepared data untouched");
		check(app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "insertion-methods", "shell")).accepted,
			"Shell Sort can be selected in the normalized-float profile");
		check(!app.dispatch(AppAction(AppActionKind::SelectAlgorithm, "unregistered", "missing")).accepted,
			"unregistered family and variant IDs are rejected safely");
		check(app.dispatch(AppAction(AppActionKind::SetShellGapSequence, 0.0f,
			static_cast<int>(sortinglogic::ShellGapSequence::Ciura))).accepted &&
			app.preferences().simulation.shellGapSequence == sortinglogic::ShellGapSequence::Ciura,
			"Shell sequence selection dispatches and persists");
		const sortinglogic::AlgorithmDescriptor* shell = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Shell);
		check(shell && app.dispatch(AppAction(AppActionKind::SetAlgorithmOption, shell->familyId, shell->variantId,
			"gap_sequence", "tokuda", static_cast<int>(sortinglogic::ShellGapSequence::Tokuda))).accepted &&
			app.preferences().simulation.shellGapSequence == sortinglogic::ShellGapSequence::Tokuda &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(shell->familyId, shell->variantId, "gap_sequence"))->second == "tokuda",
			"catalog option dispatch applies and persists a stable Shell gap option ID");
		const sortinglogic::CommandResult run = app.dispatch(AppAction(AppActionKind::Run));
		check(run.accepted, "compatible Shell Sort run begins");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "Shell trace completes through the existing single-job service");
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted, "showcase trace starts playback");
		const int writesBeforeBusyProfile = persistence.writes;
		check(!app.dispatch(AppAction(AppActionKind::SetInputProfile, 0.0f,
			static_cast<int>(sortinglogic::InputProfile::IntegerKeys))).accepted && persistence.writes == writesBeforeBusyProfile,
			"profile changes are blocked during playback and do not persist");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
	}

	void testMetadataAlgorithmOptions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const sortinglogic::AlgorithmDescriptor* merge = sortinglogic::algorithmDescriptor(sortinglogic::SortAlgorithm::Merge);
		check(merge && app.dispatch(AppAction(AppActionKind::SelectAlgorithm, merge->familyId, merge->variantId)).accepted,
			"Merge can be selected before applying catalog options");
		const int writesBeforeUnchanged = persistence.writes;
		const AppAction unchangedBottomUp(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "bottom_up", static_cast<int>(sortinglogic::MergeExecutionMode::BottomUp));
		check(app.dispatch(unchangedBottomUp).accepted && persistence.writes == writesBeforeUnchanged,
			"choosing the current catalog option does not cause an unnecessary settings write");
		const int topDownValue = static_cast<int>(sortinglogic::MergeExecutionMode::TopDown);
		const AppAction topDown(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "top_down", topDownValue);
		check(app.dispatch(topDown).accepted && app.snapshot().mergeExecutionMode == sortinglogic::MergeExecutionMode::TopDown &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(merge->familyId, merge->variantId, "execution_mode"))->second == "top_down",
			"Merge execution order dispatches and persists through stable option metadata");
		const int animatedValue = static_cast<int>(sortinglogic::MergeWritebackMode::AnimatedWrites);
		const AppAction animated(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"writeback_mode", "animated_writes", animatedValue);
		check(app.dispatch(animated).accepted && app.snapshot().mergeWritebackMode == sortinglogic::MergeWritebackMode::AnimatedWrites &&
			app.preferences().algorithmOptions.find(algorithmOptionSettingsKey(merge->familyId, merge->variantId, "writeback_mode"))->second == "animated_writes",
			"Merge writeback remains independent and uses its own stable option group");
		const int writesBeforeInvalid = persistence.writes;
		const AppAction invalid(AppActionKind::SetAlgorithmOption, merge->familyId, merge->variantId,
			"execution_mode", "missing_option", topDownValue);
		check(!app.dispatch(invalid).accepted && persistence.writes == writesBeforeInvalid,
			"unknown stable option IDs are rejected without persisting a change");
	}

	void testPlaybackTimingActions()
	{
		MemorySettings persistence;
		AppController app(persistence);
		const float originalDuration = app.snapshot().durationSeconds;
		const AppAction reference(AppActionKind::SetPlaybackTimingMode, 0.0f,
			static_cast<int>(SoundEngine::PlaybackTimingMode::ReferenceCadence));
		check(app.dispatch(reference).accepted &&
			app.preferences().simulation.playbackTimingMode == SoundEngine::PlaybackTimingMode::ReferenceCadence &&
			persistence.writes == 1,
			"reference cadence is a persisted simulation preference");
		check(!app.dispatch(AppAction(AppActionKind::IncreaseDuration)).accepted &&
			app.snapshot().durationSeconds == originalDuration && persistence.writes == 1,
			"manual duration changes are rejected without a settings write in reference cadence");
		check(!app.dispatch(AppAction(AppActionKind::SetPlaybackTimingMode, 0.0f, 99)).accepted &&
			persistence.writes == 1,
			"invalid timing modes are rejected without persistence");
	}

	void testAudioPreparationPlaybackGate()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.dispatch(AppAction(AppActionKind::Run)).accepted, "run creates a trace for the audio-preparation gate");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "audio-preparation gate fixture receives its trace");
		app.setAudioPreparationPending(true);
		check(!app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && !app.snapshot().playing,
			"Play is rejected with an actionable status while its immutable audio timeline is preparing");
		app.setAudioPreparationPending(false);
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && app.snapshot().playing,
			"Play becomes available as soon as timeline preparation completes");
		app.dispatch(AppAction(AppActionKind::TogglePlayback));
	}

	void testStaleAudioPlayheadCannotCancelPlayback()
	{
		MemorySettings persistence;
		AppController app(persistence);
		check(app.dispatch(AppAction(AppActionKind::Run)).accepted,
			"playhead revision regression creates a trace");
		const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (app.snapshot().buildingTrace && std::chrono::steady_clock::now() < deadline)
		{
			app.update(0.0f);
			std::this_thread::yield();
		}
		check(app.snapshot().hasTrace, "playhead revision regression receives its trace");
		app.setAudioPreparationPending(false);
		check(app.dispatch(AppAction(AppActionKind::TogglePlayback)).accepted && app.snapshot().playing,
			"playhead revision regression starts playback");

		SoundEngine::AudioPlayheadFeedback stalePause;
		stalePause.generation = 7;
		stalePause.transportRevision = 10;
		stalePause.durationFrames = 44100 * 10;
		stalePause.playing = false;
		app.applyAudioPlayhead(stalePause, 7, 11);
		check(app.snapshot().playing,
			"an older paused feedback snapshot cannot cancel a newer Play transport revision");

		SoundEngine::AudioPlayheadFeedback currentPlayhead;
		currentPlayhead.generation = 7;
		currentPlayhead.transportRevision = 11;
		currentPlayhead.durationFrames = 44100 * 10;
		currentPlayhead.timelineFrame = 4410;
		currentPlayhead.sourceCursor = 12;
		currentPlayhead.playing = true;
		app.applyAudioPlayhead(currentPlayhead, 7, 11);
		check(app.snapshot().playing && app.snapshot().cursor == 12,
			"feedback for the current transport revision advances the visible playhead");

		currentPlayhead.timelineFrame = currentPlayhead.durationFrames;
		currentPlayhead.sourceCursor = app.snapshot().eventCount;
		currentPlayhead.playing = false;
		app.applyAudioPlayhead(currentPlayhead, 7, 11);
		check(!app.snapshot().playing,
			"completion feedback for the current revision still pauses the simulation");
	}
}

int main()
{
	testSpacing();
	testBarCountSteps();
	testRenderPipelineContracts();
	testLayoutAndWidgets();
	testSettingsStore();
	testAppControllerDispatchAndPersistence();
	testRestoreReconstructedDefaultsScope();
	testShowcaseActions();
	testMetadataAlgorithmOptions();
	testPlaybackTimingActions();
	testAudioPreparationPlaybackGate();
	testStaleAudioPlayheadCannotCancelPlayback();
	testAudioPresentationBridgeAndMailbox();
	testAccentAudioActions();
	testVariantAudioLevels();
	if (failures)
	{
		std::cerr << failures << " app framework checks failed.\n";
		return 1;
	}
	std::cout << "All app framework checks passed.\n";
	return 0;
}
