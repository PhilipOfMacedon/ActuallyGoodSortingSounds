#include "ComposableEngine.h"
#include "ReconstructedDsp.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
const double Pi = 3.1415926535897932384626433832795;
const double TwoPi = 2.0 * Pi;
const double ConstantPower = 0.7071067811865475244;
const std::size_t ControlCadence = 32;
const std::size_t WindowTableSize = 2049;

float clampFloat(float value, float low, float high)
{
    return (std::max)(low, (std::min)(high, value));
}

double clampDouble(double value, double low, double high)
{
    return (std::max)(low, (std::min)(high, value));
}

float alphaFor(float milliseconds, std::uint32_t sampleRate)
{
    if (milliseconds <= 0.0f)
        return 1.0f;
    const double seconds = static_cast<double>(milliseconds) * 0.001;
    return static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}

std::vector<SoundEngine::GeneratorDescriptor> &generatorTable()
{
    static std::vector<SoundEngine::GeneratorDescriptor> value;
    return value;
}
std::vector<SoundEngine::FilterDescriptor> &filterTable()
{
    static std::vector<SoundEngine::FilterDescriptor> value;
    return value;
}
std::vector<SoundEngine::OutputProcessorDescriptor> &outputTable()
{
    static std::vector<SoundEngine::OutputProcessorDescriptor> value;
    return value;
}

class ContinuousSine : public SoundEngine::ISignalGenerator
{
  public:
    ContinuousSine() : phase_(0.0), sampleRate_(44100)
    {
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::GeneratorConfiguration &,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        phase_ = 0.0;
        return true;
    }
    void reset() override
    {
        phase_ = 0.0;
    }
    void render(float *output, const SoundEngine::RenderContext &context) override
    {
        for (std::size_t i = 0; i < context.frameCount; ++i)
        {
            output[i] = static_cast<float>(std::sin(phase_));
            phase_ += TwoPi * context.frequencyHz[i] / sampleRate_;
            if (phase_ >= TwoPi)
                phase_ -= TwoPi * std::floor(phase_ / TwoPi);
        }
    }

  private:
    double phase_;
    std::uint32_t sampleRate_;
};

class ClockedSinePackets : public SoundEngine::ISignalGenerator
{
  public:
    ClockedSinePackets()
        : sampleRate_(44100), clockHz_(300.0), clockPhase_(1.0), referencePhase_(0.0), configuration_(), windowTable_(),
          activeGrain_()
    {
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::GeneratorConfiguration &configuration,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        configuration_ = configuration;
        clockHz_ = clampDouble(configuration.packetClockHz, 1.0, sampleRate_ * 0.2);
        clockPhase_ = 1.0;
        referencePhase_ = 0.0;
        windowTable_.resize(WindowTableSize);
        const double gaussianEdge = std::exp(-8.0);
        for (std::size_t i = 0; i < WindowTableSize; ++i)
        {
            const double x = static_cast<double>(i) / static_cast<double>(WindowTableSize - 1);
            if (configuration_.window == SoundEngine::PacketWindow::Hann)
                windowTable_[i] = static_cast<float>(0.5 - 0.5 * std::cos(TwoPi * x));
            else
            {
                const double centered = 2.0 * x - 1.0;
                windowTable_[i] =
                    static_cast<float>((std::exp(-8.0 * centered * centered) - gaussianEdge) / (1.0 - gaussianEdge));
            }
        }
        reset();
        return true;
    }
    void reset() override
    {
        clockPhase_ = 1.0;
        referencePhase_ = 0.0;
        for (std::size_t i = 0; i < activeGrain_.size(); ++i)
            activeGrain_[i] = Grain();
    }
    void render(float *output, const SoundEngine::RenderContext &context) override
    {
        for (std::size_t frame = 0; frame < context.frameCount; ++frame)
        {
            if (clockPhase_ >= 1.0)
            {
                clockPhase_ -= 1.0;
                if (context.excitationEnabled[frame])
                    launch(context.frequencyHz[frame]);
            }
            double sample = 0.0;
            for (std::size_t i = 0; i < activeGrain_.size(); ++i)
            {
                Grain &grain = activeGrain_[i];
                if (!grain.active)
                    continue;
                const double position = grain.durationSamples <= 1 ? 1.0
                                                                   : static_cast<double>(grain.age) /
                                                                         static_cast<double>(grain.durationSamples - 1);
                const double tablePosition = position * static_cast<double>(windowTable_.size() - 1);
                const std::size_t index = (std::min)(static_cast<std::size_t>(tablePosition), windowTable_.size() - 2);
                const float fraction = static_cast<float>(tablePosition - index);
                const float window = windowTable_[index] + (windowTable_[index + 1] - windowTable_[index]) * fraction;
                sample += window * grain.normalization * std::sin(grain.phase);
                grain.phase += grain.phaseIncrement;
                if (grain.phase >= TwoPi)
                    grain.phase -= TwoPi * std::floor(grain.phase / TwoPi);
                if (++grain.age >= grain.durationSamples)
                    grain.active = false;
            }
            output[frame] = static_cast<float>(sample);
            referencePhase_ += TwoPi * context.frequencyHz[frame] / sampleRate_;
            if (referencePhase_ >= TwoPi)
                referencePhase_ -= TwoPi * std::floor(referencePhase_ / TwoPi);
            clockPhase_ += clockHz_ / sampleRate_;
        }
    }

  private:
    struct Grain
    {
        bool active;
        std::uint32_t age, durationSamples;
        double phase, phaseIncrement;
        float normalization;
        Grain() : active(false), age(0), durationSamples(0), phase(0.0), phaseIncrement(0.0), normalization(1.0f)
        {
        }
    };
    void launch(float frequency)
    {
        Grain *selected = nullptr;
        for (std::size_t i = 0; i < activeGrain_.size(); ++i)
            if (!activeGrain_[i].active)
            {
                selected = &activeGrain_[i];
                break;
            }
        if (selected == nullptr)
        {
            selected = &activeGrain_[0];
            for (std::size_t i = 1; i < activeGrain_.size(); ++i)
                if (activeGrain_[i].age > selected->age)
                    selected = &activeGrain_[i];
        }
        const double basePeriod = 1.0 / clockHz_;
        double durationSeconds = configuration_.durationMode == SoundEngine::PacketDuration::FixedSeconds
                                     ? configuration_.packetDurationSeconds
                                     : configuration_.carrierCycles / (std::max)(1.0f, frequency);
        durationSeconds = clampDouble(durationSeconds, basePeriod * 0.5, basePeriod * 4.0);
        const std::uint32_t duration =
            (std::max)(2u, static_cast<std::uint32_t>(std::lround(durationSeconds * sampleRate_)));
        const double baselineSamples = (2.0 / clockHz_) * sampleRate_;
        const float normalization = configuration_.normalization == SoundEngine::PacketNormalization::WindowEnergy
                                        ? static_cast<float>(std::sqrt(baselineSamples / duration))
                                        : 1.0f;
        selected->active = true;
        selected->age = 0;
        selected->durationSamples = duration;
        selected->phase = configuration_.phase == SoundEngine::PacketPhase::ResetAtBirth ? 0.0 : referencePhase_;
        selected->phaseIncrement = TwoPi * (std::max)(1.0f, frequency) / sampleRate_;
        selected->normalization = normalization;
    }
    std::uint32_t sampleRate_;
    double clockHz_, clockPhase_, referencePhase_;
    SoundEngine::GeneratorConfiguration configuration_;
    std::vector<float> windowTable_;
    std::array<Grain, 8> activeGrain_;
};

class BandlimitedSaw300 : public SoundEngine::ISignalGenerator
{
  public:
    BandlimitedSaw300() : phase_(0.0), sampleRate_(44100), maximumHarmonic_(57)
    {
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::GeneratorConfiguration &configuration,
                 std::string *) override
    {
        phase_ = 0.0;
        sampleRate_ = spec.sampleRate;
        const double maximum = (std::min)(static_cast<double>(configuration.sawMaximumHz), sampleRate_ * 0.45);
        maximumHarmonic_ = (std::max)(1u, static_cast<unsigned int>(std::floor(maximum / 300.0)));
        return true;
    }
    void reset() override
    {
        phase_ = 0.0;
    }
    void render(float *output, const SoundEngine::RenderContext &context) override
    {
        for (std::size_t frame = 0; frame < context.frameCount; ++frame)
        {
            double value = 0.0;
            for (unsigned int harmonic = 1; harmonic <= maximumHarmonic_; ++harmonic)
            {
                const double sign = (harmonic & 1u) ? 1.0 : -1.0;
                value += sign * std::sin(phase_ * harmonic) / harmonic;
            }
            output[frame] = static_cast<float>((2.0 / Pi) * value);
            phase_ += TwoPi * 300.0 / sampleRate_;
            if (phase_ >= TwoPi)
                phase_ -= TwoPi;
        }
    }

  private:
    double phase_;
    std::uint32_t sampleRate_;
    unsigned int maximumHarmonic_;
};

class HarmonicBed : public SoundEngine::ISignalGenerator
{
  public:
    HarmonicBed() : phases_(), sampleRate_(44100), configuration_(), gains_(), phasesOffset_()
    {
        phases_.fill(0.0);
        gains_.fill(0.0);
        phasesOffset_.fill(0.0);
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::GeneratorConfiguration &configuration,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        configuration_ = configuration;
        const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount> &bank = SoundEngine::frequencyBank();
        for (std::size_t i = 0; i < bank.size(); ++i)
        {
            const double frequency = bank[i].frequencyHz;
            if (frequency < 300.0)
            {
                gains_[i] = configuration_.includeSub300HzBed ? bank[i].baseAmplitude : 0.0f;
                phasesOffset_[i] = 0.0;
            }
            else
            {
                const double harmonic = frequency / 300.0;
                gains_[i] = static_cast<float>(std::pow(harmonic, -configuration_.harmonicExponent) *
                                               SoundEngine::bedCombMagnitude(frequency, configuration_.bedComb));
                phasesOffset_[i] = SoundEngine::bedCombPhase(frequency, configuration_.bedComb);
            }
        }
        reset();
        return true;
    }
    void reset() override
    {
        phases_.fill(0.0);
    }
    void render(float *output, const SoundEngine::RenderContext &context) override
    {
        const std::array<SoundEngine::FrequencyBand, SoundEngine::BandCount> &bank = SoundEngine::frequencyBank();
        for (std::size_t frame = 0; frame < context.frameCount; ++frame)
        {
            double mixed = 0.0;
            for (std::size_t i = 0; i < bank.size(); ++i)
            {
                mixed += static_cast<double>(gains_[i]) * std::sin(phases_[i] + phasesOffset_[i]);
                phases_[i] += TwoPi * bank[i].frequencyHz / sampleRate_;
                if (phases_[i] >= TwoPi)
                    phases_[i] -= TwoPi;
            }
            output[frame] = static_cast<float>(mixed);
        }
    }

  private:
    std::array<double, SoundEngine::BandCount> phases_;
    std::uint32_t sampleRate_;
    SoundEngine::GeneratorConfiguration configuration_;
    std::array<float, SoundEngine::BandCount> gains_;
    std::array<double, SoundEngine::BandCount> phasesOffset_;
};

class BypassFilter : public SoundEngine::IAudioFilter
{
  public:
    bool prepare(const SoundEngine::RenderSpec &, const SoundEngine::FilterConfiguration &, std::string *) override
    {
        return true;
    }
    void reset() override
    {
    }
    void process(float *, const SoundEngine::RenderContext &) override
    {
    }
};

class TptBandPass : public SoundEngine::IAudioFilter
{
  public:
    TptBandPass()
        : sampleRate_(44100), bandwidthHz_(200.0f), q_(4.0f), qCurrent_(4.0), targetQ_(4.0), stepQ_(0.0),
          constantQ_(true), g_(0.0), targetG_(0.0), stepG_(0.0), initialG_(0.0), initialQ_(4.0f), cadence_(0),
          ic1eq_(0.0), ic2eq_(0.0)
    {
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::FilterConfiguration &configuration,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        bandwidthHz_ = (std::max)(1.0f, configuration.bandwidthHz);
        q_ = clampFloat(configuration.q, 0.1f, 64.0f);
        qCurrent_ = targetQ_ = q_;
        stepQ_ = 0.0;
        constantQ_ = configuration.constantQ;
        g_ = targetG_ = coefficient(configuration.cutoffHz);
        initialG_ = g_;
        initialQ_ = q_;
        stepG_ = 0.0;
        reset();
        return true;
    }
    void reset() override
    {
        cadence_ = 0;
        ic1eq_ = ic2eq_ = 0.0;
        g_ = targetG_ = initialG_;
        stepG_ = 0.0;
        q_ = initialQ_;
        qCurrent_ = targetQ_ = initialQ_;
        stepQ_ = 0.0;
    }
    void process(float *samples, const SoundEngine::RenderContext &context) override
    {
        for (std::size_t i = 0; i < context.frameCount; ++i)
        {
            if (cadence_ == 0)
            {
                float q = q_;
                if (!constantQ_)
                    q = clampFloat(context.frequencyHz[i] / bandwidthHz_, 0.1f, 64.0f);
                targetQ_ = q;
                stepQ_ = (targetQ_ - qCurrent_) / static_cast<double>(ControlCadence);
                targetG_ = coefficient(context.frequencyHz[i]);
                stepG_ = (targetG_ - g_) / static_cast<double>(ControlCadence);
            }
            g_ += stepG_;
            qCurrent_ += stepQ_;
            const double k = 1.0 / qCurrent_;
            const double a1 = 1.0 / (1.0 + g_ * (g_ + k));
            const double a2 = g_ * a1;
            const double a3 = g_ * a2;
            const double v3 = static_cast<double>(samples[i]) - ic2eq_;
            const double v1 = a1 * ic1eq_ + a2 * v3;
            const double v2 = ic2eq_ + a2 * ic1eq_ + a3 * v3;
            ic1eq_ = 2.0 * v1 - ic1eq_;
            ic2eq_ = 2.0 * v2 - ic2eq_;
            samples[i] = static_cast<float>(v1 / qCurrent_);
            cadence_ = (cadence_ + 1) % ControlCadence;
        }
    }

  private:
    double coefficient(float cutoff) const
    {
        const double safe = clampDouble(cutoff, 10.0, sampleRate_ * 0.45);
        return std::tan(Pi * safe / sampleRate_);
    }
    std::uint32_t sampleRate_;
    float bandwidthHz_, q_;
    double qCurrent_, targetQ_, stepQ_;
    bool constantQ_;
    double g_, targetG_, stepG_, initialG_;
    float initialQ_;
    std::size_t cadence_;
    double ic1eq_, ic2eq_;
};

class ButterworthLowPass : public SoundEngine::IAudioFilter
{
  public:
    ButterworthLowPass() : sampleRate_(44100), g_(0.0), ic1eq_(0.0), ic2eq_(0.0)
    {
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::FilterConfiguration &configuration,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        const double cutoff = clampDouble(configuration.cutoffHz, 10.0, sampleRate_ * 0.45);
        g_ = std::tan(Pi * cutoff / sampleRate_);
        reset();
        return true;
    }
    void reset() override
    {
        ic1eq_ = ic2eq_ = 0.0;
    }
    void process(float *samples, const SoundEngine::RenderContext &context) override
    {
        const double k = std::sqrt(2.0);
        const double a1 = 1.0 / (1.0 + g_ * (g_ + k));
        const double a2 = g_ * a1;
        const double a3 = g_ * a2;
        for (std::size_t i = 0; i < context.frameCount; ++i)
        {
            const double v3 = static_cast<double>(samples[i]) - ic2eq_;
            const double v1 = a1 * ic1eq_ + a2 * v3;
            const double v2 = ic2eq_ + a2 * ic1eq_ + a3 * v3;
            ic1eq_ = 2.0 * v1 - ic1eq_;
            ic2eq_ = 2.0 * v2 - ic2eq_;
            samples[i] = static_cast<float>(v2);
        }
    }

  private:
    std::uint32_t sampleRate_;
    double g_, ic1eq_, ic2eq_;
};

class BypassProcessor : public SoundEngine::IOutputProcessor
{
  public:
    bool prepare(const SoundEngine::RenderSpec &, const SoundEngine::OutputProcessorConfiguration &,
                 std::string *) override
    {
        return true;
    }
    void reset() override
    {
    }
    void process(float *, std::size_t) override
    {
    }
};

class StereoChorus : public SoundEngine::IOutputProcessor
{
  public:
    StereoChorus()
        : sampleRate_(44100), mix_(0.0f), rate_(1.0f), depthMs_(2.0f), phase_(0.0), write_(0), left_(), right_()
    {
        left_.fill(0.0f);
        right_.fill(0.0f);
    }
    bool prepare(const SoundEngine::RenderSpec &spec, const SoundEngine::OutputProcessorConfiguration &configuration,
                 std::string *) override
    {
        sampleRate_ = spec.sampleRate;
        mix_ = clampFloat(configuration.mix, 0.0f, 1.0f);
        rate_ = clampFloat(configuration.rateHz, 0.01f, 20.0f);
        depthMs_ = clampFloat(configuration.depthMs, 0.0f, 20.0f);
        reset();
        return true;
    }
    void reset() override
    {
        left_.fill(0.0f);
        right_.fill(0.0f);
        phase_ = 0.0;
        write_ = 0;
    }
    void process(float *stereo, std::size_t frames) override
    {
        for (std::size_t i = 0; i < frames; ++i)
        {
            const float inLeft = stereo[i * 2], inRight = stereo[i * 2 + 1];
            left_[write_] = inLeft;
            right_[write_] = inRight;
            const double depth = depthMs_ * sampleRate_ * 0.001;
            const double center = 0.018 * sampleRate_;
            const float delayedLeft = read(left_, center + depth * std::sin(phase_));
            const float delayedRight = read(right_, center + depth * std::sin(phase_ + Pi * 0.5));
            stereo[i * 2] = inLeft * (1.0f - mix_) + delayedLeft * mix_;
            stereo[i * 2 + 1] = inRight * (1.0f - mix_) + delayedRight * mix_;
            write_ = (write_ + 1) % left_.size();
            phase_ += TwoPi * rate_ / sampleRate_;
            if (phase_ >= TwoPi)
                phase_ -= TwoPi;
        }
    }

  private:
    float read(const std::array<float, 16384> &buffer, double delay)
    {
        delay = clampDouble(delay, 1.0, buffer.size() - 2.0);
        double position = static_cast<double>(write_) - delay;
        while (position < 0.0)
            position += buffer.size();
        while (position >= buffer.size())
            position -= buffer.size();
        const std::size_t index = static_cast<std::size_t>(position);
        const std::size_t next = (index + 1) % buffer.size();
        const float fraction = static_cast<float>(position - index);
        return buffer[index] + (buffer[next] - buffer[index]) * fraction;
    }
    std::uint32_t sampleRate_;
    float mix_, rate_, depthMs_;
    double phase_;
    std::size_t write_;
    std::array<float, 16384> left_, right_;
};

std::unique_ptr<SoundEngine::ISignalGenerator> createSine()
{
    return std::unique_ptr<SoundEngine::ISignalGenerator>(new ContinuousSine());
}
std::unique_ptr<SoundEngine::ISignalGenerator> createPackets()
{
    return std::unique_ptr<SoundEngine::ISignalGenerator>(new ClockedSinePackets());
}
std::unique_ptr<SoundEngine::ISignalGenerator> createSaw()
{
    return std::unique_ptr<SoundEngine::ISignalGenerator>(new BandlimitedSaw300());
}
std::unique_ptr<SoundEngine::ISignalGenerator> createHarmonicBed()
{
    return std::unique_ptr<SoundEngine::ISignalGenerator>(new HarmonicBed());
}
std::unique_ptr<SoundEngine::IAudioFilter> createBypass()
{
    return std::unique_ptr<SoundEngine::IAudioFilter>(new BypassFilter());
}
std::unique_ptr<SoundEngine::IAudioFilter> createBandPass()
{
    return std::unique_ptr<SoundEngine::IAudioFilter>(new TptBandPass());
}
std::unique_ptr<SoundEngine::IAudioFilter> createLowPass()
{
    return std::unique_ptr<SoundEngine::IAudioFilter>(new ButterworthLowPass());
}
std::unique_ptr<SoundEngine::IOutputProcessor> createProcessorBypass()
{
    return std::unique_ptr<SoundEngine::IOutputProcessor>(new BypassProcessor());
}
std::unique_ptr<SoundEngine::IOutputProcessor> createChorus()
{
    return std::unique_ptr<SoundEngine::IOutputProcessor>(new StereoChorus());
}

void ensureBuiltins()
{
    static const bool registered = []() {
        generatorTable().push_back(SoundEngine::GeneratorDescriptor{
            "continuous_sine", "Continuous phase-tracking sine oscillator", &createSine});
        generatorTable().push_back(
            SoundEngine::GeneratorDescriptor{"clocked_sine_packets", "Clocked windowed sine packets", &createPackets});
        generatorTable().push_back(SoundEngine::GeneratorDescriptor{
            "bandlimited_saw_300", "Band-limited 300 Hz sawtooth Fourier series", &createSaw});
        generatorTable().push_back(SoundEngine::GeneratorDescriptor{
            "legacy_harmonic_bed", "Coherent 300 Hz family with the selected legacy bed spectrum and comb profile",
            &createHarmonicBed});
        generatorTable().push_back(SoundEngine::GeneratorDescriptor{
            "reconstructed_delayed_sine_bursts", "Sample-clocked captured-pitch Tukey sine bursts",
            &SoundEngine::createReconstructedBurstGenerator});
        filterTable().push_back(SoundEngine::FilterDescriptor{"bypass", "Identity filter", &createBypass});
        filterTable().push_back(SoundEngine::FilterDescriptor{
            "tpt_bandpass", "Peak-normalized topology-preserving state-variable band-pass", &createBandPass});
        filterTable().push_back(
            SoundEngine::FilterDescriptor{"butterworth_lowpass", "Two-pole Butterworth low-pass", &createLowPass});
        outputTable().push_back(SoundEngine::OutputProcessorDescriptor{"bypass", "Identity stereo output processor",
                                                                       &createProcessorBypass});
        outputTable().push_back(SoundEngine::OutputProcessorDescriptor{
            "stereo_chorus", "Modulated fractional-delay stereo chorus", &createChorus});
        outputTable().push_back(SoundEngine::OutputProcessorDescriptor{
            "reference_fir_31", "Calibrated 31-tap reconstruction FIR for linked stereo",
            &SoundEngine::createReferenceFirProcessor});
        return true;
    }();
    (void)registered;
}
} // namespace

namespace SoundEngine
{
RenderSpec::RenderSpec()
    : sampleRate(44100), masterGain(0.50f), sourcePeakGain(0.25f), attackMs(8.0f), releaseMs(8.0f), pitchSlewMs(2.0f),
      minimumFrequencyHz(50.0f), maximumFrequencyHz(4200.0f)
{
}
VoiceControl::VoiceControl() : frequencyHz(440.0f), gain(1.0f), active(false)
{
}
VoiceControl::VoiceControl(float frequency, float amplitude, bool enabled)
    : frequencyHz(frequency), gain(amplitude), active(enabled)
{
}
GeneratorConfiguration::GeneratorConfiguration()
    : id("continuous_sine"), packetClockHz(300.0f), packetDurationSeconds(2.0f / 300.0f), carrierCycles(2.0f), tukeyFraction(0.0256667726f),
      durationMode(PacketDuration::FixedSeconds), window(PacketWindow::Gaussian), phase(PacketPhase::ResetAtBirth),
      normalization(PacketNormalization::Peak), sawMaximumHz(17100.0f), harmonicExponent(1.5f),
      includeSub300HzBed(false), bedComb()
{
}
FilterConfiguration::FilterConfiguration() : id(), cutoffHz(10000.0f), bandwidthHz(200.0f), q(4.0f), constantQ(true)
{
}
VoicePipelineDescription::VoicePipelineDescription() : source(), filters(), filterCount(0)
{
}
OutputProcessorConfiguration::OutputProcessorConfiguration() : id("bypass"), mix(0.0f), rateHz(1.0f), depthMs(2.0f)
{
}
PipelineDescription::PipelineDescription()
    : voices(), bedEnabled(false), bed(), bedGain(0.0f), outputProcessors(), outputProcessorCount(0)
{
}

bool ComponentRegistry::registerGenerator(const GeneratorDescriptor &descriptor)
{
    ensureBuiltins();
    if (descriptor.id == nullptr || descriptor.create == nullptr || *descriptor.id == '\0' ||
        findGenerator(descriptor.id))
        return false;
    generatorTable().push_back(descriptor);
    return true;
}
bool ComponentRegistry::registerFilter(const FilterDescriptor &descriptor)
{
    ensureBuiltins();
    if (descriptor.id == nullptr || descriptor.create == nullptr || *descriptor.id == '\0' || findFilter(descriptor.id))
        return false;
    filterTable().push_back(descriptor);
    return true;
}
bool ComponentRegistry::registerOutputProcessor(const OutputProcessorDescriptor &descriptor)
{
    ensureBuiltins();
    if (descriptor.id == nullptr || descriptor.create == nullptr || *descriptor.id == '\0' ||
        findOutputProcessor(descriptor.id))
        return false;
    outputTable().push_back(descriptor);
    return true;
}
const GeneratorDescriptor *ComponentRegistry::findGenerator(const std::string &id)
{
    ensureBuiltins();
    for (std::size_t i = 0; i < generatorTable().size(); ++i)
        if (id == generatorTable()[i].id)
            return &generatorTable()[i];
    return nullptr;
}
const FilterDescriptor *ComponentRegistry::findFilter(const std::string &id)
{
    ensureBuiltins();
    for (std::size_t i = 0; i < filterTable().size(); ++i)
        if (id == filterTable()[i].id)
            return &filterTable()[i];
    return nullptr;
}
const OutputProcessorDescriptor *ComponentRegistry::findOutputProcessor(const std::string &id)
{
    ensureBuiltins();
    for (std::size_t i = 0; i < outputTable().size(); ++i)
        if (id == outputTable()[i].id)
            return &outputTable()[i];
    return nullptr;
}
const std::vector<GeneratorDescriptor> &ComponentRegistry::generators()
{
    ensureBuiltins();
    return generatorTable();
}
const std::vector<FilterDescriptor> &ComponentRegistry::filters()
{
    ensureBuiltins();
    return filterTable();
}
const std::vector<OutputProcessorDescriptor> &ComponentRegistry::outputProcessors()
{
    ensureBuiltins();
    return outputTable();
}

struct ComposableRenderer::Lane
{
    std::unique_ptr<ISignalGenerator> source;
    std::array<std::unique_ptr<IAudioFilter>, MaxFiltersPerVoice> filters;
    std::size_t filterCount;
    Lane() : source(), filters(), filterCount(0)
    {
    }
};

ComposableRenderer::ComposableRenderer()
    : spec_(), description_(), lanes_(), processors_(), processorCount_(0), controls_(), laneScratch_(),
      amplitudeScratch_(), frequencyScratch_(), excitationScratch_(), stereoScratch_(), outputScratch_(),
      masterGainScratch_(), sourcePeakGainScratch_(), gateLevels_(), smoothedGains_(), smoothedFrequencies_(),
      sampleClock_(0), masterGain_(0.5f), targetMasterGain_(0.5f), sourcePeakGain_(0.25f), targetSourcePeakGain_(0.25f),
      prepared_(false)
{
    gateLevels_.fill(0.0f);
    smoothedGains_.fill(1.0f);
    smoothedFrequencies_.fill(440.0f);
}

ComposableRenderer::~ComposableRenderer() = default;

bool ComposableRenderer::prepare(const RenderSpec &spec, const PipelineDescription &description, std::string *error)
{
    prepared_ = false;
    if (spec.sampleRate < 8000 || spec.sampleRate > 192000 || description.outputProcessorCount > MaxOutputProcessors)
    {
        if (error)
            *error = "Invalid sample rate or output processor count.";
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i)
        if (description.voices[i].filterCount > MaxFiltersPerVoice)
        {
            if (error)
                *error = "A voice exceeds the four-filter limit.";
            return false;
        }
    if (description.bed.filterCount > MaxFiltersPerVoice)
    {
        if (error)
            *error = "The bed exceeds the four-filter limit.";
        return false;
    }
    spec_ = spec;
    spec_.masterGain = clampFloat(spec.masterGain, 0.0f, 1.0f);
    spec_.sourcePeakGain = clampFloat(spec.sourcePeakGain, 0.0f, 4.0f);
    sourcePeakGain_ = targetSourcePeakGain_ = spec_.sourcePeakGain;
    masterGain_ = targetMasterGain_ = spec_.masterGain;
    spec_.attackMs = clampFloat(spec.attackMs, 0.0f, 10000.0f);
    spec_.releaseMs = clampFloat(spec.releaseMs, 0.0f, 10000.0f);
    spec_.pitchSlewMs = clampFloat(spec.pitchSlewMs, 0.0f, 10000.0f);
    spec_.minimumFrequencyHz = clampFloat(spec.minimumFrequencyHz, 1.0f, spec.sampleRate * 0.45f);
    spec_.maximumFrequencyHz = clampFloat(spec.maximumFrequencyHz, spec_.minimumFrequencyHz, spec.sampleRate * 0.45f);
    description_ = description;
    for (std::size_t i = 0; i < lanes_.size(); ++i)
        lanes_[i].reset();
    lanes_[0].reset(new Lane());
    lanes_[1].reset(new Lane());
    if (description.bedEnabled)
        lanes_[2].reset(new Lane());
    for (std::size_t i = 0; i < 2; ++i)
        if (!prepareLane(*lanes_[i], description.voices[i], error))
            return false;
    if (description.bedEnabled && !prepareLane(*lanes_[2], description.bed, error))
        return false;
    processorCount_ = description.outputProcessorCount;
    for (std::size_t i = 0; i < processorCount_; ++i)
    {
        const OutputProcessorDescriptor *descriptor =
            ComponentRegistry::findOutputProcessor(description.outputProcessors[i].id);
        if (descriptor == nullptr)
        {
            if (error)
                *error = "Unknown output processor: " + description.outputProcessors[i].id;
            return false;
        }
        processors_[i] = descriptor->create();
        if (!processors_[i] || !processors_[i]->prepare(spec_, description.outputProcessors[i], error))
            return false;
    }
    controls_[0] = VoiceControl();
    controls_[1] = VoiceControl();
    controls_[2] = VoiceControl();
    gateLevels_.fill(0.0f);
    smoothedGains_.fill(1.0f);
    for (std::size_t i = 0; i < 3; ++i)
        smoothedFrequencies_[i] =
            clampFloat(controls_[i].frequencyHz, spec_.minimumFrequencyHz, spec_.maximumFrequencyHz);
    sampleClock_ = 0;
    reset();
    prepared_ = true;
    return true;
}

bool ComposableRenderer::prepareLane(Lane &lane, const VoicePipelineDescription &description, std::string *error)
{
    const GeneratorDescriptor *source = ComponentRegistry::findGenerator(description.source.id);
    if (source == nullptr)
    {
        if (error)
            *error = "Unknown source generator: " + description.source.id;
        return false;
    }
    lane.source = source->create();
    if (!lane.source || !lane.source->prepare(spec_, description.source, error))
        return false;
    lane.filterCount = description.filterCount;
    for (std::size_t i = 0; i < lane.filterCount; ++i)
    {
        const FilterDescriptor *filter = ComponentRegistry::findFilter(description.filters[i].id);
        if (filter == nullptr)
        {
            if (error)
                *error = "Unknown filter: " + description.filters[i].id;
            return false;
        }
        lane.filters[i] = filter->create();
        if (!lane.filters[i] || !lane.filters[i]->prepare(spec_, description.filters[i], error))
            return false;
    }
    return true;
}

void ComposableRenderer::setVoiceControl(std::size_t voice, const VoiceControl &control)
{
    if (voice >= 2)
        return;
    controls_[voice] = control;
}
void ComposableRenderer::setBedControl(const VoiceControl &control)
{
    controls_[2] = control;
}
void ComposableRenderer::setMasterGain(float gain)
{
    if (std::isfinite(gain))
        targetMasterGain_ = clampFloat(gain, 0.0f, 1.0f);
}
void ComposableRenderer::setSourcePeakGain(float gain)
{
    if (std::isfinite(gain))
        targetSourcePeakGain_ = clampFloat(gain, 0.0f, 4.0f);
}

void ComposableRenderer::renderLane(Lane &lane, const VoiceControl &control, float *output, std::size_t frames,
                                    std::uint64_t firstSample)
{
    const std::size_t laneIndex = (&lane == lanes_[0].get()) ? 0 : ((&lane == lanes_[1].get()) ? 1 : 2);
    float targetFrequency = std::isfinite(control.frequencyHz) ? control.frequencyHz : 440.0f;
    targetFrequency = clampFloat(targetFrequency, spec_.minimumFrequencyHz, spec_.maximumFrequencyHz);
    const float targetGain = std::isfinite(control.gain) ? clampFloat(control.gain, 0.0f, 4.0f) : 0.0f;
    const float frequencyAlpha = alphaFor(spec_.pitchSlewMs, spec_.sampleRate);
    const float attackAlpha = alphaFor(spec_.attackMs, spec_.sampleRate);
    const float releaseAlpha = alphaFor(spec_.releaseMs, spec_.sampleRate);
    const float gainAlpha = alphaFor(10.0f, spec_.sampleRate);
    for (std::size_t i = 0; i < frames; ++i)
    {
        smoothedFrequencies_[laneIndex] += (targetFrequency - smoothedFrequencies_[laneIndex]) * frequencyAlpha;
        smoothedGains_[laneIndex] += (targetGain - smoothedGains_[laneIndex]) * gainAlpha;
        const float targetGate = control.active ? 1.0f : 0.0f;
        gateLevels_[laneIndex] +=
            (targetGate - gateLevels_[laneIndex]) * (targetGate > gateLevels_[laneIndex] ? attackAlpha : releaseAlpha);
        frequencyScratch_[laneIndex][i] = smoothedFrequencies_[laneIndex];
        excitationScratch_[laneIndex][i] = control.active;
        outputScratch_[i * 2] = 0.0f;
        outputScratch_[i * 2 + 1] = 0.0f;
        laneScratch_[laneIndex][i] = 0.0f;
        amplitudeScratch_[laneIndex][i] =
            gateLevels_[laneIndex] * smoothedGains_[laneIndex] * sourcePeakGainScratch_[i];
    }
    RenderContext context = {frequencyScratch_[laneIndex].data(), excitationScratch_[laneIndex].data(), frames,
                             spec_.sampleRate, firstSample};
    lane.source->render(laneScratch_[laneIndex].data(), context);
    for (std::size_t i = 0; i < lane.filterCount; ++i)
        lane.filters[i]->process(laneScratch_[laneIndex].data(), context);
    for (std::size_t i = 0; i < frames; ++i)
        output[i] = laneScratch_[laneIndex][i] * amplitudeScratch_[laneIndex][i];
}

void ComposableRenderer::render(float *output, std::size_t frameCount)
{
    if (output == nullptr || !prepared_)
        return;
    const float masterAlpha = alphaFor(10.0f, spec_.sampleRate);
    const float sourceAlpha = alphaFor(10.0f, spec_.sampleRate);
    std::size_t completed = 0;
    while (completed < frameCount)
    {
        const std::size_t frames = (std::min)(PipelineScratchFrames, frameCount - completed);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            stereoScratch_[frame * 2] = 0.0f;
            stereoScratch_[frame * 2 + 1] = 0.0f;
            masterGain_ += (targetMasterGain_ - masterGain_) * masterAlpha;
            sourcePeakGain_ += (targetSourcePeakGain_ - sourcePeakGain_) * sourceAlpha;
            masterGainScratch_[frame] = masterGain_;
            sourcePeakGainScratch_[frame] = sourcePeakGain_;
        }
        for (std::size_t laneIndex = 0; laneIndex < 2; ++laneIndex)
        {
            renderLane(*lanes_[laneIndex], controls_[laneIndex], outputScratch_.data(), frames, sampleClock_);
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                const float sample = outputScratch_[frame];
                stereoScratch_[frame * 2] += sample * static_cast<float>(ConstantPower);
                stereoScratch_[frame * 2 + 1] += sample * static_cast<float>(ConstantPower);
            }
        }
        if (description_.bedEnabled)
        {
            renderLane(*lanes_[2], controls_[2], outputScratch_.data(), frames, sampleClock_);
            for (std::size_t frame = 0; frame < frames; ++frame)
            {
                const float sample = outputScratch_[frame] * description_.bedGain;
                stereoScratch_[frame * 2] += sample;
                stereoScratch_[frame * 2 + 1] += sample;
            }
        }
        for (std::size_t processor = 0; processor < processorCount_; ++processor)
            processors_[processor]->process(stereoScratch_.data(), frames);
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            output[(completed + frame) * 2] = stereoScratch_[frame * 2] * masterGainScratch_[frame];
            output[(completed + frame) * 2 + 1] = stereoScratch_[frame * 2 + 1] * masterGainScratch_[frame];
        }
        sampleClock_ += frames;
        completed += frames;
    }
}

void ComposableRenderer::reset()
{
    for (std::size_t i = 0; i < lanes_.size(); ++i)
        if (lanes_[i])
        {
            if (lanes_[i]->source)
                lanes_[i]->source->reset();
            for (std::size_t filter = 0; filter < lanes_[i]->filterCount; ++filter)
                lanes_[i]->filters[filter]->reset();
        }
    for (std::size_t i = 0; i < processorCount_; ++i)
        if (processors_[i])
            processors_[i]->reset();
    gateLevels_.fill(0.0f);
    smoothedGains_.fill(1.0f);
    for (std::size_t i = 0; i < controls_.size(); ++i)
    {
        controls_[i] = VoiceControl();
        smoothedFrequencies_[i] =
            clampFloat(controls_[i].frequencyHz, spec_.minimumFrequencyHz, spec_.maximumFrequencyHz);
    }
    masterGain_ = targetMasterGain_;
    sourcePeakGain_ = targetSourcePeakGain_;
    sampleClock_ = 0;
}
std::uint64_t ComposableRenderer::sampleClock() const
{
    return sampleClock_;
}
bool ComposableRenderer::prepared() const
{
    return prepared_;
}

const char *continuousSineGeneratorId()
{
    return "continuous_sine";
}
const char *clockedSinePacketGeneratorId()
{
    return "clocked_sine_packets";
}
const char *bandlimitedSaw300GeneratorId()
{
    return "bandlimited_saw_300";
}
const char *harmonicBedGeneratorId()
{
    return "legacy_harmonic_bed";
}
const char *bypassFilterId()
{
    return "bypass";
}
const char *tptBandPassFilterId()
{
    return "tpt_bandpass";
}
const char *butterworthLowPassFilterId()
{
    return "butterworth_lowpass";
}
const char *stereoChorusProcessorId()
{
    return "stereo_chorus";
}
const char *reconstructedBurstGeneratorId()
{
    return "reconstructed_delayed_sine_bursts";
}
const char *referenceFirProcessorId()
{
    return "reference_fir_31";
}
} // namespace SoundEngine
