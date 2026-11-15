#pragma once

#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryErrors.h>

// Allocation that the caller cannot do without: on failure the out-of-memory handler reports, then the process
// terminates (docs/NEXYL_ARCHITECTURE.md, section 5.8). Use IAllocator::TryAllocate where the caller can degrade.

namespace Nexyl::Memory {

/// Describes an allocation that failed, for the out-of-memory handler.
/// @threadsafety safe
/// @since 0.1
struct OutOfMemoryInfo {
    /// The allocator that could not provide the block.
    const IAllocator* allocator = nullptr;
    /// Requested size in bytes.
    usize size = 0;
    /// Requested alignment.
    usize alignment = 0;
    /// Why the allocator failed.
    Error error = Error(MemoryError::OutOfMemory);
};

/// Function called before the process terminates because an allocation failed. It writes a report, for example a
/// tagged memory summary; it cannot prevent the termination. It may be called from any thread, concurrently.
/// @since 0.1
using OutOfMemoryHandler = void (*)(const OutOfMemoryInfo& info);

/// Installs the out-of-memory handler. Without one, a failed allocation terminates through NX_FATAL directly.
/// @param handler The new handler, or nullptr to remove the current one.
/// @return The previously installed handler, or nullptr.
/// @threadsafety safe
/// @since 0.1
NX_MEMORY_API OutOfMemoryHandler SetOutOfMemoryHandler(OutOfMemoryHandler handler);

/// Reports an allocation the caller cannot do without as failed: calls the out-of-memory handler, then terminates
/// through NX_FATAL. For containers and allocators whose own growth failed through a Try function.
/// @param info What failed.
/// @threadsafety safe
/// @since 0.1
[[noreturn]] NX_MEMORY_API void ReportOutOfMemory(const OutOfMemoryInfo& info);

/// Allocates a block or terminates: the handler reports the failure, then NX_FATAL ends the process.
/// @param allocator The allocator.
/// @param size      Bytes. Not zero.
/// @param alignment A power of two.
/// @return The block, never null.
/// @threadsafety As the allocator's TryAllocate.
/// @since 0.1
[[nodiscard]] NX_MEMORY_API void* Allocate(IAllocator& allocator, usize size, usize alignment = kDefaultAlignment);

/// Resizes a block or terminates, like Allocate.
/// @param allocator The allocator that produced `block`.
/// @param block     The block.
/// @param oldSize   Its size.
/// @param newSize   The new size. Not zero.
/// @param alignment The alignment it was allocated with.
/// @return The block, possibly moved, never null.
/// @threadsafety As the allocator's TryReallocate.
/// @since 0.1
[[nodiscard]] NX_MEMORY_API void* Reallocate(IAllocator& allocator, void* block, usize oldSize, usize newSize,
                                             usize alignment = kDefaultAlignment);

/// Creates an object in memory from `allocator`, terminating if the memory is not available.
///
/// Release it with Delete and the same allocator, or own it with UniquePtr (MakeUnique) instead.
/// @param allocator The allocator.
/// @param arguments Constructor arguments.
/// @return The new object.
/// @threadsafety As the allocator.
/// @since 0.1
template <typename T, typename... TArgs>
[[nodiscard]] T* New(IAllocator& allocator, TArgs&&... arguments) {
    void* const memory = Allocate(allocator, sizeof(T), alignof(T));
    return ConstructAt<T>(memory, ::Nexyl::Forward<TArgs>(arguments)...);
}

/// Destroys an object created with New and returns its memory to the allocator.
/// @param allocator The allocator `object` was created with.
/// @param object    The object, or null, which is ignored.
/// @threadsafety As the allocator.
/// @since 0.1
template <typename T>
void Delete(IAllocator& allocator, T* object) {
    if (object == nullptr) {
        return;
    }
    DestroyAt(object);
    allocator.Deallocate(object, sizeof(T));
}

} // namespace Nexyl::Memory
