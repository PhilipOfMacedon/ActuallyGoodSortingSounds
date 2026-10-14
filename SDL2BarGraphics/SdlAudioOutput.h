#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL.h>

#include "AudioPresentationBridge.h"

class SdlAudioOutput
{
public:
	explicit SdlAudioOutput(LatestAudioStateMailbox& mailbox);
	~SdlAudioOutput();

	SdlAudioOutput(const SdlAudioOutput&) = delete;
	SdlAudioOutput& operator=(const SdlAudioOutput&) = delete;

	bool retry();
	bool available() const { return device_ != 0; }
	const std::string& status() const { return status_; }
	bool armDiagnosticCapture();
	void serviceDiagnosticCapture();

private:
	enum class CaptureState : unsigned int
	{
		Idle,
		Armed,
		Complete,
		Saving,
		Saved
	};

	struct CallbackRecord
	{
		std::uint64_t frameOffset;
		std::uint32_t frameCount;
		std::uint64_t intervalTicks;
		std::uint64_t renderTicks;
		float rms;
		bool traceLoaded;
		bool hasComparison;
	};

	static const unsigned int DiagnosticDurationSeconds = 30;

	LatestAudioStateMailbox& mailbox_;
	SDL_AudioDeviceID device_;
	SDL_AudioSpec obtained_;
	bool subsystemInitialized_;
	std::string status_;
	SoundEngine::Synthesizer synthesizer_;
	std::uint64_t appliedRevision_;
	std::vector<float> diagnosticSamples_;
	std::vector<CallbackRecord> callbackRecords_;
	std::atomic<unsigned int> captureState_;
	std::size_t targetCaptureFrames_;
	std::size_t captureFramesWritten_;
	std::size_t callbackRecordCount_;
	std::uint64_t lastCallbackStartCounter_;
	bool callbackTraceLoaded_;
	bool callbackHasComparison_;

	static void audioCallback(void* userdata, Uint8* stream, int length);
	void render(Uint8* stream, int length);
	bool saveDiagnosticCapture();
};
