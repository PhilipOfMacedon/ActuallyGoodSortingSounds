#pragma once

#include <cstdint>
#include <string>
#include <vector>

bool writeWav16Stereo(const std::string& path, const std::vector<float>& interleavedSamples, std::uint32_t sampleRate);
