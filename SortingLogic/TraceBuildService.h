#pragma once

#include "SortingLogic.h"

#include <mutex>
#include <thread>

namespace sortinglogic
{
	// A deliberately small, single-job worker. The service owns only trace
	// construction; simulation state is always published by its caller thread.
	class TraceBuildService
	{
	public:
		TraceBuildService();
		~TraceBuildService();

		TraceBuildService(const TraceBuildService&) = delete;
		TraceBuildService& operator=(const TraceBuildService&) = delete;

		bool submit(const TraceBuildRequest& request, std::string* error = nullptr);
		bool poll(TraceBuildResult& result);
		bool isBusy() const;

	private:
		mutable std::mutex mutex_;
		std::thread worker_;
		bool busy_;
		bool completionReady_;
		TraceBuildResult completion_;

		void build(TraceBuildRequest request);
	};
}
