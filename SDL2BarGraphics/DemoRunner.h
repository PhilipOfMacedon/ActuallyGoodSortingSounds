#pragma once

#include "DemoModels.h"

enum class DemoRunnerState
{
	Inactive,
	Preparing,
	Prepared,
	Playing,
	Intermission,
	Finished,
	Failed
};

// Playback sequence state only. The application owns the active RunSession and
// services the runner's preparation requests through the single trace worker.
class DemoRunner
{
public:
	DemoRunner();

	bool begin(const DemoPreset& snapshot, std::string* error = nullptr);
	void stop();
	bool active() const;
	bool paused() const { return paused_; }
	void resume();
	DemoRunnerState state() const { return state_; }
	const DemoPreset& preset() const { return preset_; }
	std::size_t stepIndex() const { return stepIndex_; }
	const DemoStep* currentStep() const;
	const DemoInputRecipe& effectiveInput() const { return effectiveInput_; }
	const std::vector<std::string>& adjustments() const { return adjustments_; }
	const std::string& message() const { return message_; }
	unsigned retryCount() const { return retryCount_; }
	float intermissionRemaining() const { return intermissionRemaining_; }
	bool preparationPending() const { return preparationPending_; }
	bool hasEffectiveInput() const { return hasEffectiveInput_; }

	bool previousStep();
	bool nextStep();
	bool takePreparationRequest();
	void setEffectiveInput(const DemoInputRecipe& recipe, const std::vector<std::string>& adjustments,
		bool resetRetryCount = true);
	bool retryWithSmallerInput(DemoInputRecipe& recipe, std::string* error = nullptr);
	void markPrepared(const std::string& message = std::string());
	void markPlaying();
	void markPaused();
	void playbackCompleted();
	void update(float deltaSeconds);
	void fail(const std::string& message);

private:
	DemoPreset preset_;
	DemoRunnerState state_;
	std::size_t stepIndex_;
	DemoInputRecipe effectiveInput_;
	std::vector<std::string> adjustments_;
	unsigned retryCount_;
	float intermissionRemaining_;
	bool preparationPending_;
	bool hasEffectiveInput_;
	bool paused_;
	std::string message_;

	void requestCurrentStep();
	void advanceAfterIntermission();
};
