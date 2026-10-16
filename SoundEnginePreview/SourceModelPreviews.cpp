#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "ComposableEngine.h"
#include "SortingLogic.h"
#include "SoundEngine.h"
#include "SourceModelPreviews.h"
#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
const std::uint32_t Rate = 44100, Seed = 0x4A475353u;
const std::size_t ScanReleaseFrames = Rate / 2;
const double DesiredRms = 0.06309573444801933, PeakCeiling = 0.8912509381337456;

struct Profile
{
    std::string id, model, label;
    SoundEngine::RenderSpec spec;
    SoundEngine::GeneratorConfiguration source;
    std::array<SoundEngine::FilterConfiguration, SoundEngine::MaxFiltersPerVoice> filters;
    std::size_t filterCount;
    std::string parametersJson;
    Profile() : id(), model(), label(), spec(), source(), filters(), filterCount(0), parametersJson("{}")
    {
    }
};
struct Event
{
    std::uint64_t frame;
    float firstHz, secondHz;
    bool firstActive, secondActive;
    int heldIndex;
    Event() : frame(0), firstHz(440), secondHz(440), firstActive(false), secondActive(false), heldIndex(-1)
    {
    }
};
struct Comparison
{
    float first, second;
    int held;
    Comparison(float a = 0.5f, float b = 0.5f, int h = -1) : first(a), second(b), held(h)
    {
    }
};
struct Signal
{
    std::string id, profile, model, scenario, group, description, parametersJson, renderSpecJson;
    std::uint32_t seed, sampleRate;
    std::vector<Event> events;
    std::vector<float> raw;
    double dc, acRms, peak, listeningGain;
    Signal()
        : id(), profile(), model(), scenario(), group(), description(), parametersJson("{}"), renderSpecJson("{}"),
          seed(Seed), sampleRate(Rate), events(), raw(), dc(0), acRms(0), peak(0), listeningGain(1)
    {
    }
};

float valueToFrequency(float x)
{
    return 50.0f + (std::max)(0.0f, (std::min)(1.0f, x)) * 4150.0f;
}
std::string num(double v)
{
    std::ostringstream s;
    s << std::setprecision(8) << v;
    return s.str();
}
std::string escapeJson(const std::string &s)
{
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '"' || s[i] == '\\')
            r.push_back('\\');
        if (s[i] == '\n')
            r += "\\n";
        else if (s[i] != '\r')
            r.push_back(s[i]);
    }
    return r;
}
std::string safeName(const std::string &s)
{
    std::string r = s;
    for (std::size_t i = 0; i < r.size(); ++i)
        if (!((r[i] >= 'a' && r[i] <= 'z') || (r[i] >= 'A' && r[i] <= 'Z') || (r[i] >= '0' && r[i] <= '9') ||
              r[i] == '-' || r[i] == '_'))
            r[i] = '_';
    return r;
}
void defaults(Profile &p, const char *sourceId)
{
    p.spec.sampleRate = Rate;
    p.spec.masterGain = .5f;
    p.spec.sourcePeakGain = .25f;
    p.spec.attackMs = 8;
    p.spec.releaseMs = 8;
    p.spec.pitchSlewMs = 2;
    p.spec.minimumFrequencyHz = 50;
    p.spec.maximumFrequencyHz = 4200;
    p.source.id = sourceId;
    p.source.sawMaximumHz = 17100;
    p.source.packetClockHz = 300;
    p.source.packetDurationSeconds = 2.0f / 300;
    p.source.carrierCycles = 2;
    p.source.durationMode = SoundEngine::PacketDuration::FixedSeconds;
    p.source.window = SoundEngine::PacketWindow::Gaussian;
    p.source.phase = SoundEngine::PacketPhase::ResetAtBirth;
    p.source.normalization = SoundEngine::PacketNormalization::Peak;
    p.source.harmonicExponent = 1.5f;
    p.source.includeSub300HzBed = false;
    p.source.bedComb = SoundEngine::BedCombFilterParameters();
    p.source.bedComb.enabled = true;
    p.source.bedComb.applyPhase = true;
    p.source.bedComb.delayMicroseconds = 222.222f;
    p.source.bedComb.phaseOffsetDegrees = 12;
    p.source.bedComb.wetAtLowFrequency = .95f;
    p.source.bedComb.wetAtHighFrequency = .90f;
    p.source.bedComb.fadeStartHz = 6600;
    p.source.bedComb.fadeEndHz = 15600;
    p.source.bedComb.fadeExponent = 3;
}
std::vector<Profile> makeProfiles()
{
    std::vector<Profile> out;
    const float slew[] = {0, 2, 8}, env[] = {2, 8};
    for (int s = 0; s < 3; ++s)
        for (int e = 0; e < 2; ++e)
        {
            Profile p;
            defaults(p, SoundEngine::continuousSineGeneratorId());
            p.model = "continuous_sine";
            p.spec.pitchSlewMs = slew[s];
            p.spec.attackMs = p.spec.releaseMs = env[e];
            p.id = "sine_slew" + num(slew[s]) + "_a" + num(env[e]) + "_r" + num(env[e]);
            p.label = "Continuous sine, pitch slew " + num(slew[s]) + " ms, envelope " + num(env[e]) + "/" +
                      num(env[e]) + " ms";
            p.parametersJson = "{\"source\":\"continuous_sine\",\"pitchSlewMs\":" + num(slew[s]) +
                               ",\"attackMs\":" + num(env[e]) + ",\"releaseMs\":" + num(env[e]) +
                               ",\"masterGain\":0.5,\"sourcePeakGain\":0.25,\"mapping\":\"linear_50_4200_hz\"}";
            out.push_back(p);
        }
    const SoundEngine::PacketWindow windows[] = {SoundEngine::PacketWindow::Hann, SoundEngine::PacketWindow::Gaussian};
    const char *wn[] = {"hann", "gaussian"};
    const SoundEngine::PacketPhase phases[] = {SoundEngine::PacketPhase::ResetAtBirth,
                                               SoundEngine::PacketPhase::ContinuousReference};
    const char *pn[] = {"reset", "continuous"};
    const SoundEngine::PacketNormalization norms[] = {SoundEngine::PacketNormalization::Peak,
                                                      SoundEngine::PacketNormalization::WindowEnergy};
    const char *nn[] = {"peak", "energy"};
    const float periods[] = {.5f, 1, 2, 4};
    for (int w = 0; w < 2; ++w)
        for (int d = 0; d < 4; ++d)
            for (int ph = 0; ph < 2; ++ph)
                for (int n = 0; n < 2; ++n)
                {
                    Profile p;
                    defaults(p, SoundEngine::clockedSinePacketGeneratorId());
                    p.model = "clocked_sine_packets";
                    p.source.window = windows[w];
                    p.source.phase = phases[ph];
                    p.source.normalization = norms[n];
                    p.source.packetDurationSeconds = periods[d] / 300.0f;
                    p.id =
                        "packet_fixed_" + std::string(wn[w]) + "_" + num(periods[d]) + "period_" + pn[ph] + "_" + nn[n];
                    p.label = "Clocked sine packet, " + std::string(wn[w]) + ", " + num(periods[d]) +
                              " clock periods, " + pn[ph] + " phase, " + nn[n] + " normalization";
                    p.parametersJson =
                        "{\"source\":\"clocked_sine_packets\",\"clockHz\":300,\"window\":\"" + std::string(wn[w]) +
                        "\",\"durationMode\":\"fixed_seconds\",\"durationSeconds\":" +
                        num(p.source.packetDurationSeconds) + ",\"phase\":\"" + pn[ph] + "\",\"normalization\":\"" +
                        nn[n] + "\",\"masterGain\":0.5,\"sourcePeakGain\":0.25}";
                    out.push_back(p);
                }
    const float cycles[] = {2, 4, 8};
    for (int w = 0; w < 2; ++w)
        for (int c = 0; c < 3; ++c)
            for (int n = 0; n < 2; ++n)
            {
                Profile p;
                defaults(p, SoundEngine::clockedSinePacketGeneratorId());
                p.model = "clocked_sine_packets";
                p.source.window = windows[w];
                p.source.phase = SoundEngine::PacketPhase::ResetAtBirth;
                p.source.normalization = norms[n];
                p.source.durationMode = SoundEngine::PacketDuration::CarrierCycles;
                p.source.carrierCycles = cycles[c];
                p.id = "packet_cycles_" + std::string(wn[w]) + "_" + num(cycles[c]) + "cycle_" + nn[n];
                p.label = "Clocked packet, " + std::string(wn[w]) + ", " + num(cycles[c]) +
                          " carrier cycles (bounded), reset, " + nn[n] + " normalization";
                p.parametersJson =
                    "{\"source\":\"clocked_sine_packets\",\"clockHz\":300,\"window\":\"" + std::string(wn[w]) +
                    "\",\"durationMode\":\"carrier_cycles\",\"carrierCycles\":" + num(cycles[c]) +
                    ",\"durationClampClockPeriods\":[0.5,4],\"phase\":\"reset\",\"normalization\":\"" + nn[n] + "\"}";
                out.push_back(p);
            }
    const float clocks[] = {250, 350};
    for (int c = 0; c < 2; ++c)
    {
        Profile p;
        defaults(p, SoundEngine::clockedSinePacketGeneratorId());
        p.model = "clocked_sine_packets";
        p.source.packetClockHz = clocks[c];
        p.source.packetDurationSeconds = 2.0f / 300;
        p.id = "packet_clock_" + num(clocks[c]) + "hz";
        p.label = "Clocked packet, " + num(clocks[c]) + " Hz, fixed 6.667 ms packet";
        p.parametersJson =
            "{\"source\":\"clocked_sine_packets\",\"clockHz\":" + num(clocks[c]) +
            ",\"durationSeconds\":0.006666667,\"window\":\"gaussian\",\"phase\":\"reset\",\"normalization\":\"peak\"}";
        out.push_back(p);
    }
    const float widths[] = {100, 200, 400}, qs[] = {2, 4, 8}, cuts[] = {0, 6000, 10000};
    const char *cutNames[] = {"bypass", "6khz", "10khz"};
    for (int mode = 0; mode < 2; ++mode)
        for (int b = 0; b < 3; ++b)
            for (int lp = 0; lp < 3; ++lp)
            {
                Profile p;
                defaults(p, SoundEngine::bandlimitedSaw300GeneratorId());
                p.model = "filtered_sawtooth";
                p.filterCount = cuts[lp] == 0 ? 1 : 2;
                p.filters[0].id = SoundEngine::tptBandPassFilterId();
                p.filters[0].constantQ = mode == 1;
                p.filters[0].q = mode ? qs[b] : 4;
                p.filters[0].bandwidthHz = mode ? 200 : widths[b];
                p.filters[0].cutoffHz = 1000;
                if (p.filterCount == 2)
                {
                    p.filters[1].id = SoundEngine::butterworthLowPassFilterId();
                    p.filters[1].cutoffHz = cuts[lp];
                }
                const std::string range = mode ? "q" + num(qs[b]) : "bw" + num(widths[b]);
                p.id = "saw_" + range + "_lp" + cutNames[lp];
                p.label = "Filtered 300 Hz saw, " + (mode ? ("Q=" + num(qs[b])) : (num(widths[b]) + " Hz bandwidth")) +
                          ", low pass " + cutNames[lp];
                p.parametersJson = "{\"source\":\"bandlimited_saw_300\",\"partialCeilingHz\":17100,\"bandPass\":\"tpt_"
                                   "bandpass\",\"bandwidthMode\":\"" +
                                   std::string(mode ? "constant_q" : "fixed_hz") +
                                   "\",\"bandwidthHz\":" + num(p.filters[0].bandwidthHz) +
                                   ",\"q\":" + num(p.filters[0].q) + ",\"lowPassHz\":" + num(cuts[lp]) + "}";
                out.push_back(p);
            }
    return out;
}
bool ensureDir(const std::string &p)
{
    if (CreateDirectoryA(p.c_str(), nullptr))
        return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}
bool makeTrace(std::vector<Comparison> &out)
{
    std::vector<sortinglogic::SortItem> input;
    for (std::size_t i = 0; i < 80; ++i)
        input.push_back(sortinglogic::SortItem(i, float(i + 1)));
    const sortinglogic::SortTrace trace =
        sortinglogic::SortTraceBuilder::build(input, sortinglogic::SortDirection::Ascending, Seed);
    std::vector<float> value(80);
    for (std::size_t i = 0; i < input.size(); ++i)
        value[static_cast<std::size_t>(input[i].stableId)] = input[i].value;
    float low = input.front().value, high = low;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        low = (std::min)(low, input[i].value);
        high = (std::max)(high, input[i].value);
    }
    std::uint64_t held = sortinglogic::NoItemId;
    for (std::size_t i = 0; i < trace.events.size(); ++i)
    {
        const sortinglogic::SemanticEvent &e = trace.events[i];
        if (e.type == sortinglogic::SemanticEventType::HoldKey)
            held = e.item.stableId;
        else if (e.type == sortinglogic::SemanticEventType::Compare)
            out.push_back(Comparison(SoundEngine::normalizeValue(value[static_cast<std::size_t>(e.firstItemId)], low, high),
                                     SoundEngine::normalizeValue(value[static_cast<std::size_t>(e.secondItemId)], low, high),
                                     e.firstItemId == held ? 0 : (e.secondItemId == held ? 1 : -1)));
        else if (e.type == sortinglogic::SemanticEventType::PlaceKey)
            held = sortinglogic::NoItemId;
    }
    return out.size() == 79;
}
bool makeSelectionSortTrace(std::vector<Comparison> &out)
{
    std::vector<float> values;
    for (std::size_t i = 0; i < 80; ++i)
        values.push_back(float(i + 1));

    std::mt19937 random(Seed);
    std::shuffle(values.begin(), values.end(), random);

    const float low = 1.0f, high = 80.0f;
    for (std::size_t i = 0; i + 1 < values.size(); ++i)
    {
        std::size_t minimum = i;
        for (std::size_t j = i + 1; j < values.size(); ++j)
        {
            out.push_back(Comparison(SoundEngine::normalizeValue(values[minimum], low, high),
                                     SoundEngine::normalizeValue(values[j], low, high)));
            if (values[j] < values[minimum])
                minimum = j;
        }
        std::swap(values[i], values[minimum]);
    }

    return out.size() == 80 * 79 / 2 && std::is_sorted(values.begin(), values.end());
}
bool isRetainedProfile(const Profile &profile)
{
    if (profile.id.find("packet_clock_") == 0)
        return false;
    if (profile.id.find("packet_fixed_") == 0)
        return profile.id.find("_4period_") != std::string::npos;
    if (profile.id.find("packet_cycles_") == 0)
        return profile.id.find("_8cycle_") != std::string::npos;
    return true;
}
std::vector<Comparison> sample(const std::vector<Comparison> &all, std::size_t count)
{
    std::vector<Comparison> result;
    for (std::size_t i = 0; i < count; ++i)
        result.push_back(all[count == 1 ? all.size() / 2 : i * (all.size() - 1) / (count - 1)]);
    return result;
}
std::vector<Event> schedule(const std::vector<Comparison> &comparisons, std::size_t frames, bool scan)
{
    std::vector<Event> events;
    for (std::size_t i = 0; i < comparisons.size(); ++i)
    {
        Event e;
        e.frame = i * frames / comparisons.size();
        e.firstHz = valueToFrequency(comparisons[i].first);
        e.secondHz = valueToFrequency(comparisons[i].second);
        e.firstActive = true;
        e.secondActive = !scan;
        e.heldIndex = comparisons[i].held;
        events.push_back(e);
    }
    return events;
}
bool include(const std::string &p, const std::string &s, const std::string &pf, const std::string &sf)
{
    return (pf.empty() || pf == p) && (sf.empty() || sf == s);
}
Signal renderModel(const Profile &p, const std::string &scenario, const std::string &group,
                   const std::string &description, const std::vector<Event> &events, std::size_t total,
                   std::size_t active, bool withBed, float heldDb)
{
    Signal r;
    r.profile = p.id;
    r.model = p.model;
    r.scenario = scenario;
    r.group = group;
    r.description = description;
    r.parametersJson = p.parametersJson;
    r.id = p.id + "__" + scenario;
    r.events = events;
    r.raw.assign(total * 2, 0);
    r.renderSpecJson = "{\"sampleRate\":" + num(p.spec.sampleRate) + ",\"masterGain\":" + num(p.spec.masterGain) +
                       ",\"sourcePeakGain\":" + num(p.spec.sourcePeakGain) + ",\"attackMs\":" + num(p.spec.attackMs) +
                       ",\"releaseMs\":" + num(p.spec.releaseMs) + ",\"pitchSlewMs\":" + num(p.spec.pitchSlewMs) +
                       ",\"minimumFrequencyHz\":" + num(p.spec.minimumFrequencyHz) +
                       ",\"maximumFrequencyHz\":" + num(p.spec.maximumFrequencyHz) + "}";
    if (!r.parametersJson.empty() && r.parametersJson[r.parametersJson.size() - 1] == '}')
        r.parametersJson.erase(r.parametersJson.size() - 1);
    if (scenario == "selection_sort_64")
        r.parametersJson += ",\"scenarioEnvelope\":{\"attackMs\":" + num(p.spec.attackMs) +
                            ",\"releaseMs\":" + num(p.spec.releaseMs) + "}";
    r.parametersJson +=
        ",\"bedEnabled\":" + std::string(withBed ? "true" : "false") + ",\"heldKeyGainDb\":" + num(heldDb) +
        ",\"outputProcessors\":[],\"eventAccentsEnabled\":false";
    if (withBed)
        r.parametersJson += ",\"bedProfile\":{\"source\":\"legacy_harmonic_bed\",\"gain\":0.15,\"harmonicExponent\":1."
                            "5,\"includeSub300Hz\":false,\"combEnabled\":true,\"phaseAware\":true,"
                            "\"delayMicroseconds\":222.222,\"phaseOffsetDegrees\":12,\"wetLow\":0.95,\"wetHigh\":0.9,"
                            "\"fadeStartHz\":6600,\"fadeEndHz\":15600,\"fadeExponent\":3}";
    r.parametersJson += "}";
    SoundEngine::PipelineDescription d;
    for (int v = 0; v < 2; ++v)
    {
        d.voices[v].source = p.source;
        d.voices[v].filterCount = p.filterCount;
        d.voices[v].filters = p.filters;
    }
    d.bedEnabled = withBed;
    if (withBed)
    {
        d.bed.source.id = SoundEngine::harmonicBedGeneratorId();
        d.bed.source.harmonicExponent = 1.5f;
        d.bed.source.includeSub300HzBed = false;
        d.bed.source.bedComb = p.source.bedComb;
        d.bedGain = .15f;
    }
    SoundEngine::ComposableRenderer synth;
    std::string error;
    if (!synth.prepare(p.spec, d, &error))
    {
        r.parametersJson = "{\"preparationError\":\"" + escapeJson(error) + "\"}";
        return r;
    }
    if (withBed)
        synth.setBedControl(SoundEngine::VoiceControl(300, 4, true));
    const float hg = float(std::pow(10.0, heldDb / 20.0));
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const std::size_t begin = static_cast<std::size_t>(events[i].frame),
                          end = i + 1 < events.size() ? static_cast<std::size_t>(events[i + 1].frame) : active;
        if (begin > cursor)
            synth.render(r.raw.data() + cursor * 2, begin - cursor);
        float g0 = events[i].firstActive ? 1.0f : 0.0f, g1 = events[i].secondActive ? 1.0f : 0.0f;
        if (heldDb != 0 && events[i].heldIndex == 0)
            g0 = hg;
        if (heldDb != 0 && events[i].heldIndex == 1)
            g1 = hg;
        synth.setVoiceControl(0, SoundEngine::VoiceControl(events[i].firstHz, g0, events[i].firstActive));
        synth.setVoiceControl(1, SoundEngine::VoiceControl(events[i].secondHz, g1, events[i].secondActive));
        if (end > begin)
            synth.render(r.raw.data() + begin * 2, end - begin);
        cursor = end;
    }
    synth.setVoiceControl(0, SoundEngine::VoiceControl(440, 0, false));
    synth.setVoiceControl(1, SoundEngine::VoiceControl(440, 0, false));
    if (cursor < total)
        synth.render(r.raw.data() + cursor * 2, total - cursor);
    return r;
}
Signal renderLegacy(const std::string &scenario, const std::string &group, const std::string &desc,
                    const std::vector<Event> &events, std::size_t total, std::size_t active, float heldDb)
{
    Signal r;
    r.profile = "legacy";
    r.model = "legacy_harmonic_field";
    r.scenario = scenario;
    r.group = group;
    r.description = desc;
    r.id = "legacy__" + scenario;
    r.events = events;
    r.raw.assign(total * 2, 0);
    SoundEngine::Synthesizer synth;
    SoundEngine::Parameters params = synth.parameters();
    params.chorusMix = 0;
    params.heldKeyBoostDb = heldDb;
    synth.setParameters(params);
    synth.setTraceLoaded(true);
    std::ostringstream parameterJson;
    parameterJson << std::setprecision(8) << "{\"masterLevel\":" << params.masterLevel
                  << ",\"bedLevel\":" << params.bedLevel << ",\"bedHarmonicExponent\":" << params.bedHarmonicExponent
                  << ",\"includeSub300HzBed\":" << (params.includeSub300HzBed ? "true" : "false")
                  << ",\"bedComb\":{\"enabled\":" << (params.bedComb.enabled ? "true" : "false")
                  << ",\"applyPhase\":" << (params.bedComb.applyPhase ? "true" : "false")
                  << ",\"delayMicroseconds\":" << params.bedComb.delayMicroseconds
                  << ",\"phaseOffsetDegrees\":" << params.bedComb.phaseOffsetDegrees
                  << ",\"wetAtLowFrequency\":" << params.bedComb.wetAtLowFrequency
                  << ",\"wetAtHighFrequency\":" << params.bedComb.wetAtHighFrequency
                  << ",\"fadeStartHz\":" << params.bedComb.fadeStartHz << ",\"fadeEndHz\":" << params.bedComb.fadeEndHz
                  << ",\"fadeExponent\":" << params.bedComb.fadeExponent
                  << "},\"comparisonBoostDb\":" << params.comparisonBoostDb
                  << ",\"heldKeyBoostDb\":" << params.heldKeyBoostDb << ",\"roleBoostDb\":[";
    for (std::size_t i = 0; i < SoundEngine::RoleCount; ++i)
    {
        if (i)
            parameterJson << ',';
        parameterJson << params.roleBoostDb[i];
    }
    parameterJson << "],\"tauBelowBins\":" << params.tauBelowBins << ",\"tauAboveBins\":" << params.tauAboveBins
                  << ",\"chorusMix\":" << params.chorusMix << ",\"chorusRateHz\":" << params.chorusRateHz
                  << ",\"chorusDepthMs\":" << params.chorusDepthMs
                  << ",\"chorusWeighting\":\"uniform\",\"attackMs\":" << params.attackMs
                  << ",\"releaseMs\":" << params.releaseMs
                  << ",\"mappingMode\":\"linear_hz\",\"comparisonSpreadMode\":\"fixed_bins\",\"bandTrimDb\":[";
    for (std::size_t i = 0; i < SoundEngine::BandCount; ++i)
    {
        if (i)
            parameterJson << ',';
        parameterJson << params.bandTrimDb[i];
    }
    parameterJson << "],\"accentRouting\":[";
    for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
    {
        if (i)
            parameterJson << ',';
        parameterJson << (params.accentRouting[i] ? "true" : "false");
    }
    parameterJson << "],\"accentGain\":[";
    for (std::size_t i = 0; i < SoundEngine::AccentCategoryCount; ++i)
    {
        if (i)
            parameterJson << ',';
        parameterJson << params.accentGain[i];
    }
    parameterJson << "],\"enabled\":" << (params.enabled ? "true" : "false") << "}";
    r.parametersJson = parameterJson.str();
    r.renderSpecJson = "{\"sampleRate\":44100,\"legacyCalculationOrder\":true}";
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const std::size_t begin = static_cast<std::size_t>(events[i].frame),
                          end = i + 1 < events.size() ? static_cast<std::size_t>(events[i + 1].frame) : active;
        if (begin > cursor)
            synth.render(r.raw.data() + cursor * 2, begin - cursor, Rate);
        const SoundEngine::HeldKeyParticipant h =
            events[i].heldIndex == 0 ? SoundEngine::HeldKeyParticipant::First
                                     : (events[i].heldIndex == 1 ? SoundEngine::HeldKeyParticipant::Second
                                                                 : SoundEngine::HeldKeyParticipant::None);
        synth.setComparison(
            SoundEngine::AudioComparison((events[i].firstHz - 50) / 4150, (events[i].secondHz - 50) / 4150, h));
        if (end > begin)
            synth.render(r.raw.data() + begin * 2, end - begin, Rate);
        cursor = end;
    }
    synth.clearComparison();
    if (cursor < total)
        synth.render(r.raw.data() + cursor * 2, total - cursor, Rate);
    return r;
}
void metrics(Signal &r)
{
    double sum = 0;
    for (float x : r.raw)
        sum += x;
    r.dc = r.raw.empty() ? 0 : sum / r.raw.size();
    double sq = 0;
    r.peak = 0;
    for (float x : r.raw)
    {
        r.peak = (std::max)(r.peak, std::fabs(double(x)));
        double a = x - r.dc;
        sq += a * a;
    }
    r.acRms = r.raw.empty() ? 0 : std::sqrt(sq / r.raw.size());
}
void match(std::vector<Signal> &signals)
{
    std::map<std::string, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < signals.size(); ++i)
    {
        metrics(signals[i]);
        groups[signals[i].group].push_back(i);
    }
    for (std::map<std::string, std::vector<std::size_t>>::const_iterator g = groups.begin(); g != groups.end(); ++g)
    {
        double target = DesiredRms;
        for (std::size_t j = 0; j < g->second.size(); ++j)
        {
            const Signal &s = signals[g->second[j]];
            if (s.peak > 0 && s.acRms > 0)
                target = (std::min)(target, s.acRms * PeakCeiling / s.peak);
        }
        for (std::size_t j = 0; j < g->second.size(); ++j)
        {
            Signal &s = signals[g->second[j]];
            s.listeningGain = s.acRms > 0 ? target / s.acRms : 1;
        }
    }
}
std::vector<Event> probeEvents(std::size_t &total)
{
    const float f[] = {50, 100, 150, 1150, 2700, 4200};
    std::vector<Event> e;
    for (std::size_t i = 0; i < 6; ++i)
    {
        Event on;
        on.frame = i * Rate * 3 / 2;
        on.firstHz = f[i];
        on.firstActive = true;
        e.push_back(on);
        Event off;
        off.frame = on.frame + Rate;
        off.firstHz = f[i];
        e.push_back(off);
    }
    total = Rate * 9;
    return e;
}
bool writePack(const std::string &dir, std::vector<Signal> &sigs)
{
    match(sigs);
    std::ofstream json((dir + "\\manifest.json").c_str(), std::ios::binary | std::ios::trunc);
    std::ofstream md((dir + "\\README.md").c_str(), std::ios::binary | std::ios::trunc);
    if (!json || !md)
        return false;
    json << "{\n  \"format\":\"source model previews v2\",\n  \"sampleRate\":" << Rate << ",\n  \"signals\":[\n";
    md << "# SoundEngine source model listening pack\n\nThe earlier 70-profile exploration has been trimmed to the retained profiles below. "
          "Raw IEEE float32 stereo renders are untouched. PCM16 copies "
          "are level matched within scenario groups toward -24 dBFS AC RMS, lowering the group target when required to "
          "keep peaks below -1 dBFS. The app still uses the existing legacy sound profile.\n\nPacket-cycle durations "
          "are clamped to 0.5–4 periods of the 300 Hz packet clock so eight preallocated grains cover the overlap. "
          "This can shorten a requested carrier-cycle count at low pitches.\n\n## Baseline references\n\n";
    const char *first[] = {"legacy__pair_sweep_equal",
                           "legacy__pair_sweep_held6db",
                           "legacy__trace_16changes_per_sec",
                           "sine_slew2_a8_r8__trace_8changes_per_sec",
                           "sine_slew2_a8_r8__trace_16changes_per_sec",
                           "packet_fixed_gaussian_4period_reset_peak__trace_8changes_per_sec",
                           "packet_fixed_gaussian_4period_reset_peak__trace_16changes_per_sec",
                           "saw_q4_lp10khz__trace_8changes_per_sec",
                           "saw_q4_lp10khz__trace_16changes_per_sec",
                           "sine_slew2_a8_r8__selection_sort_64",
                           "packet_fixed_gaussian_4period_reset_peak__selection_sort_64",
                           "saw_q4_lp10khz__selection_sort_64"};
    for (std::size_t k = 0; k < sizeof(first) / sizeof(first[0]); ++k)
        for (std::size_t i = 0; i < sigs.size(); ++i)
            if (sigs[i].id == first[k])
                md << "- **" << sigs[i].id << "** " << sigs[i].description << " (" << safeName(sigs[i].id)
                   << "_listen_pcm16.wav; " << safeName(sigs[i].id) << "_raw_f32.wav)\n";
    md << "\n## Filtered saw family — Selection Sort demos\n\n"
          "These clips share the same seeded 64-comparison Selection Sort schedule, 1/2 ms envelope, "
          "and five-second duration. They vary only the saw filter configuration.\n\n";
    for (std::size_t i = 0; i < sigs.size(); ++i)
        if (sigs[i].scenario == "selection_sort_64" && sigs[i].profile.find("saw_") == 0)
            md << "- **" << sigs[i].profile << "** (" << safeName(sigs[i].id) << "_listen_pcm16.wav; "
               << safeName(sigs[i].id) << "_raw_f32.wav)\n";
    md << "\n## All signals\n\n| Signal | Model | Scenario | Matched RMS | Matched peak |\n|---|---|---|---:|---:|\n";
    for (std::size_t i = 0; i < sigs.size(); ++i)
    {
        Signal &s = sigs[i];
        const std::string stem = safeName(s.id);
        std::vector<float> listen(s.raw.size());
        for (std::size_t j = 0; j < listen.size(); ++j)
            listen[j] = float(s.raw[j] * s.listeningGain);
        if (!writeWavFloat32Stereo(dir + "\\" + stem + "_raw_f32.wav", s.raw, Rate) ||
            !writeWav16Stereo(dir + "\\" + stem + "_listen_pcm16.wav", listen, Rate))
            return false;
        md << "| " << s.id << " | " << s.model << " | " << s.scenario << " | " << num(s.acRms * s.listeningGain)
           << " | " << num(s.peak * s.listeningGain) << " |\n";
        json << "    {\"id\":\"" << escapeJson(s.id) << "\",\"profileId\":\"" << escapeJson(s.profile)
             << "\",\"model\":\"" << escapeJson(s.model) << "\",\"scenarioId\":\"" << escapeJson(s.scenario)
             << "\",\"group\":\"" << escapeJson(s.group) << "\",\"description\":\"" << escapeJson(s.description)
             << "\",\"seed\":" << s.seed << ",\"sampleRate\":" << Rate << ",\"frameCount\":" << s.raw.size() / 2
             << ",\"renderSpec\":" << s.renderSpecJson << ",\"parameters\":" << s.parametersJson
             << ",\"eventFrames\":[";
        for (std::size_t e = 0; e < s.events.size(); ++e)
        {
            if (e)
                json << ',';
            json << s.events[e].frame;
        }
        json << "],\"events\":[";
        for (std::size_t e = 0; e < s.events.size(); ++e)
        {
            if (e)
                json << ',';
            const Event &v = s.events[e];
            json << "{\"frame\":" << v.frame << ",\"firstHz\":" << v.firstHz << ",\"secondHz\":" << v.secondHz
                 << ",\"firstActive\":" << (v.firstActive ? "true" : "false")
                 << ",\"secondActive\":" << (v.secondActive ? "true" : "false")
                 << ",\"heldParticipant\":" << v.heldIndex << "}";
        }
        json << "],\"dc\":" << num(s.dc) << ",\"acRms\":" << num(s.acRms) << ",\"peak\":" << num(s.peak)
             << ",\"appliedListeningGain\":" << num(s.listeningGain) << ",\"rawFile\":\"" << stem
             << "_raw_f32.wav\",\"listeningFile\":\"" << stem << "_listen_pcm16.wav\"}"
             << (i + 1 == sigs.size() ? "\n" : ",\n");
    }
    json << "  ]\n}\n";
    md << "\n## Selection Sort demos\n\nEach retained profile has a five-second Selection Sort clip with exactly 64 comparisons. "
          "The comparisons are sampled evenly across the complete 3,160-comparison Selection Sort trace for a "
          "seeded shuffled 80-value input. Comparison activity runs for 4.5 seconds, followed by a 0.5-second "
          "tail. These clips use a 1 ms attack and 2 ms release. Participants have equal gain; the candidate "
          "minimum is participant A and the scan item is B.\n\n"
          "## Retained matrix\n\n36 profiles remain: six continuous-sine profiles, eight fixed packet profiles at four clock periods, "
          "four carrier-cycle packet profiles at eight cycles, and all 18 filtered-saw profiles. The 250/350 Hz "
          "packet-clock sweeps, shorter fixed packets, and shorter cycle packets were removed. Legacy trace previews "
          "below 16 changes per second were removed.\n\n"
          "manifest.json records settings, fixed seed, sample rate, event frame positions, raw DC/AC RMS/peak and "
          "applied listening gain.\n";
    json.flush();
    md.flush();
    return json.good() && md.good();
}
} // namespace

bool generateSourceModelPreviews(const std::string &dir, const std::string &profileFilter,
                                 const std::string &scenarioFilter)
{
    if (!ensureDir(dir))
        return false;
    const std::vector<Profile> allProfiles = makeProfiles();
    if (allProfiles.size() != 70)
        return false;
    std::vector<Profile> profiles;
    for (std::size_t i = 0; i < allProfiles.size(); ++i)
        if (isRetainedProfile(allProfiles[i]))
            profiles.push_back(allProfiles[i]);
    if (profiles.size() != 36)
        return false;
    std::vector<Comparison> trace;
    if (!makeTrace(trace))
        return false;
    std::vector<Comparison> selectionTrace;
    if (!makeSelectionSortTrace(selectionTrace))
        return false;
    std::vector<Signal> signals;
    const std::size_t scan = Rate * 2, totalScan = scan + ScanReleaseFrames;
    const std::size_t selectionTotal = Rate * 5, selectionActive = Rate * 9 / 2;
    const std::vector<Event> selectionEvents = schedule(sample(selectionTrace, 64), selectionActive, false);
    std::vector<Comparison> scanValues;
    for (std::size_t i = 0; i < 80; ++i)
        scanValues.push_back(Comparison(float(i) / 79, float(i) / 79, -1));
    for (std::size_t p = 0; p < profiles.size(); ++p)
    {
        if (include(profiles[p].id, "ordered_scan", profileFilter, scenarioFilter))
            signals.push_back(renderModel(profiles[p], "ordered_scan", "ordered_scan_profiles",
                                          "Single voice scans 80 values from 50 through 4,200 Hz.",
                                          schedule(scanValues, scan, true), totalScan, scan, false, 0));
        if (include(profiles[p].id, "insertion_adjacent", profileFilter, scenarioFilter))
            signals.push_back(renderModel(profiles[p], "insertion_adjacent", "insertion_profiles",
                                          "79 adjacent comparisons from Insertion Sort on an ordered 80-item array.",
                                          schedule(trace, scan, false), totalScan, scan, false, 0));
        if (include(profiles[p].id, "selection_sort_64", profileFilter, scenarioFilter))
        {
            Profile selectionProfile = profiles[p];
            selectionProfile.spec.attackMs = 1.0f;
            selectionProfile.spec.releaseMs = 2.0f;
            signals.push_back(renderModel(
                selectionProfile, "selection_sort_64", "selection_sort_profiles",
                "64 comparisons sampled evenly across a seeded Selection Sort trace for 80 shuffled values; "
                "1 ms attack, 2 ms release, 4.5 seconds active plus 0.5-second tail.",
                selectionEvents, selectionTotal, selectionActive, false, 0));
        }
    }
    const std::size_t active5 = Rate * 5;
    for (int r = 0; r < 2; ++r)
    {
        const std::size_t count = r ? 80 : 40;
        const std::string sc = r ? "trace_16changes_per_sec" : "trace_8changes_per_sec",
                          group = r ? "baseline_trace_16hz" : "baseline_trace_8hz";
        const std::vector<Event> ev = schedule(sample(trace, count), active5, false);
        if (r == 1 && include("legacy", sc, profileFilter, scenarioFilter))
            signals.push_back(renderLegacy(
                sc, group, "Legacy profile; seeded ordered Insertion Sort trace sampled at the selected event rate.",
                ev, active5 + ScanReleaseFrames, active5, 6.0f));
        for (std::size_t p = 0; p < profiles.size(); ++p)
            if ((profiles[p].id == "sine_slew2_a8_r8" || profiles[p].id == "packet_fixed_gaussian_4period_reset_peak" ||
                 profiles[p].id == "saw_q4_lp10khz") &&
                include(profiles[p].id, sc, profileFilter, scenarioFilter))
                signals.push_back(renderModel(profiles[p], sc, group,
                                              "Seeded trace comparisons evenly sampled at the selected event rate.", ev,
                                              active5 + ScanReleaseFrames, active5, false, 0));
    }
    std::vector<Event> pair;
    for (std::size_t i = 0; i < 79; ++i)
    {
        Event e;
        e.frame = i * active5 / 79;
        e.firstHz = valueToFrequency(float(i) / 79);
        e.secondHz = valueToFrequency(float(i + 1) / 79);
        e.firstActive = e.secondActive = true;
        e.heldIndex = 1;
        pair.push_back(e);
    }
    if (include("legacy", "pair_sweep_equal", profileFilter, scenarioFilter))
        signals.push_back(renderLegacy("pair_sweep_equal", "legacy_pair_sweeps",
                                       "Ordered pair sweep, equal participant roles.", pair,
                                       active5 + ScanReleaseFrames, active5, 0.0f));
    if (include("legacy", "pair_sweep_held6db", profileFilter, scenarioFilter))
        signals.push_back(renderLegacy("pair_sweep_held6db", "legacy_pair_sweeps",
                                       "Same ordered pair sweep with +6 dB held-key weighting.", pair,
                                       active5 + ScanReleaseFrames, active5, 6.0f));
    for (std::size_t p = 0; p < profiles.size(); ++p)
    {
        if (profiles[p].id != "sine_slew2_a8_r8" && profiles[p].id != "packet_fixed_gaussian_4period_reset_peak" &&
            profiles[p].id != "saw_q4_lp10khz")
            continue;
        const std::vector<Event> ev = schedule(trace, active5, false);
        if (include(profiles[p].id, "pair_sweep_bed", profileFilter, scenarioFilter))
            signals.push_back(renderModel(profiles[p], "pair_sweep_bed", "baseline_pair_sweeps",
                                          "Baseline comparison sweep plus the independent legacy harmonic bed.", ev,
                                          active5 + ScanReleaseFrames, active5, true, 0));
        if (include(profiles[p].id, "pair_sweep_held6db", profileFilter, scenarioFilter))
            signals.push_back(renderModel(profiles[p], "pair_sweep_held6db", "baseline_pair_sweeps",
                                          "Baseline comparison sweep with +6 dB on the held participant.", ev,
                                          active5 + ScanReleaseFrames, active5, false, 6));
    }
    std::size_t probeTotal = 0;
    const std::vector<Event> probes = probeEvents(probeTotal);
    for (std::size_t p = 0; p < profiles.size(); ++p)
    {
        if (profiles[p].id != "sine_slew2_a8_r8" && profiles[p].id != "packet_fixed_gaussian_4period_reset_peak" &&
            profiles[p].id != "saw_q4_lp10khz")
            continue;
        if (include(profiles[p].id, "single_voice_probes", profileFilter, scenarioFilter))
            signals.push_back(renderModel(
                profiles[p], "single_voice_probes", "model_probes",
                "Six one-second tones at 50, 100, 150, 1,150, 2,700 and 4,200 Hz separated by 0.5-second release gaps.",
                probes, probeTotal, probeTotal, false, 0));
        if (include(profiles[p].id, "release_probe", profileFilter, scenarioFilter))
        {
            std::vector<Event> e(2);
            e[0].firstHz = 1150;
            e[0].firstActive = true;
            e[1].frame = Rate;
            e[1].firstHz = 1150;
            signals.push_back(renderModel(profiles[p], "release_probe", "release_probes",
                                          "One second at 1,150 Hz followed by two seconds without focus.", e, Rate * 3,
                                          Rate, false, 0));
        }
    }
    if (profileFilter.empty() && scenarioFilter.empty() && signals.size() != 129)
    {
        std::ofstream e((dir + "\\generation_error.txt").c_str());
        e << "Expected 129 signals, created " << signals.size();
        return false;
    }
    return writePack(dir, signals);
}
