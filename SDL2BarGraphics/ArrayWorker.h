#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <GL/glew.h>

#include "ArraySorter.h"

class ArrayWorker
{
public:
	ArrayWorker(
		std::vector<GLfloat>& array,
		std::vector<int>& highlights,
		std::mutex& stateMutex);
	~ArrayWorker();

	ArrayWorker(const ArrayWorker&) = delete;
	ArrayWorker& operator=(const ArrayWorker&) = delete;

	bool selectOperation(SorterOperation operation);
	void stopAndJoin();
	bool isSorting() const { return sorting.load(); }

private:
	void runOperation(SorterOperation operation);

	std::atomic<bool> stopRequested;
	ArraySorter sorter;
	std::thread workerThread;
	std::mutex lifecycleMutex;
	std::atomic<bool> sorting;
};
