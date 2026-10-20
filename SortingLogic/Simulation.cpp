#include "SortingLogic.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sortinglogic
{
	std::uint32_t Simulation::createFreshSeed()
	{
		std::random_device source;
		const std::uint32_t a = static_cast<std::uint32_t>(source()), b = static_cast<std::uint32_t>(source());
		return (a << 16) ^ b ^ static_cast<std::uint32_t>(source());
	}

	Simulation::Simulation(std::size_t initialItemCount, std::uint32_t startupSeed, InputProfile profile)
		: preparedInput_(), currentState_(), trace_(), itemCount_(initialItemCount), cursor_(0), inputSeed_(startupSeed),
		durationSeconds_(10.0f), elapsedSeconds_(0.0f), direction_(SortDirection::Ascending), algorithm_(SortAlgorithm::Insertion),
		profile_(profile), domain_(profile == InputProfile::NormalizedFloat ? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys),
		pattern_(InputPattern::Random), shellGapSequence_(ShellGapSequence::Halving),
		mergeWritebackMode_(MergeWritebackMode::AtomicRangeWrite), mergeExecutionMode_(MergeExecutionMode::BottomUp),
		mergeBufferVisualizationEnabled_(true),
		completionScanEnabled_(false),
		baselineKind_(BaselineKind::Shuffle),
		buildingTrace_(false), playing_(false), status_()
	{
		if (itemCount_ < MinItemCount || itemCount_ > MaxItemCount)
			throw std::invalid_argument("Initial item count must be between " + std::to_string(MinItemCount) +
				" and " + std::to_string(MaxItemCount) + ".");
		prepareItems(BaselineKind::Shuffle, startupSeed);
		status_ = "Ready. Input shuffle seed " + std::to_string(inputSeed_) + ".";
	}

	Simulation::Simulation(std::size_t initialItemCount, std::uint32_t startupSeed, InputDomain domain, InputPattern pattern)
		: Simulation(initialItemCount, startupSeed, inputProfileForDomain(domain))
	{
		if (!inputPatternCompatible(domain, pattern)) throw std::invalid_argument("The selected input pattern is incompatible with this data domain.");
		domain_ = domain;
		profile_ = inputProfileForDomain(domain);
		pattern_ = pattern;
		if (pattern_ == InputPattern::AndrutReference) itemCount_ = 100;
		prepareItems(BaselineKind::Shuffle, startupSeed);
	}

	Simulation::~Simulation() = default;

	CommandResult Simulation::checkPreparationAllowed() const
	{
		if (buildingTrace_) return CommandResult(false, "Cannot prepare input or start a run while the trace is being built.");
		if (playing_) return CommandResult(false, "Pause or reset playback before changing the input or run settings.");
		return CommandResult(true, "Ready.");
	}

	void Simulation::prepareItems(BaselineKind kind, std::uint32_t seed)
	{
		baselineKind_ = kind;
		inputSeed_ = kind == BaselineKind::Shuffle ? seed : 0;
		std::vector<SortItem> items;
		items.reserve(itemCount_);
		std::mt19937 engine(seed);
		static const int andrutRanks[100] = {
			78,89,75,81,31,39,93,6,86,27,56,20,42,98,96,95,60,62,77,15,
			2,49,37,46,63,64,82,45,32,12,28,76,94,3,57,59,30,50,90,72,
			38,70,36,53,34,44,40,43,4,84,29,7,25,33,8,41,66,14,67,48,
			35,61,79,88,85,0,92,87,97,19,18,26,58,1,91,10,23,54,55,69,
			73,22,24,5,13,74,71,99,9,16,65,83,51,80,11,52,68,47,21,17
		};
		const bool andrutFixture = kind == BaselineKind::Shuffle && pattern_ == InputPattern::AndrutReference;
		const auto assignKey = [this](SortItem& item, int key)
		{
			item.integerKey = key;
			item.hasIntegerKey = true;
			const float denominator = static_cast<float>(std::max<std::size_t>(1, itemCount_));
			item.value = domain_ == InputDomain::SignedIntegerKeys
				? static_cast<float>(key + static_cast<int>(itemCount_)) / (2.0f * denominator)
				: static_cast<float>(key + 1) / denominator;
		};
		for (std::size_t i = 0; i < itemCount_; ++i)
		{
			const float normalized = static_cast<float>(i + 1) / static_cast<float>(itemCount_);
			if (andrutFixture)
				items.push_back(SortItem(static_cast<std::uint64_t>(i), static_cast<float>(andrutRanks[i]) / 99.0f));
			else if (domain_ == InputDomain::NormalizedFloats)
				items.push_back(SortItem(static_cast<std::uint64_t>(i), normalized));
			else
			{
				int key = static_cast<int>(i);
				if (domain_ == InputDomain::NonnegativeIntegerKeys)
				{
					std::uniform_int_distribution<int> choose(0, static_cast<int>(itemCount_ - 1));
					key = choose(engine);
				}
				else if (domain_ == InputDomain::SignedIntegerKeys)
				{
					const int extent = static_cast<int>(itemCount_ - 1);
					std::uniform_int_distribution<int> choose(-extent, extent);
					key = choose(engine);
				}
				SortItem item(static_cast<std::uint64_t>(i), key, normalized);
				assignKey(item, key);
				items.push_back(item);
			}
		}
		const auto sortByDirection = [&items](SortDirection sortDirection)
		{
			std::stable_sort(items.begin(), items.end(), [sortDirection](const SortItem& a, const SortItem& b)
				{ return compareItems(a, b, sortDirection) == ComparisonResult::Less; });
		};
		if (kind == BaselineKind::Shuffle)
		{
			switch (pattern_)
			{
			case InputPattern::Random:
				std::shuffle(items.begin(), items.end(), engine);
				break;
			case InputPattern::Sorted:
				sortByDirection(SortDirection::Ascending);
				break;
			case InputPattern::Reversed:
				sortByDirection(SortDirection::Descending);
				break;
			case InputPattern::NearlySorted:
				sortByDirection(SortDirection::Ascending);
				if (items.size() > 1)
				{
					const std::size_t swaps = std::max<std::size_t>(1, items.size() / 20);
					std::uniform_int_distribution<std::size_t> choose(0, items.size() - 1);
					for (std::size_t i = 0; i < swaps; ++i) std::swap(items[choose(engine)], items[choose(engine)]);
				}
				break;
			case InputPattern::FewUnique:
			{
				const int uniqueCount = static_cast<int>(std::max<std::size_t>(1, std::min<std::size_t>(5, itemCount_)));
				std::uniform_int_distribution<int> choose(0, uniqueCount - 1);
				for (std::size_t i = 0; i < items.size(); ++i)
				{
					const int selected = choose(engine);
					if (domain_ == InputDomain::NormalizedFloats)
						items[i].value = static_cast<float>(selected + 1) / static_cast<float>(uniqueCount);
					else if (domain_ == InputDomain::SignedIntegerKeys)
						assignKey(items[i], selected - (uniqueCount - 1) / 2);
					else assignKey(items[i], selected);
				}
				std::shuffle(items.begin(), items.end(), engine);
				break;
			}
			case InputPattern::OrganPipe:
				for (std::size_t i = 0; i < items.size(); ++i)
				{
					const std::size_t rank = i <= items.size() / 2 ? i : items.size() - 1 - i;
					if (domain_ == InputDomain::NormalizedFloats)
						items[i].value = static_cast<float>(rank + 1) / static_cast<float>(itemCount_);
					else if (domain_ == InputDomain::SignedIntegerKeys)
						assignKey(items[i], static_cast<int>(rank) - static_cast<int>(itemCount_ / 2));
					else assignKey(items[i], static_cast<int>(rank));
				}
				break;
			case InputPattern::AndrutReference:
				// Installed from the fixture above; retain its exact left-to-right order.
				break;
			}
		}
		else sortByDirection(kind == BaselineKind::Descending ? SortDirection::Descending : SortDirection::Ascending);
		preparedInput_.swap(items);
		replaceTraceWithBaseline();
	}

	void Simulation::replaceTraceWithBaseline()
	{
		trace_.reset(); currentState_ = makeInitialState(preparedInput_); cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
	}

	CommandResult Simulation::prepareShuffle(std::uint32_t seed)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Shuffle, seed); status_ = "Prepared shuffled input with seed " + std::to_string(inputSeed_) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::prepareInstaSort()
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Ascending, 0); status_ = "Prepared the ascending baseline. Reset returns to this input."; return CommandResult(true, status_);
	}
	CommandResult Simulation::prepareInstaInvert()
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		prepareItems(BaselineKind::Descending, 0); status_ = "Prepared the reverse-sorted baseline. Reset returns to this input."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setItemCount(std::size_t count)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (count < MinItemCount || count > MaxItemCount)
		{
			status_ = "Bar count must be between " + std::to_string(MinItemCount) + " and " + std::to_string(MaxItemCount) + ".";
			return CommandResult(false, status_);
		}
		if (count == itemCount_) { status_ = "Bar count is already " + std::to_string(itemCount_) + "."; return CommandResult(true, status_); }
		itemCount_ = count;
		const bool leftAndrutPreset = pattern_ == InputPattern::AndrutReference && itemCount_ != 100;
		if (leftAndrutPreset) pattern_ = InputPattern::Random;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? createFreshSeed() : 0);
		status_ = leftAndrutPreset
			? "Bar count set to " + std::to_string(itemCount_) + "; input arrangement returned to Random because the Andrut reference contains exactly 100 bars."
			: "Bar count set to " + std::to_string(itemCount_) + "; prepared baseline refreshed.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setDirection(SortDirection direction)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (direction_ == direction) { status_ = "Direction is unchanged."; return CommandResult(true, status_); }
		direction_ = direction; replaceTraceWithBaseline(); status_ = "Direction changed; build a new trace."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setDuration(float seconds)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (!std::isfinite(seconds) || seconds < MinRunDurationSeconds || seconds > MaxRunDurationSeconds) { status_ = "Run duration must be between 1 and 120 seconds."; return CommandResult(false, status_); }
		durationSeconds_ = seconds;
		updateElapsedFromCursor();
		std::ostringstream formattedDuration;
		formattedDuration << std::setprecision(6) << seconds;
		status_ = "Run duration set to " + formattedDuration.str() + " seconds.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::selectAlgorithm(SortAlgorithm algorithm)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm);
		if (!descriptor) { status_ = "The selected algorithm is not registered."; return CommandResult(false, status_); }
		InputDomain nextDomain = domain_;
		if (!algorithmSupports(algorithm, nextDomain))
		{
			if (!descriptor->supportedDomains.empty()) nextDomain = descriptor->supportedDomains.front();
			else if (descriptor->supportsNormalizedFloat) nextDomain = InputDomain::NormalizedFloats;
			else if (descriptor->supportsIntegerKeys) nextDomain = InputDomain::NonnegativeIntegerKeys;
			else { status_ = "The selected algorithm does not support an available input domain."; return CommandResult(false, status_); }
		}
		const bool domainChanged = nextDomain != domain_;
		const bool patternChanged = !inputPatternCompatible(nextDomain, pattern_);
		const bool algorithmChanged = algorithm_ != algorithm;
		if (!domainChanged && !patternChanged && !algorithmChanged)
		{
			status_ = "Algorithm selection is unchanged.";
			return CommandResult(true, status_);
		}
		domain_ = nextDomain;
		profile_ = inputProfileForDomain(nextDomain);
		if (patternChanged) pattern_ = InputPattern::Random;
		algorithm_ = algorithm;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = std::string("Algorithm set to ") + algorithmDisplayName(algorithm) + ".";
		if (domainChanged) status_ += std::string(" Input domain changed to ") + inputDomainName(domain_) + ".";
		if (patternChanged) status_ += " Incompatible input pattern changed to Random.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setInputProfile(InputProfile profile)
	{
		if (profile != InputProfile::NormalizedFloat && profile != InputProfile::IntegerKeys)
			return CommandResult(false, "Invalid legacy input profile.");
		return setInputDomain(profile == InputProfile::NormalizedFloat
			? InputDomain::NormalizedFloats : InputDomain::NonnegativeIntegerKeys);
	}

	CommandResult Simulation::setInputDomain(InputDomain domain)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (static_cast<int>(domain) < static_cast<int>(InputDomain::NormalizedFloats) ||
			static_cast<int>(domain) > static_cast<int>(InputDomain::IntegerPermutation))
			return CommandResult(false, "Invalid input domain.");
		if (!inputPatternCompatible(domain, pattern_))
			return CommandResult(false, std::string(inputPatternName(pattern_)) + " is unavailable for " + inputDomainName(domain) + ".");
		if (domain_ == domain) return CommandResult(true, "Input domain is unchanged.");
		domain_ = domain;
		profile_ = inputProfileForDomain(domain);
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = canRun() ? std::string("Input domain set to ") + inputDomainName(domain) + "; regenerated from the current seed."
			: std::string("Input domain set to ") + inputDomainName(domain) + "; selected algorithm does not support this domain.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setInputPattern(InputPattern pattern)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (pattern == InputPattern::AndrutReference)
		{
			if (!algorithmSupports(algorithm_, InputDomain::NormalizedFloats))
			{
				status_ = std::string(algorithmDisplayName(algorithm_)) + " does not support the Andrut reference input domain.";
				return CommandResult(false, status_);
			}
			domain_ = InputDomain::NormalizedFloats;
			profile_ = InputProfile::NormalizedFloat;
			itemCount_ = 100;
		}
		if (!inputPatternCompatible(domain_, pattern))
			return CommandResult(false, std::string(inputPatternName(pattern)) + " is unavailable for " + inputDomainName(domain_) + ".");
		if (pattern_ == pattern && (pattern != InputPattern::AndrutReference || itemCount_ == 100))
			return CommandResult(true, "Input pattern is unchanged.");
		pattern_ = pattern;
		prepareItems(baselineKind_, baselineKind_ == BaselineKind::Shuffle ? inputSeed_ : 0);
		status_ = pattern == InputPattern::AndrutReference
			? "Andrut's 100-bar reference arrangement loaded; item count set to 100."
			: std::string("Input pattern set to ") + inputPatternName(pattern) + "; regenerated from the current seed.";
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setShellGapSequence(ShellGapSequence sequence)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (sequence < ShellGapSequence::Halving || sequence > ShellGapSequence::Tokuda) return CommandResult(false, "Invalid Shell gap sequence.");
		if (shellGapSequence_ == sequence) return CommandResult(true, "Shell gap sequence is unchanged.");
		shellGapSequence_ = sequence; replaceTraceWithBaseline(); status_ = std::string("Shell gap sequence set to ") + shellGapSequenceName(sequence) + "."; return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeWritebackMode(MergeWritebackMode mode)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mode != MergeWritebackMode::AnimatedWrites && mode != MergeWritebackMode::AtomicRangeWrite)
			return CommandResult(false, "Invalid Merge Sort writeback mode.");
		if (mergeWritebackMode_ == mode) return CommandResult(true, "Merge Sort writeback mode is unchanged.");
		mergeWritebackMode_ = mode;
		replaceTraceWithBaseline();
		status_ = std::string("Merge Sort writeback set to ") +
			(mode == MergeWritebackMode::AtomicRangeWrite ? "comparison phase then atomic range write." : "animated item writes.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeExecutionMode(MergeExecutionMode mode)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mode != MergeExecutionMode::BottomUp && mode != MergeExecutionMode::TopDown)
			return CommandResult(false, "Invalid Merge Sort execution mode.");
		if (mergeExecutionMode_ == mode) return CommandResult(true, "Merge Sort execution mode is unchanged.");
		mergeExecutionMode_ = mode;
		replaceTraceWithBaseline();
		status_ = std::string("Merge Sort execution set to ") +
			(mode == MergeExecutionMode::BottomUp ? "bottom-up." : "top-down.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setCompletionScanEnabled(bool enabled)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (completionScanEnabled_ == enabled) return CommandResult(true, "Completion scan setting is unchanged.");
		completionScanEnabled_ = enabled;
		replaceTraceWithBaseline();
		status_ = std::string("Completion scan ") + (enabled ? "enabled." : "disabled.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::setMergeBufferVisualizationEnabled(bool enabled)
	{
		const CommandResult allowed = checkPreparationAllowed();
		if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (mergeBufferVisualizationEnabled_ == enabled)
			return CommandResult(true, "Merge buffer visualization setting is unchanged.");
		mergeBufferVisualizationEnabled_ = enabled;
		replaceTraceWithBaseline();
		status_ = std::string("Merge buffer visualization ") + (enabled ? "enabled." : "disabled.");
		return CommandResult(true, status_);
	}

	CommandResult Simulation::beginTraceBuild(TraceBuildRequest& request)
	{
		const CommandResult allowed = checkPreparationAllowed(); if (!allowed.accepted) { status_ = allowed.message; return allowed; }
		if (!algorithmSupports(algorithm_, domain_)) { status_ = std::string(algorithmDisplayName(algorithm_)) + " does not support " + inputDomainName(domain_) + "."; return CommandResult(false, status_); }
		if (!inputPatternCompatible(domain_, pattern_)) { status_ = std::string(inputPatternName(pattern_)) + " is incompatible with " + inputDomainName(domain_) + "."; return CommandResult(false, status_); }
		if (!algorithmAcceptsItemCount(algorithm_, itemCount_))
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm_);
			status_ = std::string(algorithmDisplayName(algorithm_)) + " has a safe limit of " +
				std::to_string(descriptor ? descriptor->safeMaxItemCount : 0) + " items. Reduce the item count to run it.";
			return CommandResult(false, status_);
		}
		request.input = preparedInput_; request.algorithm = algorithm_; request.direction = direction_; request.inputSeed = inputSeed_;
		const AlgorithmDescriptor* selected = algorithmDescriptor(algorithm_);
		request.familyId = selected ? selected->familyId : FamilyId();
		request.variantId = selected ? selected->variantId : VariantId();
		request.profile = profile_; request.domain = domain_; request.pattern = pattern_; request.shellGapSequence = shellGapSequence_;
		request.mergeWritebackMode = mergeWritebackMode_;
		request.mergeExecutionMode = mergeExecutionMode_;
		request.completionScanEnabled = completionScanEnabled_;
		request.mergeBufferVisualizationEnabled = mergeBufferVisualizationEnabled_;
		trace_.reset(); currentState_ = makeInitialState(request.input); cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
		buildingTrace_ = true; status_ = "Building deterministic trace from a copy of the prepared input..."; return CommandResult(true, status_);
	}

	void Simulation::completeTraceBuild(TraceBuildResult completed)
	{
		if (!buildingTrace_) return;
		buildingTrace_ = false;
		if (!completed.succeeded) { status_ = "Trace build failed: " + (completed.error.empty() ? "Unknown error." : completed.error); return; }
		trace_ = std::make_shared<SortTrace>(std::move(completed.trace)); currentState_ = trace_->initialState;
		cursor_ = 0; elapsedSeconds_ = 0.0f; playing_ = false;
		status_ = trace_->complete
			? "Trace ready: " + std::to_string(trace_->events.size()) + " events. Press Play."
			: "Partial trace ready: " + std::to_string(trace_->operationCount) + " operations. " + trace_->completionReason;
	}

	CommandResult Simulation::togglePlayback()
	{
		if (buildingTrace_) { status_ = "Trace is still being built; playback will be available when it is published."; return CommandResult(false, status_); }
		if (!trace_) { status_ = canRun() ? "Build a trace with Run before starting playback." : "Selected algorithm is incompatible with the current profile."; return CommandResult(false, status_); }
		if (playing_) { playing_ = false; status_ = "Playback paused."; return CommandResult(true, status_); }
		if (cursor_ >= trace_->events.size()) seekInternal(0);
		playing_ = true; status_ = "Playback running."; return CommandResult(true, status_);
	}
	CommandResult Simulation::stopPlayback() { playing_ = false; status_ = "Playback paused."; return CommandResult(true, status_); }
	CommandResult Simulation::reset()
	{
		playing_ = false; cursor_ = 0; elapsedSeconds_ = 0.0f;
		currentState_ = trace_ ? trace_->initialState : makeInitialState(preparedInput_);
		status_ = buildingTrace_ ? "Reset to the prepared baseline; trace generation continues." : "Reset to the prepared baseline at event zero.";
		return CommandResult(true, status_);
	}
	CommandResult Simulation::previousEvent()
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; stepping starts when ready." : "No trace is ready to step backward."; return CommandResult(false, status_); }
		playing_ = false; if (cursor_ > 0) seekInternal(cursor_ - 1); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::nextEvent()
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; stepping starts when ready." : "No trace is ready to step forward."; return CommandResult(false, status_); }
		playing_ = false; if (cursor_ < trace_->events.size()) seekInternal(cursor_ + 1); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::seekTo(std::size_t eventCursor)
	{
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; seeking starts when ready." : "Build a trace before seeking."; return CommandResult(false, status_); }
		playing_ = false; seekInternal(eventCursor); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	CommandResult Simulation::seekToFraction(float fraction)
	{
		if (!std::isfinite(fraction)) { status_ = "Timeline position must be finite."; return CommandResult(false, status_); }
		if (!trace_) { status_ = buildingTrace_ ? "Trace is being built; seeking starts when ready." : "Build a trace before using the timeline."; return CommandResult(false, status_); }
		fraction = std::max(0.0f, std::min(1.0f, fraction));
		const std::size_t target = fraction >= 1.0f ? trace_->events.size() : static_cast<std::size_t>(fraction * static_cast<double>(trace_->events.size()));
		playing_ = false; seekInternal(target); status_ = "Event cursor " + std::to_string(cursor_) + " / " + std::to_string(trace_->events.size()) + "."; return CommandResult(true, status_);
	}
	void Simulation::advance(float deltaSeconds)
	{
		if (!playing_ || !trace_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
		elapsedSeconds_ = std::min(durationSeconds_, elapsedSeconds_ + deltaSeconds);
		const float elapsed = elapsedSeconds_;
		const std::size_t count = trace_->events.size();
		const std::size_t target = elapsed >= durationSeconds_ || count == 0 ? count : static_cast<std::size_t>((static_cast<double>(elapsed) / durationSeconds_) * count);
		seekInternal(target); elapsedSeconds_ = elapsed;
		if (elapsed >= durationSeconds_)
		{
			playing_ = false; elapsedSeconds_ = durationSeconds_;
			status_ = trace_->complete ? "Playback complete." : "Partial playback complete. " + trace_->completionReason;
		}
	}

	void Simulation::synchronizePlaybackPosition(std::size_t eventCursor, double elapsedSeconds,
		double playbackDurationSeconds, bool playing)
	{
		if (!trace_ || buildingTrace_ || !std::isfinite(elapsedSeconds) ||
			!std::isfinite(playbackDurationSeconds) || playbackDurationSeconds < 0.0)
			return;
		seekInternal((std::min)(eventCursor, trace_->events.size()));
		const double duration = playbackDurationSeconds;
		elapsedSeconds_ = static_cast<float>((std::max)(0.0, (std::min)(duration, elapsedSeconds)));
		if (duration == 0.0 || elapsedSeconds >= duration)
		{
			seekInternal(trace_->events.size());
			elapsedSeconds_ = static_cast<float>(duration);
			playing_ = false;
			status_ = trace_->complete ? "Playback complete." : "Partial playback complete. " + trace_->completionReason;
		}
		else
			playing_ = playing;
	}

	void Simulation::seekInternal(std::size_t targetCursor)
	{
		if (!trace_) return;
		const std::size_t target = std::min(targetCursor, trace_->events.size());
		if (target != cursor_)
		{
			const std::size_t distance = target > cursor_ ? target - cursor_ : cursor_ - target;
			if (distance <= 256)
			{
				while (cursor_ > target) applyEventBackward(currentState_, trace_->events[--cursor_]);
				while (cursor_ < target) applyEventForward(currentState_, trace_->events[cursor_++]);
			}
			else
			{
				std::map<std::size_t, SortState>::const_iterator snapshot = trace_->snapshots.upper_bound(target);
				if (snapshot != trace_->snapshots.begin()) --snapshot;
				if (snapshot != trace_->snapshots.end()) { currentState_ = snapshot->second; cursor_ = snapshot->first; }
				else { currentState_ = trace_->initialState; cursor_ = 0; }
				while (cursor_ < target) applyEventForward(currentState_, trace_->events[cursor_++]);
			}
		}
		else cursor_ = target;
		updateElapsedFromCursor();
	}

	void Simulation::updateElapsedFromCursor()
	{
		if (!trace_ || trace_->events.empty()) elapsedSeconds_ = 0.0f;
		else elapsedSeconds_ = durationSeconds_ * (static_cast<float>(cursor_) / static_cast<float>(trace_->events.size()));
	}

	std::vector<ActiveRole> Simulation::rolesAtCursor() const
	{
		std::vector<ActiveRole> roles;
		if (!trace_ || cursor_ == 0) return roles;
		const SemanticEvent& event = trace_->events[cursor_ - 1];
		if (event.type == SemanticEventType::Compare)
		{
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex, event.firstLane));
			roles.push_back(ActiveRole(event.secondRole, event.secondItemId, event.secondIndex, event.secondLane));
		}
		else if (event.type == SemanticEventType::Probe)
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex, event.firstLane));
		else if (event.type == SemanticEventType::HoldKey)
		{
			roles.push_back(ActiveRole(SemanticRole::HeldKey, event.item.stableId, event.firstIndex));
			roles.push_back(ActiveRole(SemanticRole::Gap, NoItemId, event.firstIndex));
		}
		else if (event.type == SemanticEventType::ShiftItem)
		{
			roles.push_back(ActiveRole(SemanticRole::ShiftedItem, event.item.stableId, event.secondIndex));
			roles.push_back(ActiveRole(SemanticRole::Gap, NoItemId, event.firstIndex));
			roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.item.stableId, event.secondIndex));
		}
		else if (event.type == SemanticEventType::PlaceKey || event.type == SemanticEventType::WriteItem)
			roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.item.stableId, event.firstIndex, event.firstLane));
		else if (event.type == SemanticEventType::WriteRange)
			for (std::size_t i = 0; i < event.afterSlots.size(); ++i)
				if (event.afterSlots[i].occupied)
					roles.push_back(ActiveRole(SemanticRole::WriteTarget, event.afterSlots[i].item.stableId, event.firstIndex + i, event.firstLane));
		else if (event.type == SemanticEventType::Swap)
		{
			roles.push_back(ActiveRole(event.firstRole, event.firstItemId, event.firstIndex));
			roles.push_back(ActiveRole(event.secondRole, event.secondItemId, event.secondIndex));
		}
		else if (event.type == SemanticEventType::AuxiliaryItemWrite)
			roles.push_back(ActiveRole(SemanticRole::AuxiliaryCopy, event.item.stableId, event.firstIndex, event.workspaceName));
		else if (event.type == SemanticEventType::DiscardItem)
			roles.push_back(ActiveRole(SemanticRole::DiscardedItem, event.item.stableId, event.firstIndex, "discard"));
		return roles;
	}

	RenderSnapshot Simulation::snapshot() const
	{
		RenderSnapshot result;
		result.state = currentState_; result.metrics = currentState_.metrics; result.activeRoles = rolesAtCursor();
		result.direction = direction_; result.algorithm = algorithm_; result.profile = profile_; result.domain = domain_; result.pattern = pattern_;
		const AlgorithmDescriptor* selected = algorithmDescriptor(algorithm_);
		result.familyId = selected ? selected->familyId : FamilyId();
		result.variantId = selected ? selected->variantId : VariantId();
		result.shellGapSequence = shellGapSequence_; result.mergeWritebackMode = mergeWritebackMode_;
		result.mergeExecutionMode = mergeExecutionMode_;
		result.mergeBufferVisualizationEnabled = mergeBufferVisualizationEnabled_;
		result.cursor = cursor_; result.eventCount = trace_ ? trace_->events.size() : 0; result.itemCount = itemCount_; result.inputSeed = inputSeed_;
		result.audioEligibleEventCount = trace_ ? trace_->audioEventIndices.size() : 0;
		result.durationSeconds = durationSeconds_; result.elapsedSeconds = elapsedSeconds_; result.hasTrace = static_cast<bool>(trace_);
		result.buildingTrace = buildingTrace_; result.playing = playing_; result.complete = trace_ && cursor_ >= trace_->events.size();
		result.partial = trace_ && !trace_->complete;
		if (trace_) result.completionReason = trace_->completionReason;
		result.runCompatible = canRun();
		if (!inputPatternCompatible(domain_, pattern_))
			result.compatibilityMessage = std::string(inputPatternName(pattern_)) + " is incompatible with " + inputDomainName(domain_) + ".";
		else if (algorithmSupports(algorithm_, domain_) && !algorithmAcceptsItemCount(algorithm_, itemCount_))
		{
			const AlgorithmDescriptor* descriptor = algorithmDescriptor(algorithm_);
			result.compatibilityMessage = std::string(algorithmDisplayName(algorithm_)) + " is limited to " +
				std::to_string(descriptor ? descriptor->safeMaxItemCount : 0) + " items.";
		}
		else if (!result.runCompatible) result.compatibilityMessage = std::string(algorithmDisplayName(algorithm_)) + " does not support " + inputDomainName(domain_) + ".";
		result.status = status_;
		if (trace_)
			for (std::size_t i = 0; i < trace_->annotations.size(); ++i)
				if (trace_->annotations[i].beginCursor <= cursor_ && cursor_ < trace_->annotations[i].endCursor)
					result.activeRegions.push_back(trace_->annotations[i]);
		return result;
	}

	std::shared_ptr<const SortTrace> Simulation::currentTrace() const { return trace_; }

}
