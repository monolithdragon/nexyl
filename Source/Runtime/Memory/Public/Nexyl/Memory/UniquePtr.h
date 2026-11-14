#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

template <typename T>
class UniquePtr;

template <typename T, typename... TArgs>
[[nodiscard]] UniquePtr<T> MakeUnique(IAllocator& allocator, TArgs&&... arguments);

/// Sole owner of an object, which it destroys with the allocator that created it (docs/CODING_STANDARDS.md,
/// section 6.1).
///
/// Created only by MakeUnique, so the object and its allocator always match. Movable, not copyable. It owns a
/// complete `T`: there is no conversion to a pointer to a base class, because sized deallocation needs the exact
/// type, and interfaces are released by their owning system (rule OWN-8).
///
/// ```cpp
/// UniquePtr<Mesh> mesh = MakeUnique<Mesh>(allocator, vertexCount);
/// mesh->Upload();
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename T>
class UniquePtr final {
public:
    /// Creates an empty pointer. @since 0.1
    constexpr UniquePtr() = default;

    /// Takes over the object of `other`, which becomes empty. @since 0.1
    UniquePtr(UniquePtr&& other) noexcept
        : m_object(Exchange(other.m_object, nullptr)), m_allocator(Exchange(other.m_allocator, nullptr)) {}

    /// Destroys the current object, then takes over the object of `other`, which becomes empty. @since 0.1
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_object = Exchange(other.m_object, nullptr);
            m_allocator = Exchange(other.m_allocator, nullptr);
        }
        return *this;
    }

    /// Destroys the object, if any. @since 0.1
    ~UniquePtr() {
        Reset();
    }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    /// Returns the object, or null. The pointer does not own it. @since 0.1
    [[nodiscard]] T* Get() const {
        return m_object;
    }

    /// Returns the allocator that created the object, or null when empty. @since 0.1
    [[nodiscard]] IAllocator* GetAllocator() const {
        return m_allocator;
    }

    /// Checks whether an object is owned. @since 0.1
    [[nodiscard]] explicit operator bool() const {
        return m_object != nullptr;
    }

    /// Accesses the object. @pre The pointer is not empty. @since 0.1
    [[nodiscard]] T& operator*() const {
        NX_ASSERT(m_object != nullptr, "Dereferencing an empty UniquePtr.");
        return *m_object;
    }

    /// Accesses a member of the object. @pre The pointer is not empty. @since 0.1
    [[nodiscard]] T* operator->() const {
        NX_ASSERT(m_object != nullptr, "Dereferencing an empty UniquePtr.");
        return m_object;
    }

    /// Destroys the object, if any, and leaves the pointer empty. @since 0.1
    void Reset() {
        if (m_object != nullptr) {
            Delete(*m_allocator, Exchange(m_object, nullptr));
            m_allocator = nullptr;
        }
    }

private:
    template <typename U, typename... TArgs>
    friend UniquePtr<U> MakeUnique(IAllocator& allocator, TArgs&&... arguments);

    UniquePtr(T* object, IAllocator& allocator) : m_object(object), m_allocator(&allocator) {}

    T* m_object = nullptr;
    IAllocator* m_allocator = nullptr;
};

/// Creates an object in memory from `allocator` and returns its owner. Terminates if the memory is not available.
/// @param allocator The allocator. It must outlive the returned pointer.
/// @param arguments Constructor arguments.
/// @return The owner of the new object.
/// @threadsafety As the allocator.
/// @since 0.1
template <typename T, typename... TArgs>
[[nodiscard]] UniquePtr<T> MakeUnique(IAllocator& allocator, TArgs&&... arguments) {
    return UniquePtr<T>(New<T>(allocator, ::Nexyl::Forward<TArgs>(arguments)...), allocator);
}

} // namespace Nexyl::Memory
