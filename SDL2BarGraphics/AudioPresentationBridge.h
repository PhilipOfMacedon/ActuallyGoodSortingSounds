#pragma once

#include <atomic>
#include <cstdint>
#include <map>

#include "../SoundEngine/SoundEngine.h"
#include "../SortingLogic/SortingLogic.h"

struct AudioPresentationState
{
	SoundEngine::Parameters parameters;
	SoundEngine::EventAccentSummary eventAccents;
	SoundEngine::AudioComparison comparison;
	bool traceLoaded;
	bool hasComparison;
	std::uint64_t revision;

	AudioPresentationState() : parameters(), eventAccents(), comparison(), traceLoaded(false), hasComparison(false), revision(0) {}
};

// A fixed-size triple buffer for one app-thread writer and one audio callback reader.
// The producer can replace unread state without waiting for the callback.
class LatestAudioStateMailbox
{
public:
	LatestAudioStateMailbox();
	void publish(const AudioPresentationState& state);
	bool consumeLatest(AudioPresentationState& state);

private:
	static const unsigned int DirtyBit = 4;
	static const unsigned int IndexMask = 3;
	AudioPresentationState slots_[3];
	std::atomic<unsigned int> middle_;
	unsigned int producerIndex_;
	unsigned int consumerIndex_;
};

class AudioPresentationBridge
{
public:
	AudioPresentationBridge();
	void update(const sortinglogic::Simulation& simulation,
		const sortinglogic::RenderSnapshot& snapshot,
		const SoundEngine::Parameters& parameters,
		LatestAudioStateMailbox& mailbox);

private:
	const sortinglogic::SortTrace* lastTrace_;
	std::size_t lastCursor_;
	SoundEngine::AudioComparison lastComparison_;
	SoundEngine::EventAccentSummary lastEventAccents_;
	bool hasComparison_;
	std::map<std::uint64_t, float> valuesById_;
	std::map<std::string, std::size_t> workspaceSizes_;
	float minimumValue_;
	float maximumValue_;
	AudioPresentationState lastPublished_;
	bool hasPublished_;
	std::uint64_t revision_;

	void recomputeComparison(const sortinglogic::SortTrace* trace, std::size_t cursor,
		SoundEngine::AudioComparison& comparison, bool& hasComparison) const;
	bool makeComparison(const sortinglogic::SortTrace& trace, const sortinglogic::SemanticEvent& event,
		SoundEngine::AudioComparison& comparison) const;
};
