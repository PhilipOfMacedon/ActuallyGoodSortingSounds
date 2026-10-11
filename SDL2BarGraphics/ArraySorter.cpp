#include "ArraySorter.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

ArraySorter::ArraySorter(
	std::vector<GLfloat>& array,
	std::vector<int>& highlights,
	std::mutex& stateMutex,
	const std::atomic<bool>& stopRequested)
	: array(array),
	highlights(highlights),
	stateMutex(stateMutex),
	stopRequested(stopRequested),
	delayDuration(5)
{
}

void ArraySorter::randomize()
{
	if (array.empty())
	{
		clearHighlights();
		return;
	}

	std::mt19937 randomEngine(1729); // Keep runs reproducible for now.
	std::uniform_int_distribution<std::size_t> distribution(0, array.size() - 1);

	for (std::size_t i = 0; i < array.size() && !stopRequested.load(); ++i)
	{
		const std::size_t position = distribution(randomEngine);
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			if (highlights.size() >= 2)
			{
				highlights[0] = static_cast<int>(i);
				highlights[1] = static_cast<int>(position);
			}
			interchange(i, position);
		}

		if (!waitForDelay())
			break;
	}

	clearHighlights();
}

void ArraySorter::revert()
{
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		std::sort(array.rbegin(), array.rend());
	}
	clearHighlights();
}

void ArraySorter::setDelay(std::uint32_t delay)
{
	if (delay <= 3000)
		delayDuration.store(delay);
}

void ArraySorter::incDelay()
{
	std::uint32_t current = delayDuration.load();
	while (current < 3000 && !delayDuration.compare_exchange_weak(current, current + 1))
	{
	}
}

void ArraySorter::decDelay()
{
	std::uint32_t current = delayDuration.load();
	while (current > 0 && !delayDuration.compare_exchange_weak(current, current - 1))
	{
	}
}

void ArraySorter::uselessScan()
{
	for (std::size_t i = 0; i < array.size() && !stopRequested.load(); ++i)
	{
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			if (!highlights.empty())
				highlights[0] = static_cast<int>(i);
		}
		if (!waitForDelay())
			break;
	}
	clearHighlights();
}

void ArraySorter::bubbleSort()
{
}

void ArraySorter::insertionSort()
{
	for (std::size_t i = 1; i < array.size() && !stopRequested.load(); ++i)
	{
		GLfloat key;
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			key = array[i];
		}

		std::size_t position = i;
		while (position > 0 && !stopRequested.load())
		{
			bool shifted = false;
			{
				std::lock_guard<std::mutex> lock(stateMutex);
				if (array[position - 1] > key)
				{
					if (highlights.size() >= 2)
					{
						highlights[0] = static_cast<int>(position);
						highlights[1] = static_cast<int>(position - 1);
					}
					array[position] = array[position - 1];
					shifted = true;
				}
			}

			if (!shifted)
				break;

			--position;
			if (!waitForDelay())
				break;
		}

		// Store the saved value even when cancellation arrives mid-pass so the
		// array remains a permutation of its previous values.
		{
			std::lock_guard<std::mutex> lock(stateMutex);
			array[position] = key;
		}
	}

	clearHighlights();
	if (!stopRequested.load())
		uselessScan();
}

void ArraySorter::selectionSort()
{
}

void ArraySorter::gnomeSort()
{
}

void ArraySorter::badBubbleSort()
{
}

void ArraySorter::dualSelectionSort()
{
}

void ArraySorter::shakerSort()
{
}

void ArraySorter::shellSort()
{
}

void ArraySorter::quickSort()
{
}

void ArraySorter::mergeSort()
{
}

void ArraySorter::heapSort()
{
}

void ArraySorter::clearHighlights()
{
	std::lock_guard<std::mutex> lock(stateMutex);
	std::fill(highlights.begin(), highlights.end(), -1);
}

void ArraySorter::interchange(std::size_t a, std::size_t b)
{
	std::swap(array[a], array[b]);
}

bool ArraySorter::waitForDelay() const
{
	const auto endTime = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(delayDuration.load());
	while (!stopRequested.load() && std::chrono::steady_clock::now() < endTime)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	return !stopRequested.load();
}
