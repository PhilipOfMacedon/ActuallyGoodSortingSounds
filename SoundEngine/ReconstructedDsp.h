#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace SoundEngine {
constexpr std::size_t ReconstructedNoteCapacityPerVoice = 256;
constexpr std::size_t ReconstructedFirTapCount = 31;
struct BurstNote {
    double frequencyHz = 0, phaseStep = 0, tukeyFraction = 0, startGain = 0;
    std::uint32_t lengthFrames = 0, ageFrames = 0;
    double delayFraction = 0;
    bool active = false;
};

// Shared by the registered source and the application profile. No heap state.
class DelayedSineBurstSource {
public:
    bool trigger(double frequency, double cycles, double taper, double fraction, double gain,
        std::uint32_t sampleRate = 44100);
    double sample();
    void reset();
    std::size_t activeCount() const;
    const std::array<BurstNote, ReconstructedNoteCapacityPerVoice>& snapshot() const { return notes_; }
    void restore(const std::array<BurstNote, ReconstructedNoteCapacityPerVoice>& notes) { notes_ = notes; }
private:
    std::array<BurstNote, ReconstructedNoteCapacityPerVoice> notes_{};
};
class ReferenceFir {
public:
    double sample(double input);
    void reset();
    static const std::array<double, ReconstructedFirTapCount>& coefficients();
    const std::array<double, ReconstructedFirTapCount>& history() const { return history_; }
    std::size_t position() const { return position_; }
    void restore(const std::array<double, ReconstructedFirTapCount>& history, std::size_t position)
    {
        history_ = history;
        position_ = position % ReconstructedFirTapCount;
    }
private:
    std::array<double, ReconstructedFirTapCount> history_{};
    std::size_t position_ = 0;
};
}
