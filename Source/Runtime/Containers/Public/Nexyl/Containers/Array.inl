#pragma once

// Definitions of Array's longer members. Included at the end of Array.h only (rule HDR-8).

#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>

namespace Nexyl::Containers {

namespace Detail {

/// Moves `count` elements into uninitialised memory and ends the lifetime of the originals. The ranges do not
/// overlap. @since 0.1
template <typename T>
void RelocateElements(T* destination, T* source, usize count) {
    if constexpr (TriviallyCopyable<T>) {
        Memory::Copy(static_cast<void*>(destination), static_cast<const void*>(source), count * sizeof(T));
    } else {
        for (usize index = 0; index < count; ++index) {
            ConstructAt<T>(destination + index, ::Nexyl::Move(source[index]));
            DestroyAt(source + index);
        }
    }
}

/// Ends the lifetime of `count` elements. @since 0.1
template <typename T>
void DestroyElements(T* first, usize count) {
    if constexpr (!TriviallyDestructible<T>) {
        for (usize index = 0; index < count; ++index) {
            DestroyAt(first + index);
        }
    }
}

} // namespace Detail

template <typename T>
Array<T>::Array(Memory::IAllocator& allocator, InitializerList<T> values)
    requires CopyConstructible<T>
    : m_allocator(&allocator) {
    Reserve(values.size());
    for (const T& value : values) {
        ConstructAt<T>(m_data + m_size, value);
        ++m_size;
    }
}

template <typename T>
Array<T>::Array(Array&& other) noexcept
    : m_data(Exchange(other.m_data, nullptr)), m_size(Exchange(other.m_size, usize{0})),
      m_capacity(Exchange(other.m_capacity, usize{0})), m_allocator(Exchange(other.m_allocator, nullptr)) {}

template <typename T>
Array<T>& Array<T>::operator=(Array&& other) noexcept {
    if (this != &other) {
        Release();
        m_data = Exchange(other.m_data, nullptr);
        m_size = Exchange(other.m_size, usize{0});
        m_capacity = Exchange(other.m_capacity, usize{0});
        m_allocator = Exchange(other.m_allocator, nullptr);
    }
    return *this;
}

template <typename T>
Array<T>::~Array() {
    Release();
}

template <typename T>
Array<T> Array<T>::Clone() const
    requires Cloneable<T>
{
    if (m_allocator == nullptr) {
        return Array();
    }
    return Clone(*m_allocator);
}

template <typename T>
Array<T> Array<T>::Clone(Memory::IAllocator& allocator) const
    requires Cloneable<T>
{
    Array copy(allocator);
    if (m_size != 0) {
        copy.Reserve(m_size);
        for (usize index = 0; index < m_size; ++index) {
            if constexpr (ExplicitlyCloneable<T>) {
                ConstructAt<T>(copy.m_data + index, m_data[index].Clone(allocator));
            } else {
                ConstructAt<T>(copy.m_data + index, m_data[index]);
            }
            // Counted per element, so that the destructor of a partly built copy destroys exactly these.
            copy.m_size = index + 1;
        }
    }
    return copy;
}

template <typename T>
void Array<T>::Reserve(usize capacity) {
    const Result<void, Error> reserved = TryReserve(capacity);
    if (reserved.IsError()) [[unlikely]] {
        Memory::ReportOutOfMemory(
            Memory::OutOfMemoryInfo{m_allocator, capacity * sizeof(T), alignof(T), reserved.GetError()});
    }
}

template <typename T>
Result<void, Error> Array<T>::TryReserve(usize capacity) {
    if (capacity <= m_capacity) {
        return {};
    }
    return TryMoveTo(capacity);
}

template <typename T>
void Array<T>::Resize(usize size)
    requires ConstructibleFrom<T>
{
    if (size < m_size) {
        Detail::DestroyElements(m_data + size, m_size - size);
    } else if (size > m_size) {
        Reserve(size);
        for (usize index = m_size; index < size; ++index) {
            ConstructAt<T>(m_data + index);
        }
    }
    m_size = size;
}

template <typename T>
void Array<T>::Resize(usize size, const T& value)
    requires CopyConstructible<T>
{
    if (size <= m_size) {
        Detail::DestroyElements(m_data + size, m_size - size);
        m_size = size;
        return;
    }
    // `value` may be an element of this array, which growing moves; copy it first.
    const T fill(value);
    Reserve(size);
    for (usize index = m_size; index < size; ++index) {
        ConstructAt<T>(m_data + index, fill);
    }
    m_size = size;
}

template <typename T>
T& Array<T>::Push(const T& value)
    requires CopyConstructible<T>
{
    return Emplace(value);
}

template <typename T>
T& Array<T>::Push(T&& value) {
    return Emplace(::Nexyl::Move(value));
}

template <typename T>
template <typename... TArgs>
T& Array<T>::Emplace(TArgs&&... arguments) {
    if (m_size < m_capacity) {
        ConstructAt<T>(m_data + m_size, ::Nexyl::Forward<TArgs>(arguments)...);
        return m_data[m_size++];
    }

    // The arguments may refer to elements that growing moves, so the new element is constructed in the new memory
    // before the old elements move there.
    NX_ASSERT(m_allocator != nullptr, "An Array needs an allocator before it can hold elements.");
    const usize capacity = GrownCapacity(m_size + 1);
    if (capacity > Memory::kMaxAllocationSize / sizeof(T)) [[unlikely]] {
        Memory::ReportOutOfMemory(Memory::OutOfMemoryInfo{m_allocator, Memory::kMaxAllocationSize, alignof(T),
                                                          Error(Memory::MemoryError::OutOfMemory)});
    }
    T* const data = static_cast<T*>(Memory::Allocate(*m_allocator, capacity * sizeof(T), alignof(T)));
    ConstructAt<T>(data + m_size, ::Nexyl::Forward<TArgs>(arguments)...);
    Detail::RelocateElements(data, m_data, m_size);
    if (m_data != nullptr) {
        m_allocator->Deallocate(static_cast<void*>(m_data), m_capacity * sizeof(T));
    }
    m_data = data;
    m_capacity = capacity;
    return m_data[m_size++];
}

template <typename T>
void Array<T>::Append(Span<const T> values)
    requires CopyConstructible<T>
{
    if (values.IsEmpty()) {
        return;
    }
    // `values` may view this array, which growing moves; remember where it starts.
    const bool isOwn = m_data != nullptr && values.Data() >= m_data && values.Data() < m_data + m_size;
    const usize ownOffset = isOwn ? static_cast<usize>(values.Data() - m_data) : 0;
    GrowFor(m_size + values.Size());
    const T* const source = isOwn ? m_data + ownOffset : values.Data();
    for (usize index = 0; index < values.Size(); ++index) {
        ConstructAt<T>(m_data + m_size + index, source[index]);
    }
    m_size += values.Size();
}

template <typename T>
T Array<T>::Pop() {
    NX_ASSERT(m_size != 0, "Pop from an empty Array.");
    T value(::Nexyl::Move(m_data[m_size - 1]));
    DestroyAt(m_data + m_size - 1);
    --m_size;
    return value;
}

template <typename T>
void Array<T>::Insert(usize index, T value) {
    NX_ASSERT(index <= m_size, "Array::Insert index out of range.");
    GrowFor(m_size + 1);
    if (index == m_size) {
        ConstructAt<T>(m_data + m_size, ::Nexyl::Move(value));
    } else if constexpr (TriviallyCopyable<T>) {
        Memory::Move(static_cast<void*>(m_data + index + 1), static_cast<const void*>(m_data + index),
                     (m_size - index) * sizeof(T));
        ConstructAt<T>(m_data + index, ::Nexyl::Move(value));
    } else {
        ConstructAt<T>(m_data + m_size, ::Nexyl::Move(m_data[m_size - 1]));
        for (usize position = m_size - 1; position > index; --position) {
            m_data[position] = ::Nexyl::Move(m_data[position - 1]);
        }
        m_data[index] = ::Nexyl::Move(value);
    }
    ++m_size;
}

template <typename T>
void Array<T>::RemoveAt(usize index) {
    NX_ASSERT(index < m_size, "Array::RemoveAt index out of range.");
    if constexpr (TriviallyCopyable<T>) {
        Memory::Move(static_cast<void*>(m_data + index), static_cast<const void*>(m_data + index + 1),
                     (m_size - index - 1) * sizeof(T));
    } else {
        for (usize position = index; position + 1 < m_size; ++position) {
            m_data[position] = ::Nexyl::Move(m_data[position + 1]);
        }
        DestroyAt(m_data + m_size - 1);
    }
    --m_size;
}

template <typename T>
void Array<T>::RemoveAtSwap(usize index) {
    NX_ASSERT(index < m_size, "Array::RemoveAtSwap index out of range.");
    if (index != m_size - 1) {
        m_data[index] = ::Nexyl::Move(m_data[m_size - 1]);
    }
    DestroyAt(m_data + m_size - 1);
    --m_size;
}

template <typename T>
void Array<T>::Clear() {
    Detail::DestroyElements(m_data, m_size);
    m_size = 0;
}

template <typename T>
void Array<T>::ShrinkToFit() {
    if (m_size == m_capacity) {
        return;
    }
    if (m_size == 0) {
        m_allocator->Deallocate(static_cast<void*>(m_data), m_capacity * sizeof(T));
        m_data = nullptr;
        m_capacity = 0;
        return;
    }
    // Shrinking is an optimisation; if the allocator cannot provide the smaller block, the array keeps the larger.
    NX_IGNORE_RESULT(TryMoveTo(m_size));
}

template <typename T>
template <typename U>
Option<usize> Array<T>::IndexOf(const U& value) const {
    for (usize index = 0; index < m_size; ++index) {
        if (m_data[index] == value) {
            return index;
        }
    }
    return kNone;
}

template <typename T>
bool Array<T>::operator==(const Array& other) const {
    if (m_size != other.m_size) {
        return false;
    }
    for (usize index = 0; index < m_size; ++index) {
        if (!(m_data[index] == other.m_data[index])) {
            return false;
        }
    }
    return true;
}

template <typename T>
usize Array<T>::GrownCapacity(usize minimum) const {
    constexpr usize kSmallestCapacity = 4;
    const usize doubled = m_capacity < kSmallestCapacity ? kSmallestCapacity : m_capacity * 2;
    return doubled < minimum ? minimum : doubled;
}

template <typename T>
Result<void, Error> Array<T>::TryMoveTo(usize capacity) {
    NX_ASSERT(m_allocator != nullptr, "An Array needs an allocator before it can hold elements.");
    NX_ASSERT(capacity >= m_size, "The new capacity must hold every element.");
    if (capacity > Memory::kMaxAllocationSize / sizeof(T)) {
        return Error(Memory::MemoryError::OutOfMemory);
    }
    if constexpr (TriviallyCopyable<T>) {
        // Plain bytes may move with the block, possibly without copying.
        if (m_data != nullptr) {
            NX_TRY_ASSIGN(void* const resized,
                          m_allocator->TryReallocate(static_cast<void*>(m_data), m_capacity * sizeof(T),
                                                     capacity * sizeof(T), alignof(T)));
            m_data = static_cast<T*>(resized);
            m_capacity = capacity;
            return {};
        }
    }
    NX_TRY_ASSIGN(void* const memory, m_allocator->TryAllocate(capacity * sizeof(T), alignof(T)));
    T* const data = static_cast<T*>(memory);
    Detail::RelocateElements(data, m_data, m_size);
    if (m_data != nullptr) {
        m_allocator->Deallocate(static_cast<void*>(m_data), m_capacity * sizeof(T));
    }
    m_data = data;
    m_capacity = capacity;
    return {};
}

template <typename T>
void Array<T>::GrowFor(usize minimum) {
    if (minimum > m_capacity) {
        Reserve(GrownCapacity(minimum));
    }
}

template <typename T>
void Array<T>::Release() {
    Detail::DestroyElements(m_data, m_size);
    if (m_data != nullptr) {
        m_allocator->Deallocate(static_cast<void*>(m_data), m_capacity * sizeof(T));
    }
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
}

} // namespace Nexyl::Containers
