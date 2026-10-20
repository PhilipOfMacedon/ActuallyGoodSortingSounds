#pragma once

#include <cstdint>

struct AudioTimelineAcknowledgement
{
	std::uint64_t timelineGeneration;
	std::uint64_t seekRevision;

	AudioTimelineAcknowledgement(std::uint64_t generation = 0, std::uint64_t seek = 0)
		: timelineGeneration(generation), seekRevision(seek) {}
};

struct AudioPlaybackRevision
{
	std::uint64_t timelineGeneration;
	std::uint64_t transportRevision;

	AudioPlaybackRevision(std::uint64_t generation = 0, std::uint64_t transport = 0)
		: timelineGeneration(generation), transportRevision(transport) {}
};
