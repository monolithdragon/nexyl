#pragma once

#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>

// Sorting and binary search. Simulation code sorts with a total order (rule DET-7): with unique keys or a full
// tie-break, the result is the same on every platform, because the algorithm makes no random choices.

namespace Nexyl::Containers {

namespace Detail {

/// Below this many elements, sorting falls back to insertion sort. @since 0.1
inline constexpr usize kInsertionSortLimit = 16;

/// Sorts a short range by insertion. @since 0.1
template <typename T, typename TLess>
void InsertionSort(T* first, usize count, TLess& less) {
    for (usize index = 1; index < count; ++index) {
        T value(::Nexyl::Move(first[index]));
        usize position = index;
        while (position > 0 && less(value, first[position - 1])) {
            first[position] = ::Nexyl::Move(first[position - 1]);
            --position;
        }
        first[position] = ::Nexyl::Move(value);
    }
}

/// Restores the heap property below `root`. @since 0.1
template <typename T, typename TLess>
void SiftDown(T* first, usize root, usize count, TLess& less) {
    while (true) {
        usize child = 2 * root + 1;
        if (child >= count) {
            return;
        }
        if (child + 1 < count && less(first[child], first[child + 1])) {
            ++child;
        }
        if (!less(first[root], first[child])) {
            return;
        }
        Swap(first[root], first[child]);
        root = child;
    }
}

/// Sorts by heapsort: the fallback that bounds introsort to O(n log n). @since 0.1
template <typename T, typename TLess>
void HeapSort(T* first, usize count, TLess& less) {
    for (usize start = count / 2; start > 0; --start) {
        SiftDown(first, start - 1, count, less);
    }
    for (usize end = count; end > 1; --end) {
        Swap(first[0], first[end - 1]);
        SiftDown(first, 0, end - 1, less);
    }
}

/// Sorts by quicksort with a median-of-three pivot, switching to heapsort past `depthLimit` partitions.
/// @since 0.1
template <typename T, typename TLess>
void IntroSort(T* first, usize count, u32 depthLimit, TLess& less) {
    while (count > kInsertionSortLimit) {
        if (depthLimit == 0) {
            HeapSort(first, count, less);
            return;
        }
        --depthLimit;

        // Order the first, middle and last elements, then use the median as the pivot at position 0. The last
        // element is then at least the pivot, which stops the left scan.
        const usize middle = count / 2;
        const usize last = count - 1;
        if (less(first[middle], first[0])) {
            Swap(first[middle], first[0]);
        }
        if (less(first[last], first[middle])) {
            Swap(first[last], first[middle]);
            if (less(first[middle], first[0])) {
                Swap(first[middle], first[0]);
            }
        }
        Swap(first[0], first[middle]);

        // Hoare partition around first[0]. Equal elements stop both scans, which keeps runs of equal keys
        // balanced.
        usize left = 1;
        usize right = last;
        while (true) {
            while (less(first[left], first[0])) {
                ++left;
            }
            while (less(first[0], first[right])) {
                --right;
            }
            if (left >= right) {
                break;
            }
            Swap(first[left], first[right]);
            ++left;
            --right;
        }
        Swap(first[0], first[right]);

        // Recurse into the smaller side and loop on the larger, which bounds the stack depth.
        const usize leftCount = right;
        const usize rightCount = count - right - 1;
        if (leftCount < rightCount) {
            IntroSort(first, leftCount, depthLimit, less);
            first += right + 1;
            count = rightCount;
        } else {
            IntroSort(first + right + 1, rightCount, depthLimit, less);
            count = leftCount;
        }
    }
    InsertionSort(first, count, less);
}

} // namespace Detail

/// Sorts elements in place, smallest first, by a comparison.
///
/// O(n log n) in every case (introsort). Not stable: elements that compare equal may change their relative order,
/// in the same way on every platform.
/// @param values The elements.
/// @param less   Returns true if its first argument sorts before its second: a strict weak order.
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename TLess>
void Sort(Span<T> values, TLess less) {
    if (values.Size() < 2) {
        return;
    }
    const u32 depthLimit = 2 * HighestSetBit(values.Size());
    Detail::IntroSort(values.Data(), values.Size(), depthLimit, less);
}

/// Sorts elements in place with `<`. @param values The elements. @threadsafety compatible @since 0.1
template <typename T>
void Sort(Span<T> values) {
    Sort(values, [](const T& first, const T& second) { return first < second; });
}

/// Sorts the elements of an array by a comparison. @threadsafety compatible @since 0.1
template <typename T, typename TLess>
void Sort(Array<T>& values, TLess less) {
    Sort(Span<T>(values), less);
}

/// Sorts the elements of an array with `<`. @threadsafety compatible @since 0.1
template <typename T>
void Sort(Array<T>& values) {
    Sort(Span<T>(values));
}

/// Checks whether elements are sorted by a comparison.
/// @return True if no element sorts before its predecessor.
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename TLess>
[[nodiscard]] bool IsSorted(Span<const T> values, TLess less) {
    for (usize index = 1; index < values.Size(); ++index) {
        if (less(values[index], values[index - 1])) {
            return false;
        }
    }
    return true;
}

/// Finds the first position in sorted elements whose element does not sort before `key`.
/// @param values Elements sorted by `less`.
/// @param key    The value to look for.
/// @param less   The comparison the elements are sorted by, called as `less(element, key)`.
/// @return The position, from 0 to values.Size().
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename TKey, typename TLess>
[[nodiscard]] usize LowerBound(Span<const T> values, const TKey& key, TLess less) {
    usize low = 0;
    usize count = values.Size();
    while (count > 0) {
        const usize half = count / 2;
        if (less(values[low + half], key)) {
            low += half + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return low;
}

/// Finds an element equal to `key` in elements sorted with `<`.
/// @return Its position, or kNone.
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename TKey>
[[nodiscard]] Option<usize> BinarySearch(Span<const T> values, const TKey& key) {
    const usize position =
        LowerBound(values, key, [](const T& element, const TKey& wanted) { return element < wanted; });
    if (position < values.Size() && !(key < values[position])) {
        return position;
    }
    return kNone;
}

} // namespace Nexyl::Containers
