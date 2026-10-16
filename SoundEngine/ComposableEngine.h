#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SoundEngine.h"

namespace SoundEngine
{
static const std::size_t MaxFiltersPerVoice = 4;
static const std::size_t MaxOutputProcessors = 4;
static const std::size_t PipelineScratchFrames = 256;

enum class PacketWindow
{
    Hann,
    Gaussian
};
enum class PacketPhase
{
    ResetAtBirth,
    ContinuousReference
};
enum class PacketNormalization
{
    Peak,
    WindowEnergy
};
enum class PacketDuration
{
    FixedSeconds,
    CarrierCycles
};

struct RenderSpec
{
    std::uint32_t sampleRate;
    float masterGain;
    float sourcePeakGain;
    float attackMs;
    float releaseMs;
    float pitchSlewMs;
    float minimumFrequencyHz;
    float maximumFrequencyHz;
    RenderSpec();
};

struct VoiceControl
{
    float frequencyHz;
    float gain;
    bool active;
    VoiceControl();
    VoiceControl(float frequency, float amplitude, bool enabled = true);
};

struct GeneratorConfiguration
{
    std::string id;
    float packetClockHz;
    float packetDurationSeconds;
    float carrierCycles;
    float tukeyFraction;
    PacketDuration durationMode;
    PacketWindow window;
    PacketPhase phase;
    PacketNormalization normalization;
    float sawMaximumHz;
    float harmonicExponent;
    bool includeSub300HzBed;
    BedCombFilterParameters bedComb;
    GeneratorConfiguration();
};

struct FilterConfiguration
{
    std::string id;
    float cutoffHz;
    float bandwidthHz;
    float q;
    bool constantQ;
    FilterConfiguration();
};

struct VoicePipelineDescription
{
    GeneratorConfiguration source;
    std::array<FilterConfiguration, MaxFiltersPerVoice> filters;
    std::size_t filterCount;
    VoicePipelineDescription();
};

struct OutputProcessorConfiguration
{
    std::string id;
    float mix;
    float rateHz;
    float depthMs;
    OutputProcessorConfiguration();
};

struct PipelineDescription
{
    std::array<VoicePipelineDescription, 2> voices;
    bool bedEnabled;
    VoicePipelineDescription bed;
    float bedGain;
    std::array<OutputProcessorConfiguration, MaxOutputProcessors> outputProcessors;
    std::size_t outputProcessorCount;
    PipelineDescription();
};

struct RenderContext
{
    const float *frequencyHz;
    const bool *excitationEnabled;
    std::size_t frameCount;
    std::uint32_t sampleRate;
    std::uint64_t firstSample;
};

class ISignalGenerator
{
  public:
    virtual ~ISignalGenerator()
    {
    }
    virtual bool prepare(const RenderSpec &spec, const GeneratorConfiguration &configuration, std::string *error) = 0;
    virtual void reset() = 0;
    virtual void render(float *monoOutput, const RenderContext &context) = 0;
};

class IAudioFilter
{
  public:
    virtual ~IAudioFilter()
    {
    }
    virtual bool prepare(const RenderSpec &spec, const FilterConfiguration &configuration, std::string *error) = 0;
    virtual void reset() = 0;
    virtual void process(float *monoSamples, const RenderContext &context) = 0;
};

class IOutputProcessor
{
  public:
    virtual ~IOutputProcessor()
    {
    }
    virtual bool prepare(const RenderSpec &spec, const OutputProcessorConfiguration &configuration,
                         std::string *error) = 0;
    virtual void reset() = 0;
    virtual void process(float *interleavedStereo, std::size_t frameCount) = 0;
};

typedef std::unique_ptr<ISignalGenerator> (*GeneratorFactory)();
typedef std::unique_ptr<IAudioFilter> (*FilterFactory)();
typedef std::unique_ptr<IOutputProcessor> (*OutputProcessorFactory)();

struct GeneratorDescriptor
{
    const char *id;
    const char *description;
    GeneratorFactory create;
};
struct FilterDescriptor
{
    const char *id;
    const char *description;
    FilterFactory create;
};
struct OutputProcessorDescriptor
{
    const char *id;
    const char *description;
    OutputProcessorFactory create;
};

// Register components before preparing pipelines. Registry lookup and factories
// are used only during prepare(), never from render().
class ComponentRegistry
{
  public:
    static bool registerGenerator(const GeneratorDescriptor &descriptor);
    static bool registerFilter(const FilterDescriptor &descriptor);
    static bool registerOutputProcessor(const OutputProcessorDescriptor &descriptor);
    static const GeneratorDescriptor *findGenerator(const std::string &id);
    static const FilterDescriptor *findFilter(const std::string &id);
    static const OutputProcessorDescriptor *findOutputProcessor(const std::string &id);
    static const std::vector<GeneratorDescriptor> &generators();
    static const std::vector<FilterDescriptor> &filters();
    static const std::vector<OutputProcessorDescriptor> &outputProcessors();
};

// Prepared pipeline with two constant-power comparison lanes, optional bed, and
// a shared ordered output chain. Calls to controls and render belong to one owner
// thread. Topology edits require another prepare().
class ComposableRenderer
{
  public:
    ComposableRenderer();
    ~ComposableRenderer();
    bool prepare(const RenderSpec &spec, const PipelineDescription &description, std::string *error = nullptr);
    void setVoiceControl(std::size_t voice, const VoiceControl &control);
    void setBedControl(const VoiceControl &control);
    void setMasterGain(float gain);
    void setSourcePeakGain(float gain);
    void render(float *interleavedStereoOutput, std::size_t frameCount);
    void reset();
    std::uint64_t sampleClock() const;
    bool prepared() const;

  private:
    struct Lane;
    bool prepareLane(Lane &lane, const VoicePipelineDescription &description, std::string *error);
    void renderLane(Lane &lane, const VoiceControl &control, float *output, std::size_t frames,
                    std::uint64_t firstSample);
    RenderSpec spec_;
    PipelineDescription description_;
    std::array<std::unique_ptr<Lane>, 3> lanes_;
    std::array<std::unique_ptr<IOutputProcessor>, MaxOutputProcessors> processors_;
    std::size_t processorCount_;
    std::array<VoiceControl, 3> controls_;
    std::array<float, PipelineScratchFrames> laneScratch_[3];
    std::array<float, PipelineScratchFrames> amplitudeScratch_[3];
    std::array<float, PipelineScratchFrames> frequencyScratch_[3];
    std::array<bool, PipelineScratchFrames> excitationScratch_[3];
    std::array<float, PipelineScratchFrames * 2> stereoScratch_;
    std::array<float, PipelineScratchFrames * 2> outputScratch_;
    std::array<float, PipelineScratchFrames> masterGainScratch_;
    std::array<float, PipelineScratchFrames> sourcePeakGainScratch_;
    std::array<float, 3> gateLevels_;
    std::array<float, 3> smoothedGains_;
    std::array<float, 3> smoothedFrequencies_;
    std::uint64_t sampleClock_;
    float masterGain_, targetMasterGain_;
    float sourcePeakGain_, targetSourcePeakGain_;
    bool prepared_;
};

// Stable built-in component identifiers for the first experiment set.
const char *continuousSineGeneratorId();
const char *clockedSinePacketGeneratorId();
const char *bandlimitedSaw300GeneratorId();
const char *harmonicBedGeneratorId();
const char *bypassFilterId();
const char *tptBandPassFilterId();
const char *butterworthLowPassFilterId();
const char *stereoChorusProcessorId();
const char *reconstructedBurstGeneratorId();
const char *referenceFirProcessorId();
std::unique_ptr<ISignalGenerator> createReconstructedBurstGenerator();
std::unique_ptr<IOutputProcessor> createReferenceFirProcessor();
} // namespace SoundEngine
