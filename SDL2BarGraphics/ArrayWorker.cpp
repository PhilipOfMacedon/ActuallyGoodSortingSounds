#include "ArrayWorker.h"

#include <exception>
#include <iostream>

ArrayWorker::ArrayWorker(
	std::vector<GLfloat>& array,
	std::vector<int>& highlights,
	std::mutex& stateMutex)
	: stopRequested(false),
	sorter(array, highlights, stateMutex, stopRequested),
	sorting(false)
{
}

ArrayWorker::~ArrayWorker()
{
	stopAndJoin();
}

bool ArrayWorker::selectOperation(SorterOperation operation)
{
	switch (operation)
	{
	case SorterOperation::IncDelay:
		sorter.incDelay();
		return true;
	case SorterOperation::DecDelay:
		sorter.decDelay();
		return true;
	case SorterOperation::Randomize:
	case SorterOperation::Revert:
	case SorterOperation::InsertionSort:
		break;
	default:
		return false;
	}

	std::lock_guard<std::mutex> lock(lifecycleMutex);
	if (stopRequested.load() || sorting.load())
		return false;

	// A finished std::thread remains joinable until explicitly joined.
	if (workerThread.joinable())
		workerThread.join();

	sorting.store(true);
	try
	{
		workerThread = std::thread(&ArrayWorker::runOperation, this, operation);
	}
	catch (const std::exception& error)
	{
		sorting.store(false);
		std::cerr << "Could not start sorting worker: " << error.what() << '\n';
		return false;
	}

	return true;
}

void ArrayWorker::stopAndJoin()
{
	std::lock_guard<std::mutex> lock(lifecycleMutex);
	stopRequested.store(true);
	if (workerThread.joinable())
		workerThread.join();
	sorting.store(false);
}

void ArrayWorker::runOperation(SorterOperation operation)
{
	try
	{
		switch (operation)
		{
		case SorterOperation::Randomize:
			sorter.randomize();
			break;
		case SorterOperation::Revert:
			sorter.revert();
			break;
		case SorterOperation::InsertionSort:
			sorter.insertionSort();
			break;
		default:
			break;
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "Sorting operation failed: " << error.what() << '\n';
	}
	catch (...)
	{
		std::cerr << "Sorting operation failed with an unknown error.\n";
	}

	sorting.store(false);
}
