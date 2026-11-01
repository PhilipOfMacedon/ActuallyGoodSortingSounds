#include "AudioTimeline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace SoundEngine
{
namespace
{
bool finiteEvent(const AudioTimelineInputEvent& event)
{
    return std::isfinite(event.firstPosition) && std::isfinite(event.secondPosition) &&
        std::isfinite(event.firstGain) && std::isfinite(event.secondGain);
}

std::uint64_t ceilFrames(long double value)
{
    if (value <= 0) return 0;
    const long double maximum = static_cast<long double>((std::numeric_limits<std::uint64_t>::max)());
    return value >= maximum ? (std::numeric_limits<std::uint64_t>::max)() :
        static_cast<std::uint64_t>(std::ceil(value));
}

std::uint64_t floorFrames(long double value)
{
    if (value <= 0) return 0;
    const long double maximum = static_cast<long double>((std::numeric_limits<std::uint64_t>::max)());
    return value >= maximum ? (std::numeric_limits<std::uint64_t>::max)() :
        static_cast<std::uint64_t>(std::floor(value));
}
}

AudioTimelineInputEvent::AudioTimelineInputEvent()
    : sourceCursor(0), kind(TimelineEventKind::Other), firstPosition(0.5), secondPosition(0.5),
      firstRole(0), secondRole(0), firstGain(1.0), secondGain(1.0)
{
}

AudioTimelineGainSettings::AudioTimelineGainSettings()
    : comparisonGain(1.0), heldKeyGain(1.0), probeGain(1.0), auxiliaryGain(1.0), reconstructionGain(1.0), roleGain()
{
    for (std::size_t i = 0; i < AudioTimelineRoleCount; ++i) roleGain[i] = 1.0;
}

PreparedAudioTimeline::PreparedAudioTimeline()
    : events_(), eligibleIndices_(), sourceEventCount_(0), eligibleEventCount_(0), durationFrames_(0),
      sampleRate_(44100), mode_(PlaybackTimingMode::FixedDuration), ready_(false)
{
}

bool PreparedAudioTimeline::prepare(const std::vector<AudioTimelineInputEvent>& input,
    std::size_t sourceEventCount, PlaybackTimingMode mode, double fixedDurationSeconds,
    std::uint32_t sampleRate, std::uint32_t referenceEventsPerSecond, std::string* error,
    FixedDurationSchedule fixedDurationSchedule)
{
    ready_ = false;
    events_.clear();
    eligibleIndices_.clear();
    sourceEventCount_ = sourceEventCount;
    eligibleEventCount_ = 0;
    durationFrames_ = 0;
    if (sampleRate == 0 || (mode == PlaybackTimingMode::FixedDuration &&
        (!std::isfinite(fixedDurationSeconds) || fixedDurationSeconds < 0.0)) ||
        (mode == PlaybackTimingMode::ReferenceCadence && referenceEventsPerSecond == 0))
    {
        if (error) *error = "The audio timeline timing parameters are invalid.";
        return false;
    }
    std::size_t previousCursor = 0;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        if (input[i].sourceCursor <= previousCursor || input[i].sourceCursor > sourceEventCount || !finiteEvent(input[i]))
        {
            if (error) *error = "Audio timeline events must be finite and ordered by a valid source cursor.";
            return false;
        }
        previousCursor = input[i].sourceCursor;
        if (input[i].kind != TimelineEventKind::Other) ++eligibleEventCount_;
    }

    sampleRate_ = sampleRate;
    mode_ = mode;
    if (mode == PlaybackTimingMode::FixedDuration)
    {
        durationFrames_ = ceilFrames(static_cast<long double>(fixedDurationSeconds) * sampleRate);
        events_.reserve(input.size());
        std::vector<std::uint64_t> frames(input.size(), durationFrames_);
        if (fixedDurationSchedule == FixedDurationSchedule::EligibleEvents && eligibleEventCount_ > 0)
        {
            std::size_t eligibleOrdinal = 0;
            for (std::size_t i = 0; i < input.size(); ++i)
                if (input[i].kind != TimelineEventKind::Other)
                    frames[i] = floorFrames(static_cast<long double>(eligibleOrdinal++) * durationFrames_ /
                        eligibleEventCount_);

            std::uint64_t upcomingFrame = durationFrames_;
            for (std::size_t i = input.size(); i > 0; --i)
            {
                if (input[i - 1].kind != TimelineEventKind::Other)
                    upcomingFrame = frames[i - 1];
                else frames[i - 1] = upcomingFrame;
            }
        }
        else
        {
            for (std::size_t i = 0; i < input.size(); ++i)
                frames[i] = sourceEventCount == 0 ? 0 : ceilFrames(
                    static_cast<long double>(input[i].sourceCursor) * durationFrames_ / sourceEventCount);
        }
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            AudioTimelineEvent event;
            event.frame = frames[i];
            event.sourceCursor = input[i].sourceCursor;
            event.kind = input[i].kind;
            event.firstPosition = input[i].firstPosition;
            event.secondPosition = input[i].secondPosition;
            event.firstRole = input[i].firstRole;
            event.secondRole = input[i].secondRole;
            event.firstGain = input[i].firstGain;
            event.secondGain = input[i].secondGain;
            events_.push_back(event);
            if (event.kind != TimelineEventKind::Other) eligibleIndices_.push_back(events_.size() - 1);
        }
    }
    else
    {
        durationFrames_ = ceilFrames(static_cast<long double>(eligibleEventCount_) * sampleRate /
            referenceEventsPerSecond);
        events_.resize(input.size());
        std::vector<std::uint64_t> eligibleFrames(eligibleEventCount_);
        std::vector<std::uint64_t> frameByInputIndex(input.size(), durationFrames_);
        std::vector<std::uint64_t> nextEligibleFrame(input.size(), durationFrames_);
        std::size_t eligibleOrdinal = 0;
        for (std::size_t i = 0; i < input.size(); ++i)
            if (input[i].kind != TimelineEventKind::Other)
            {
                eligibleFrames[eligibleOrdinal] = static_cast<std::uint64_t>(
                    (static_cast<long double>(eligibleOrdinal) * sampleRate) / referenceEventsPerSecond);
                frameByInputIndex[i] = eligibleFrames[eligibleOrdinal++];
            }

        std::uint64_t upcomingFrame = durationFrames_;
        for (std::size_t i = input.size(); i > 0; --i)
        {
            if (input[i - 1].kind != TimelineEventKind::Other)
                upcomingFrame = frameByInputIndex[i - 1];
            nextEligibleFrame[i - 1] = upcomingFrame;
        }

        eligibleOrdinal = 0;
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            std::uint64_t frame = durationFrames_;
            if (input[i].kind != TimelineEventKind::Other)
                frame = eligibleFrames[eligibleOrdinal++];
            else
                frame = nextEligibleFrame[i];
            AudioTimelineEvent& event = events_[i];
            event.frame = frame;
            event.sourceCursor = input[i].sourceCursor;
            event.kind = input[i].kind;
            event.firstPosition = input[i].firstPosition;
            event.secondPosition = input[i].secondPosition;
            event.firstRole = input[i].firstRole;
            event.secondRole = input[i].secondRole;
            event.firstGain = input[i].firstGain;
            event.secondGain = input[i].secondGain;
        }
        for (std::size_t i = 0; i < events_.size(); ++i)
            if (events_[i].kind != TimelineEventKind::Other) eligibleIndices_.push_back(i);
    }
    ready_ = true;
    if (error) error->clear();
    return true;
}

double PreparedAudioTimeline::durationSeconds() const
{
    return sampleRate_ == 0 ? 0.0 : static_cast<double>(durationFrames_) / sampleRate_;
}

std::size_t PreparedAudioTimeline::cursorAtFrame(std::uint64_t frame) const
{
    const auto end = std::upper_bound(events_.begin(), events_.end(), frame,
        [](std::uint64_t target, const AudioTimelineEvent& event) { return target < event.frame; });
    return end == events_.begin() ? 0 : (end - 1)->sourceCursor;
}

bool PreparedAudioTimeline::comparisonAtFrame(std::uint64_t frame, AudioTimelineEvent& event) const
{
    const auto end = std::upper_bound(eligibleIndices_.begin(), eligibleIndices_.end(), frame,
        [this](std::uint64_t target, std::size_t index) { return target < events_[index].frame; });
    if (end == eligibleIndices_.begin()) return false;
    event = events_[*(end - 1)];
    return true;
}

bool PreparedAudioTimeline::comparisonAtCursor(std::size_t cursor, AudioTimelineEvent& event) const
{
    const auto end = std::upper_bound(eligibleIndices_.begin(), eligibleIndices_.end(), cursor,
        [this](std::size_t target, std::size_t index) { return target < events_[index].sourceCursor; });
    if (end == eligibleIndices_.begin()) return false;
    event = events_[*(end - 1)];
    return true;
}

std::uint64_t PreparedAudioTimeline::frameForCursor(std::size_t cursor) const
{
    if (cursor == 0 || events_.empty()) return 0;
    const auto found = std::lower_bound(events_.begin(), events_.end(), cursor,
        [](const AudioTimelineEvent& event, std::size_t target) { return event.sourceCursor < target; });
    return found == events_.end() ? durationFrames_ : found->frame;
}
}
