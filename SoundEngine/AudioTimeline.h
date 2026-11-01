#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SoundEngine
{
enum class PlaybackTimingMode : std::uint8_t { FixedDuration, ReferenceCadence };
enum class TimelineEventKind : std::uint8_t { Other, Comparison, Probe, Auxiliary, Reconstruction };
enum class FixedDurationSchedule : std::uint8_t { SourceCursor, EligibleEvents };

static const std::size_t AudioTimelineRoleCount = 8;

// Gain controls travel separately from immutable event data so live mixer
// changes affect upcoming note births without rebuilding or replacing a trace.
struct AudioTimelineGainSettings
{
    double comparisonGain;
    double heldKeyGain;
    double probeGain;
    double auxiliaryGain;
    double reconstructionGain;
    double roleGain[AudioTimelineRoleCount];
    AudioTimelineGainSettings();
};

// Plain transfer data. The app translates sorting events into this form; the
// sound library never needs to know about sorting types or item storage.
struct AudioTimelineInputEvent
{
    std::size_t sourceCursor;
    TimelineEventKind kind;
    double firstPosition;
    double secondPosition;
    std::uint8_t firstRole;
    std::uint8_t secondRole;
    double firstGain;
    double secondGain;
    AudioTimelineInputEvent();
};

struct AudioTimelineEvent
{
    std::uint64_t frame;
    std::size_t sourceCursor;
    TimelineEventKind kind;
    double firstPosition;
    double secondPosition;
    std::uint8_t firstRole;
    std::uint8_t secondRole;
    double firstGain;
    double secondGain;
};

// Immutable after prepare(). Queries use binary search and do not allocate,
// which makes the same object usable by a callback and by host-clock playback.
class PreparedAudioTimeline
{
public:
    PreparedAudioTimeline();
    bool prepare(const std::vector<AudioTimelineInputEvent>& events,
        std::size_t sourceEventCount, PlaybackTimingMode mode,
        double fixedDurationSeconds, std::uint32_t sampleRate = 44100,
        std::uint32_t referenceEventsPerSecond = 300,
        std::string* error = nullptr,
        FixedDurationSchedule fixedDurationSchedule = FixedDurationSchedule::SourceCursor);

    std::uint64_t durationFrames() const { return durationFrames_; }
    std::uint32_t sampleRate() const { return sampleRate_; }
    std::size_t sourceEventCount() const { return sourceEventCount_; }
    std::size_t eligibleEventCount() const { return eligibleEventCount_; }
    PlaybackTimingMode mode() const { return mode_; }
    double durationSeconds() const;
    const std::vector<AudioTimelineEvent>& events() const { return events_; }

    std::size_t cursorAtFrame(std::uint64_t frame) const;
    bool comparisonAtFrame(std::uint64_t frame, AudioTimelineEvent& event) const;
    bool comparisonAtCursor(std::size_t cursor, AudioTimelineEvent& event) const;
    std::uint64_t frameForCursor(std::size_t cursor) const;
    bool ready() const { return ready_; }

private:
    std::vector<AudioTimelineEvent> events_;
    std::vector<std::size_t> eligibleIndices_;
    std::size_t sourceEventCount_;
    std::size_t eligibleEventCount_;
    std::uint64_t durationFrames_;
    std::uint32_t sampleRate_;
    PlaybackTimingMode mode_;
    bool ready_;
};

struct AudioTransportState
{
    std::uint64_t timelineFrame;
    std::size_t sourceCursor;
    std::uint64_t generation;
    std::uint64_t revision;
    bool playing;
    bool holdSelection;
    bool releaseSelection;
    bool stopAtEnd;
    AudioTransportState() : timelineFrame(0), sourceCursor(0), generation(0), revision(0), playing(false),
        holdSelection(false), releaseSelection(false), stopAtEnd(false) {}
};

struct AudioPlayheadFeedback
{
    std::uint64_t timelineFrame;
    std::size_t sourceCursor;
    std::uint64_t generation;
    std::uint64_t transportRevision;
    std::uint64_t durationFrames;
    bool playing;
    AudioPlayheadFeedback() : timelineFrame(0), sourceCursor(0), generation(0), transportRevision(0), durationFrames(0), playing(false) {}
};
}
