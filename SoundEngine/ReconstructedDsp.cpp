#include "ReconstructedDsp.h"
#include "ComposableEngine.h"
#include <algorithm>
#include <cmath>

namespace SoundEngine {
namespace {
constexpr double Pi = 3.1415926535897932384626433832795;
double window(std::uint32_t age, std::uint32_t length, double alpha) {
    if (age >= length || length < 2) return 0;
    if (alpha <= 0) return 1;
    const double x = double(age) / double(length - 1);
    if (x < alpha * .5) return .5 * (1 - std::cos(2 * Pi * x / alpha));
    if (x > 1 - alpha * .5) return .5 * (1 - std::cos(2 * Pi * (1 - x) / alpha));
    return 1;
}
double roundEven(double x) {
    const double low = std::floor(x), fraction = x - low;
    return fraction < .5 ? low : fraction > .5 ? low + 1 : (std::fmod(low, 2.) == 0 ? low : low + 1);
}
}
bool DelayedSineBurstSource::trigger(double frequency, double cycles, double taper, double fraction, double gain,
    std::uint32_t sampleRate) {
    if (gain <= 0) return true;
    if (!std::isfinite(frequency) || frequency <= 0 || !std::isfinite(gain) || sampleRate == 0) return false;
    for (auto& note : notes_) {
        if (note.active) continue;
        note.frequencyHz = frequency;
        note.phaseStep = 2 * Pi * frequency / sampleRate;
        note.lengthFrames = std::uint32_t((std::max)(2., roundEven(cycles * sampleRate / frequency)));
        note.ageFrames = 0; note.tukeyFraction = taper;
        note.delayFraction = fraction; note.startGain = gain; note.active = true;
        return true;
    }
    return false;
}
double DelayedSineBurstSource::sample() {
    double sum = 0;
    for (auto& note : notes_) {
        if (!note.active) continue;
        const auto age = note.ageFrames;
        const double current = age < note.lengthFrames ? std::sin(note.phaseStep * age) * window(age, note.lengthFrames, note.tukeyFraction) : 0;
        const double previous = age ? std::sin(note.phaseStep * (age - 1)) * window(age - 1, note.lengthFrames, note.tukeyFraction) : 0;
        sum += (current * (1 - note.delayFraction) + previous * note.delayFraction) * note.startGain;
        if (++note.ageFrames >= note.lengthFrames + (note.delayFraction > 0 ? 1u : 0u)) note.active = false;
    }
    return sum;
}
void DelayedSineBurstSource::reset() { notes_.fill(BurstNote()); }
std::size_t DelayedSineBurstSource::activeCount() const {
    std::size_t count = 0; for (const auto& note : notes_) if (note.active) ++count; return count;
}
const std::array<double, ReconstructedFirTapCount>& ReferenceFir::coefficients() {
    static const std::array<double, ReconstructedFirTapCount> taps{{
        .0016486559836917096,-.004185890318945093,.001360402251996021,-.00031163516317250843,
        .00261757621982264,.0011364214532758117,.00134444133827627,-.0026856849060442178,
        -.0028206594808248956,-.0011810646438476446,-.0011321145593396677,.0020016985251066813,
        .0035150245000002633,.005950763107064225,.013762066498436164,.015937095666321996,
        .009045040844705442,.0063039341331267915,.004098791990895944,.0015066864099895633,
        -.0008206938996880114,-.0019455424692308963,-.002342288209422767,-.0009956532022891026,
        .0010625867335191785,.000685565041919052,.0012126648546338754,-.0011150491083811178,
        -.00014805797477064722,.0021006851401400195,-.002261290933230586 }};
    return taps;
}
double ReferenceFir::sample(double input) {
    history_[position_] = input; double output = 0; auto index = position_;
    const auto& taps = coefficients();
    for (std::size_t tap = 0; tap < taps.size(); ++tap) {
        output += taps[tap] * history_[index]; index = index ? index - 1 : taps.size() - 1;
    }
    position_ = (position_ + 1) % taps.size(); return output;
}
void ReferenceFir::reset() { history_.fill(0); position_ = 0; }

namespace
{
class ReconstructedBurstGenerator : public ISignalGenerator
{
public:
    ReconstructedBurstGenerator() : source_(), sampleRate_(44100), clockHz_(300.0), cycles_(9.302650315454065),
        taper_(0.025666772580559438), clockPhase_(1.0)
    {
    }
    bool prepare(const RenderSpec& spec, const GeneratorConfiguration& configuration, std::string* error) override
    {
        if (spec.sampleRate < 8000 || !std::isfinite(configuration.packetClockHz) || configuration.packetClockHz <= 0 ||
            !std::isfinite(configuration.carrierCycles) || configuration.carrierCycles <= 0 ||
            !std::isfinite(configuration.tukeyFraction) || configuration.tukeyFraction < 0 || configuration.tukeyFraction > 1)
        {
            if (error) *error = "The reconstructed burst source configuration is invalid.";
            return false;
        }
        sampleRate_ = spec.sampleRate;
        clockHz_ = (std::min)(static_cast<double>(configuration.packetClockHz), sampleRate_ * 0.45);
        cycles_ = configuration.carrierCycles;
        taper_ = configuration.tukeyFraction;
        reset();
        if (error) error->clear();
        return true;
    }
    void reset() override
    {
        source_.reset();
        clockPhase_ = 1.0;
    }
    void render(float* output, const RenderContext& context) override
    {
        for (std::size_t frame = 0; frame < context.frameCount; ++frame)
        {
            if (clockPhase_ >= 1.0)
            {
                clockPhase_ -= 1.0;
                if (context.excitationEnabled[frame])
                    source_.trigger(context.frequencyHz[frame], cycles_, taper_, 0.0, 1.0, sampleRate_);
            }
            output[frame] = static_cast<float>(source_.sample());
            clockPhase_ += clockHz_ / sampleRate_;
        }
    }
private:
    DelayedSineBurstSource source_;
    std::uint32_t sampleRate_;
    double clockHz_, cycles_, taper_, clockPhase_;
};

class ReferenceFirProcessor : public IOutputProcessor
{
public:
    bool prepare(const RenderSpec&, const OutputProcessorConfiguration&, std::string* error) override
    {
        fir_.reset();
        if (error) error->clear();
        return true;
    }
    void reset() override { fir_.reset(); }
    void process(float* stereo, std::size_t frames) override
    {
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            const double mono = 0.5 * (static_cast<double>(stereo[frame * 2]) + stereo[frame * 2 + 1]);
            const float filtered = static_cast<float>(fir_.sample(mono));
            stereo[frame * 2] = stereo[frame * 2 + 1] = filtered;
        }
    }
private:
    ReferenceFir fir_;
};
}

std::unique_ptr<ISignalGenerator> createReconstructedBurstGenerator()
{
    return std::unique_ptr<ISignalGenerator>(new ReconstructedBurstGenerator());
}

std::unique_ptr<IOutputProcessor> createReferenceFirProcessor()
{
    return std::unique_ptr<IOutputProcessor>(new ReferenceFirProcessor());
}
}
