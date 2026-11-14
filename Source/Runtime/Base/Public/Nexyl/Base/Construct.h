#pragma once

#include <Nexyl/Base/Std/New.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl {

/// Constructs an object in memory the caller already owns. Allocates nothing.
///
/// This is the engine's placement new: modules other than Base cannot include <new> (rule STL-1).
///
/// @param address   Suitably sized and aligned storage for a `T`.
/// @param arguments Constructor arguments.
/// @return A pointer to the new object.
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename... TArgs>
T* ConstructAt(void* address, TArgs&&... arguments) {
    return ::new (address) T(Forward<TArgs>(arguments)...);
}

/// Destroys an object created with ConstructAt without releasing its memory.
///
/// @param object The object to destroy.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
void DestroyAt(T* object) noexcept {
    object->~T();
}

} // namespace Nexyl
