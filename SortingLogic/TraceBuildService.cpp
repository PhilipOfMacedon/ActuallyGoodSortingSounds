#include "TraceBuildService.h"

#include <exception>
#include <utility>

namespace sortinglogic
{
	TraceBuildService::TraceBuildService()
		: mutex_(), worker_(), busy_(false), completionReady_(false), completion_(), activeCancellation_()
	{
	}

	TraceBuildService::~TraceBuildService()
	{
		cancelActive();
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

		TraceBuildRequest job = request;
		if (!job.cancellation) job.cancellation.reset(new std::atomic<bool>(false));
		activeCancellation_ = job.cancellation;
		busy_ = true;
		try
		{
			worker_ = std::thread(&TraceBuildService::build, this, job);
		}
		catch (const std::exception& exception)
		{
			busy_ = false;
			activeCancellation_.reset();
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
			activeCancellation_.reset();
		}

		if (worker_.joinable())
			worker_.join();

		{
			std::lock_guard<std::mutex> lock(mutex_);
			busy_ = false;
		}
		return true;
	}

	bool TraceBuildService::cancelActive()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!busy_ || !activeCancellation_) return false;
		activeCancellation_->store(true, std::memory_order_relaxed);
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
		result.sessionId = request.sessionId;
		result.requestGeneration = request.requestGeneration;
		try
		{
			result.trace = SortTraceBuilder::build(request);
			result.succeeded = true;
			result.status = result.trace.complete ? TraceBuildStatus::Complete : TraceBuildStatus::Partial;
		}
		catch (const std::exception& exception)
		{
			if (request.cancellation && request.cancellation->load(std::memory_order_relaxed))
			{
				result.status = TraceBuildStatus::Cancelled;
				result.error.clear();
			}
			else result.error = exception.what();
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
