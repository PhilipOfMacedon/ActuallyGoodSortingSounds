#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace SoundEngine
{
	static const std::size_t BandCount = 60;
	static const std::size_t FocusBandCount = 17;
	static const std::size_t AccentCategoryCount = 4;
	static const std::size_t RoleCount = 8;

	enum class AccentCategory : std::size_t { Probe, Movement, Distribution, Discard };

	struct EventAccentSummary
	{
		std::array<std::uint16_t, AccentCategoryCount> counts;
		std::array<float, AccentCategoryCount> representativePositions;
		EventAccentSummary() { counts.fill(0); representativePositions.fill(0.5f); }
	};

	enum class MappingMode
	{
		LinearHz,
		LogFrequency,
		EqualBandSlots
	};

	enum class HeldKeyParticipant
	{
		None,
		First,
		Second
	};

	// Semantic comparison roles remain independent from the algorithm trace API.
	enum class ComparisonRole : std::size_t
	{
		None = 0, HeldKey, Pivot, CurrentMinimum, CurrentMaximum, WriteTarget, ProbedItem, ProcessedItem
	};

	enum class ChorusWeighting
	{
		Uniform,
		ComparisonEnvelope
	};

	enum class ComparisonSpreadMode
	{
		FixedBins,
		PositionScaled
	};

	struct FrequencyBand
	{
		double frequencyHz;
		float baseAmplitude;
	};

	struct BandBlend
	{
		std::size_t lowerIndex;
		std::size_t upperIndex;
		float upperWeight;
		double frequencyHz;
	};

	struct AudioComparison
	{
		float firstPosition;
		float secondPosition;
		HeldKeyParticipant heldKey;
		ComparisonRole firstRole;
		ComparisonRole secondRole;

		AudioComparison();
		AudioComparison(float first, float second, HeldKeyParticipant held);
		AudioComparison(float first, float second, ComparisonRole firstSemanticRole, ComparisonRole secondSemanticRole);
	};

	// Experimental two-path comb shaping for the continuous harmonic bed.
	// Comparison energy is not filtered by this profile.
	struct BedCombFilterParameters
	{
		bool enabled;
		bool applyPhase;
		float delayMicroseconds;
		float phaseOffsetDegrees;
		float wetAtLowFrequency;
		float wetAtHighFrequency;
		float fadeStartHz;
		float fadeEndHz;
		float fadeExponent;

		BedCombFilterParameters();
	};

	struct Parameters
	{
		float masterLevel;
		float bedLevel;
		float bedHarmonicExponent;
		bool includeSub300HzBed;
		BedCombFilterParameters bedComb;
		float comparisonBoostDb;
		float heldKeyBoostDb;
		std::array<float, RoleCount> roleBoostDb;
		float tauBelowBins;
		float tauAboveBins;
		float chorusMix;
		float chorusRateHz;
		float chorusDepthMs;
		ChorusWeighting chorusWeighting;
		float attackMs;
		float releaseMs;
		MappingMode mappingMode;
		ComparisonSpreadMode comparisonSpreadMode;
		std::array<float, BandCount> bandTrimDb;
		std::array<bool, AccentCategoryCount> accentRouting;
		std::array<float, AccentCategoryCount> accentGain;
		bool enabled;

		Parameters();
		bool operator==(const Parameters& other) const;
	};

	enum class AudioCommandKind
	{
		TraceLoaded,
		Comparison,
		ClearComparison,
		Enable,
		Disable
	};

	struct AudioCommand
	{
		AudioCommandKind kind;
		AudioComparison comparison;
		bool traceLoaded;

		AudioCommand();
	};

	const std::array<FrequencyBand, BandCount>& frequencyBank();
	BandBlend mapNormalizedPosition(float position, MappingMode mode);
	float normalizeValue(float value, float minimum, float maximum);
	float decibelsToGain(float decibels);
	// Returns the normalized magnitude response for a bed partial. Disabled
	// filters and frequencies below 300 Hz return unity gain.
	float bedCombMagnitude(double frequencyHz, const BedCombFilterParameters& parameters);
	// Returns the two-path comb's phase response in radians. It is zero when
	// phase application is disabled or below the 300 Hz bed boundary.
	float bedCombPhase(double frequencyHz, const BedCombFilterParameters& parameters);

	// A small fixed-state synthesizer. Commands are applied immediately in caller order;
	// comparison changes retarget envelopes instead of building an audio backlog.
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

		// Writes interleaved stereo float samples in [-1, 1]. No memory is allocated.
		void render(float* output, std::size_t frameCount, std::uint32_t sampleRate);
		void reset();

	private:
	void updateComparisonTargets();
	void updateEventAccentTargets();
	float comparisonRoleGain(ComparisonRole role) const;
		void retargetParticipant(std::array<float, BandCount>& targets, float position);
		float readDelay(const std::array<float, 16384>& delayLine, double delaySamples) const;
		static float sanitize(float value, float fallback, float minimum, float maximum);
		static MappingMode sanitizeMappingMode(MappingMode mode);
		static ComparisonSpreadMode sanitizeComparisonSpreadMode(ComparisonSpreadMode mode);
		static ChorusWeighting sanitizeChorusWeighting(ChorusWeighting weighting);

		Parameters targetParameters_;
		AudioComparison activeComparison_;
		bool hasComparison_;
		bool traceLoaded_;
		std::uint64_t comparisonUpdateCount_;

		std::array<double, BandCount> phases_;
		std::array<float, BandCount> firstWeightTargets_;
		std::array<float, BandCount> secondWeightTargets_;
		std::array<float, BandCount> firstWeights_;
		std::array<float, BandCount> secondWeights_;
		std::array<float, BandCount> bandTrimGains_;
		std::array<float, BandCount> targetBandTrimGains_;
		EventAccentSummary eventAccentSummary_;
		std::array<float, AccentCategoryCount> eventAccentTargets_;
		std::array<float, AccentCategoryCount> eventAccentEnvelopes_;
		std::array<float, AccentCategoryCount> eventAccentAttackRemainingSeconds_;
		std::array<double, AccentCategoryCount> eventAccentPhases_;
		std::array<float, BandCount> bedCombGains_;
		std::array<float, BandCount> targetBedCombGains_;
		std::array<double, BandCount> bedCombPhases_;
		std::array<double, BandCount> targetBedCombPhases_;
		std::array<float, BandCount> bedHarmonicGains_;
		std::array<float, BandCount> targetBedHarmonicGains_;
		float bedEnvelope_;
		float masterLevel_;
		float bedLevel_;
		float comparisonBoost_;
		float heldKeyBoost_;
		float targetComparisonBoost_;
		float targetHeldKeyBoost_;
		std::array<float, RoleCount> roleBoosts_;
		std::array<float, RoleCount> targetRoleBoosts_;
		float chorusMix_;
		float chorusRateHz_;
		float chorusDepthMs_;
		double chorusPhase_;
		std::array<float, 16384> chorusDelay_;
		std::array<float, 16384> chorusEnvelopeDelay_;
		std::size_t chorusWriteIndex_;
	};
}
