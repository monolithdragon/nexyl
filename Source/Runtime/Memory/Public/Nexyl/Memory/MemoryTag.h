#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Memory {

/// Names who owns an allocation, for budgets and leak reports (docs/NEXYL_ARCHITECTURE.md, section 5.8).
///
/// A tag holds the owning module's ModuleId in the high 16 bits and a sub-tag that the module defines in the low
/// 16 bits, like an ErrorCode. Tags are therefore unique without a central list, and Memory needs to know nothing
/// about the modules above it. Sub-tag zero stands for the module as a whole; the value zero means untagged.
///
/// ```cpp
/// inline constexpr MemoryTag kContactsTag = MemoryTag::Make(ModuleId::Physics, 1);
/// ```
/// @threadsafety safe
/// @since 0.1
struct MemoryTag {
    /// The packed tag.
    u32 value = 0;

    /// Builds a tag.
    /// @param module The owning module.
    /// @param subTag A module-defined subdivision, or zero for the whole module.
    /// @return The tag.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] static constexpr MemoryTag Make(ModuleId module, u16 subTag = 0) {
        return MemoryTag{(static_cast<u32>(module) << 16U) | subTag};
    }

    /// Returns the owning module. @since 0.1
    [[nodiscard]] constexpr ModuleId GetModule() const {
        return static_cast<ModuleId>(value >> 16U);
    }

    /// Returns the module-defined sub-tag. @since 0.1
    [[nodiscard]] constexpr u16 GetSubTag() const {
        return static_cast<u16>(value & 0xFFFFU);
    }

    /// Checks whether no owner is named. Persistent heap allocations are never untagged (rule OWN-2). @since 0.1
    [[nodiscard]] constexpr bool IsUntagged() const {
        return value == 0;
    }

    /// Compares two tags. @since 0.1
    constexpr bool operator==(const MemoryTag&) const = default;
};

/// The sub-tag every module reserves for the third-party code it links (rule TP-5).
/// @since 0.1
inline constexpr u16 kThirdPartySubTag = 0xFFFF;

} // namespace Nexyl::Memory
