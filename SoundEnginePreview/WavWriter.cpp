#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace
{
	void writeU16(std::ofstream& output, std::uint16_t value)
	{
		output.put(static_cast<char>(value & 0xff));
		output.put(static_cast<char>((value >> 8) & 0xff));
	}

	void writeU32(std::ofstream& output, std::uint32_t value)
	{
		output.put(static_cast<char>(value & 0xff));
		output.put(static_cast<char>((value >> 8) & 0xff));
		output.put(static_cast<char>((value >> 16) & 0xff));
		output.put(static_cast<char>((value >> 24) & 0xff));
	}
}

bool writeWav16Stereo(const std::string& path, const std::vector<float>& interleavedSamples, std::uint32_t sampleRate)
{
	if (sampleRate == 0 || interleavedSamples.size() % 2 != 0)
		return false;
	const std::uint64_t dataBytes64 = static_cast<std::uint64_t>(interleavedSamples.size()) * sizeof(std::int16_t);
	if (dataBytes64 > std::numeric_limits<std::uint32_t>::max() - 36)
		return false;
	const std::uint32_t dataBytes = static_cast<std::uint32_t>(dataBytes64);

	std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
	if (!output)
		return false;
	output.write("RIFF", 4);
	writeU32(output, 36 + dataBytes);
	output.write("WAVE", 4);
	output.write("fmt ", 4);
	writeU32(output, 16);
	writeU16(output, 1); // Integer PCM.
	writeU16(output, 2);
	writeU32(output, sampleRate);
	writeU32(output, sampleRate * 2 * sizeof(std::int16_t));
	writeU16(output, 2 * sizeof(std::int16_t));
	writeU16(output, 16);
	output.write("data", 4);
	writeU32(output, dataBytes);

	for (std::size_t i = 0; i < interleavedSamples.size(); ++i)
	{
		const float sample = std::max(-1.0f, std::min(1.0f, interleavedSamples[i]));
		const long rounded = std::lround(sample * (sample < 0.0f ? 32768.0f : 32767.0f));
		writeU16(output, static_cast<std::uint16_t>(static_cast<std::int16_t>(rounded)));
	}
	output.flush();
	return output.good();
}
