#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include <GL/glew.h>

enum class SorterOperation
{
    None,

    IncDelay,
    DecDelay,

    Randomize,
    Revert,
    BubbleSortFast,
    BubbleSortSlow,
    InsertionSort,
    SelectionSort,
    DualSelectionSort,
    GnomeSort,
    ShakerSort,
    ShellSort,
    QuickSort,
    MergeSort,
    HeapSort,
};

class ArraySorter
{
public:
    ArraySorter(
        std::vector<GLfloat>& array,
        std::vector<int>& highlights,
        std::mutex& stateMutex,
        const std::atomic<bool>& stopRequested);

    void randomize();
    void revert();
    void setDelay(std::uint32_t delay);
    void incDelay();
    void decDelay();
    void uselessScan();

    void bubbleSort();
    void insertionSort();
    void selectionSort();
    void gnomeSort();
    void badBubbleSort();
    void dualSelectionSort();
    void shakerSort();

    void shellSort();
    void quickSort();
    void mergeSort();
    void heapSort();

private:
    std::vector<GLfloat>& array;
    std::vector<int>& highlights;
    std::mutex& stateMutex;
    const std::atomic<bool>& stopRequested;
    std::atomic<std::uint32_t> delayDuration;

    void interchange(std::size_t a, std::size_t b);
    void clearHighlights();
    bool waitForDelay() const;
};
