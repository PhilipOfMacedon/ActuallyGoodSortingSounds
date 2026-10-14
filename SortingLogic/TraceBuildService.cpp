#include "TraceBuildService.h"

#include <exception>
#include <utility>

namespace sortinglogic
{
	TraceBuildService::TraceBuildService()
		: mutex_(), worker_(), busy_(false), completionReady_(false), completion_()
	{
	}

	TraceBuildService::~TraceBuildService()
	{
		if (worker_.joinable())
			worker_.join();
	}

	bool TraceBuildService::submit(const TraceBuildRequest& request, std::string* error)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (busy_ || completionReady_)
		{
			if (error) *error = "A trace build is already active or awaiting delivery.";
			return false;
		}

		busy_ = true;
		try
		{
			worker_ = std::thread(&TraceBuildService::build, this, request);
		}
		catch (const std::exception& exception)
		{
			busy_ = false;
			if (error) *error = std::string("Could not start trace worker: ") + exception.what();
			return false;
		}
		if (error) error->clear();
		return true;
	}

	bool TraceBuildService::poll(TraceBuildResult& result)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!completionReady_)
				return false;
			result = std::move(completion_);
			completion_ = TraceBuildResult();
			completionReady_ = false;
		}

		if (worker_.joinable())
			worker_.join();

		{
			std::lock_guard<std::mutex> lock(mutex_);
			busy_ = false;
		}
		return true;
	}

	bool TraceBuildService::isBusy() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return busy_;
	}

	void TraceBuildService::build(TraceBuildRequest request)
	{
		TraceBuildResult result;
		try
		{
			result.trace = SortTraceBuilder::build(request);
			result.succeeded = true;
			result.status = result.trace.complete ? TraceBuildStatus::Complete : TraceBuildStatus::Partial;
		}
		catch (const std::exception& exception)
		{
			result.error = exception.what();
		}
		catch (...)
		{
			result.error = "Unknown error while building the sorting trace.";
		}

		std::lock_guard<std::mutex> lock(mutex_);
		completion_ = std::move(result);
		completionReady_ = true;
	}
}
