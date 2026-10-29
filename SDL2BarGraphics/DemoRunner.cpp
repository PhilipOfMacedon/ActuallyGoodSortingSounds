#include "DemoRunner.h"

#include <algorithm>
#include <cmath>

DemoRunner::DemoRunner()
	: preset_(), state_(DemoRunnerState::Inactive), stepIndex_(0), effectiveInput_(), adjustments_(),
	retryCount_(0), intermissionRemaining_(0.0f), preparationPending_(false), hasEffectiveInput_(false), message_()
{
}

bool DemoRunner::begin(const DemoPreset& snapshot, std::string* error)
{
	const std::vector<DemoPresetIssue> issues = validateDemoPreset(snapshot);
	for (std::size_t i = 0; i < issues.size(); ++i)
	{
		if (issues[i].message.find("Algorithm unavailable:") == 0)
		{
			if (error) *error = "Cannot start demo step " + std::to_string(issues[i].stepIndex + 1) + ": " + issues[i].message;
			return false;
		}
		if (error) *error = issues[i].message;
		return false;
	}
	preset_ = snapshot;
	state_ = DemoRunnerState::Preparing;
	stepIndex_ = 0;
	effectiveInput_ = DemoInputRecipe();
	adjustments_.clear();
	retryCount_ = 0;
	intermissionRemaining_ = 0.0f;
	message_ = "Preparing step 1 of " + std::to_string(preset_.steps.size()) + ".";
	requestCurrentStep();
	if (error) error->clear();
	return true;
}

void DemoRunner::stop()
{
	preset_ = DemoPreset();
	state_ = DemoRunnerState::Inactive;
	stepIndex_ = 0;
	effectiveInput_ = DemoInputRecipe();
	adjustments_.clear();
	retryCount_ = 0;
	intermissionRemaining_ = 0.0f;
	preparationPending_ = false;
	hasEffectiveInput_ = false;
	message_.clear();
}

bool DemoRunner::active() const
{
	return state_ == DemoRunnerState::Preparing || state_ == DemoRunnerState::Prepared ||
		state_ == DemoRunnerState::Playing || state_ == DemoRunnerState::Intermission;
}

const DemoStep* DemoRunner::currentStep() const
{
	return stepIndex_ < preset_.steps.size() ? &preset_.steps[stepIndex_] : nullptr;
}

bool DemoRunner::previousStep()
{
	if (!active() || stepIndex_ == 0) return false;
	--stepIndex_;
	requestCurrentStep();
	return true;
}

bool DemoRunner::nextStep()
{
	if (!active() || stepIndex_ + 1 >= preset_.steps.size()) return false;
	++stepIndex_;
	requestCurrentStep();
	return true;
}

bool DemoRunner::takePreparationRequest()
{
	if (!preparationPending_) return false;
	preparationPending_ = false;
	return true;
}

void DemoRunner::setEffectiveInput(const DemoInputRecipe& recipe, const std::vector<std::string>& adjustments, bool resetRetryCount)
{
	effectiveInput_ = recipe;
	adjustments_ = adjustments;
	hasEffectiveInput_ = true;
	if (resetRetryCount) retryCount_ = 0;
}

bool DemoRunner::retryWithSmallerInput(DemoInputRecipe& recipe, std::string* error)
{
	if (effectiveInput_.itemCount <= sortinglogic::MinItemCount)
	{
		if (error) *error = "Trace budgets were still exceeded at the minimum supported input size.";
		return false;
	}
	recipe = effectiveInput_;
	recipe.itemCount = std::max(sortinglogic::MinItemCount, recipe.itemCount / 2);
	++retryCount_;
	std::string message = "Trace budget reached; retrying with " + std::to_string(recipe.itemCount) + " items using the same seed.";
	adjustments_.push_back(message);
	effectiveInput_ = recipe;
	state_ = DemoRunnerState::Preparing;
	message_ = message;
	preparationPending_ = true;
	if (error) error->clear();
	return true;
}

void DemoRunner::markPrepared(const std::string& message)
{
	state_ = DemoRunnerState::Prepared;
	preparationPending_ = false;
	message_ = message.empty()
		? "Step " + std::to_string(stepIndex_ + 1) + " is ready."
		: message;
}

void DemoRunner::markPlaying()
{
	if (!active()) return;
	state_ = DemoRunnerState::Playing;
	message_ = "Playing step " + std::to_string(stepIndex_ + 1) + " of " + std::to_string(preset_.steps.size()) + ".";
}

void DemoRunner::markPaused()
{
	if (state_ != DemoRunnerState::Playing) return;
	state_ = DemoRunnerState::Prepared;
	message_ = "Step " + std::to_string(stepIndex_ + 1) + " paused.";
}

void DemoRunner::playbackCompleted()
{
	if (state_ != DemoRunnerState::Playing) return;
	if (preset_.intermissionSeconds <= 0.0f)
	{
		advanceAfterIntermission();
		return;
	}
	state_ = DemoRunnerState::Intermission;
	intermissionRemaining_ = preset_.intermissionSeconds;
	message_ = "Intermission before step " + std::to_string(stepIndex_ + 2) + ".";
}

void DemoRunner::update(float deltaSeconds)
{
	if (state_ != DemoRunnerState::Intermission || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
	intermissionRemaining_ = std::max(0.0f, intermissionRemaining_ - deltaSeconds);
	if (intermissionRemaining_ <= 0.0f) advanceAfterIntermission();
}

void DemoRunner::fail(const std::string& message)
{
	state_ = DemoRunnerState::Failed;
	preparationPending_ = false;
	intermissionRemaining_ = 0.0f;
	message_ = message.empty() ? "Demo stopped because its step could not be prepared." : message;
}

void DemoRunner::requestCurrentStep()
{
	state_ = DemoRunnerState::Preparing;
	preparationPending_ = true;
	effectiveInput_ = DemoInputRecipe();
	adjustments_.clear();
	retryCount_ = 0;
	hasEffectiveInput_ = false;
	intermissionRemaining_ = 0.0f;
	message_ = "Preparing step " + std::to_string(stepIndex_ + 1) + " of " + std::to_string(preset_.steps.size()) + ".";
}

void DemoRunner::advanceAfterIntermission()
{
	intermissionRemaining_ = 0.0f;
	if (stepIndex_ + 1 < preset_.steps.size())
	{
		++stepIndex_;
		requestCurrentStep();
	}
	else if (preset_.loop)
	{
		stepIndex_ = 0;
		requestCurrentStep();
	}
	else
	{
		state_ = DemoRunnerState::Finished;
		preparationPending_ = false;
		message_ = "Demo finished.";
	}
}
