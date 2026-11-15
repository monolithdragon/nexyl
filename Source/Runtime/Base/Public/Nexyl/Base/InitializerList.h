#pragma once

#include <Nexyl/Base/Std/InitializerList.h>

namespace Nexyl {

/// The type of a braced list of values, such as `{1, 2, 3}`. The language ties brace initialization to this type,
/// so containers take it to be constructible from a list.
///
/// ```cpp
/// Containers::Array<u32> values(allocator, {1, 2, 3});
/// ```
/// @since 0.1
template <typename T>
using InitializerList = std::initializer_list<T>;

} // namespace Nexyl
