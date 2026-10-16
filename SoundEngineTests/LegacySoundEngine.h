#pragma once

// Frozen copy of the pre-composition renderer. Keep this test-only reference
// unchanged so PCM parity tests continue to compare against the approved sound.
#include <array>
#include <cstddef>
#include <cstdint>

namespace LegacySoundEngine
{
	static const std::size_t BandCount = 60;
	static const std::size_t FocusBandCount = 17;
	static const std::size_t AccentCategoryCount = 4;
	static const std::size_t RoleCount = 8;
	enum class AccentCategory : std::size_t { Probe, Movement, Distribution, Discard };
	struct EventAccentSummary { std::array<std::uint16_t, AccentCategoryCount> counts; std::array<float, AccentCategoryCount> representativePositions; EventAccentSummary() { counts.fill(0); representativePositions.fill(0.5f); } };
	enum class MappingMode { LinearHz, LogFrequency, EqualBandSlots };
	enum class HeldKeyParticipant { None, First, Second };
	enum class ComparisonRole : std::size_t { None = 0, HeldKey, Pivot, CurrentMinimum, CurrentMaximum, WriteTarget, ProbedItem, ProcessedItem };
	enum class ChorusWeighting { Uniform, ComparisonEnvelope };
	enum class ComparisonSpreadMode { FixedBins, PositionScaled };
	struct FrequencyBand { double frequencyHz; float baseAmplitude; };
	struct BandBlend { std::size_t lowerIndex; std::size_t upperIndex; float upperWeight; double frequencyHz; };
	struct AudioComparison
	{
		float firstPosition, secondPosition;
		HeldKeyParticipant heldKey;
		ComparisonRole firstRole, secondRole;
		AudioComparison();
		AudioComparison(float first, float second, HeldKeyParticipant held);
		AudioComparison(float first, float second, ComparisonRole firstSemanticRole, ComparisonRole secondSemanticRole);
	};
	struct BedCombFilterParameters
	{
		bool enabled, applyPhase;
		float delayMicroseconds, phaseOffsetDegrees, wetAtLowFrequency, wetAtHighFrequency, fadeStartHz, fadeEndHz, fadeExponent;
		BedCombFilterParameters();
	};
	struct Parameters
	{
		float masterLevel, bedLevel, bedHarmonicExponent;
		bool includeSub300HzBed;
		BedCombFilterParameters bedComb;
		float comparisonBoostDb, heldKeyBoostDb;
		std::array<float, RoleCount> roleBoostDb;
		float tauBelowBins, tauAboveBins, chorusMix, chorusRateHz, chorusDepthMs;
		ChorusWeighting chorusWeighting;
		float attackMs, releaseMs;
		MappingMode mappingMode;
		ComparisonSpreadMode comparisonSpreadMode;
		std::array<float, BandCount> bandTrimDb;
		std::array<bool, AccentCategoryCount> accentRouting;
		std::array<float, AccentCategoryCount> accentGain;
		bool enabled;
		Parameters();
	};
	enum class AudioCommandKind { TraceLoaded, Comparison, ClearComparison, Enable, Disable };
	struct AudioCommand { AudioCommandKind kind; AudioComparison comparison; bool traceLoaded; AudioCommand(); };
	const std::array<FrequencyBand, BandCount>& frequencyBank();
	BandBlend mapNormalizedPosition(float position, MappingMode mode);
	float normalizeValue(float value, float minimum, float maximum);
	float decibelsToGain(float decibels);
	float bedCombMagnitude(double frequencyHz, const BedCombFilterParameters& parameters);
	float bedCombPhase(double frequencyHz, const BedCombFilterParameters& parameters);
	class Synthesizer
	{
	public:
		Synthesizer();
		void applyCommand(const AudioCommand& command);
		void setTraceLoaded(bool loaded);
		void setComparison(const AudioComparison& comparison);
		void clearComparison();
		void setEventAccentSummary(const EventAccentSummary& summary);
		void setParameters(const Parameters& parameters);
		const Parameters& parameters() const;
		bool traceLoaded() const;
		std::uint64_t comparisonUpdateCount() const;
		void render(float* output, std::size_t frameCount, std::uint32_t sampleRate);
		void reset();
	private:
		void updateComparisonTargets(); void updateEventAccentTargets(); float comparisonRoleGain(ComparisonRole role) const;
		void retargetParticipant(std::array<float, BandCount>& targets, float position);
		float readDelay(const std::array<float, 16384>& delayLine, double delaySamples) const;
		static float sanitize(float value, float fallback, float minimum, float maximum);
		static MappingMode sanitizeMappingMode(MappingMode mode);
		static ComparisonSpreadMode sanitizeComparisonSpreadMode(ComparisonSpreadMode mode);
		static ChorusWeighting sanitizeChorusWeighting(ChorusWeighting weighting);
		Parameters targetParameters_; AudioComparison activeComparison_; bool hasComparison_, traceLoaded_;
		std::uint64_t comparisonUpdateCount_;
		std::array<double, BandCount> phases_;
		std::array<float, BandCount> firstWeightTargets_, secondWeightTargets_, firstWeights_, secondWeights_, bandTrimGains_, targetBandTrimGains_;
		EventAccentSummary eventAccentSummary_;
		std::array<float, AccentCategoryCount> eventAccentTargets_, eventAccentEnvelopes_, eventAccentAttackRemainingSeconds_;
		std::array<double, AccentCategoryCount> eventAccentPhases_;
		std::array<float, BandCount> bedCombGains_, targetBedCombGains_;
		std::array<double, BandCount> bedCombPhases_, targetBedCombPhases_;
		std::array<float, BandCount> bedHarmonicGains_, targetBedHarmonicGains_;
		float bedEnvelope_, masterLevel_, bedLevel_, comparisonBoost_, heldKeyBoost_, targetComparisonBoost_, targetHeldKeyBoost_;
		std::array<float, RoleCount> roleBoosts_, targetRoleBoosts_;
		float chorusMix_, chorusRateHz_, chorusDepthMs_;
		double chorusPhase_;
		std::array<float, 16384> chorusDelay_, chorusEnvelopeDelay_;
		std::size_t chorusWriteIndex_;
	};
}
