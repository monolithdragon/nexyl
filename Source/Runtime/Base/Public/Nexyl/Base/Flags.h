#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// A set of values of an enumeration, stored as bits (rule CLS-10).
///
/// Each enumerator names one bit: its value is the bit position, from 0 to 63. The enumeration stays a plain
/// `enum class` with consecutive values; no operators are defined on it.
///
/// ```cpp
/// enum class FieldFlag : u8 { Public, Transient, ReadOnly };
/// constexpr Flags<FieldFlag> kFlags{FieldFlag::Public, FieldFlag::ReadOnly};
/// static_assert(kFlags.Has(FieldFlag::ReadOnly) && !kFlags.Has(FieldFlag::Transient));
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename TEnum>
    requires Enumeration<TEnum>
class Flags final {
public:
    /// Creates an empty set. @since 0.1
    constexpr Flags() = default;

    /// Creates a set of the given values.
    /// @param values The values in the set; repeated values are allowed.
    /// @since 0.1
    template <typename... TValues>
        requires(sizeof...(TValues) > 0 && (SameAs<TValues, TEnum> && ...))
    constexpr explicit Flags(TValues... values) : m_bits((BitOf(values) | ...)) {}

    /// Creates a set from its bit representation.
    /// @param bits Bit `n` set means the enumerator with value `n` is in the set.
    /// @return The set.
    /// @since 0.1
    [[nodiscard]] static constexpr Flags FromBits(u64 bits) {
        Flags flags;
        flags.m_bits = bits;
        return flags;
    }

    /// Tells whether a value is in the set.
    /// @param value The value.
    /// @return True if it is.
    /// @since 0.1
    [[nodiscard]] constexpr bool Has(TEnum value) const {
        return (m_bits & BitOf(value)) != 0;
    }

    /// Tells whether the set is empty.
    /// @return True if no value is in the set.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsEmpty() const {
        return m_bits == 0;
    }

    /// Adds a value.
    /// @param value The value.
    /// @since 0.1
    constexpr void Set(TEnum value) {
        m_bits |= BitOf(value);
    }

    /// Removes a value.
    /// @param value The value.
    /// @since 0.1
    constexpr void Clear(TEnum value) {
        m_bits &= ~BitOf(value);
    }

    /// Returns the bit representation.
    /// @return Bit `n` is set if the enumerator with value `n` is in the set.
    /// @since 0.1
    [[nodiscard]] constexpr u64 GetBits() const {
        return m_bits;
    }

    /// Returns the union of two sets.
    /// @param other The other set.
    /// @return Every value that is in either set.
    /// @since 0.1
    [[nodiscard]] constexpr Flags operator|(Flags other) const {
        return FromBits(m_bits | other.m_bits);
    }

    /// Returns the intersection of two sets.
    /// @param other The other set.
    /// @return Every value that is in both sets.
    /// @since 0.1
    [[nodiscard]] constexpr Flags operator&(Flags other) const {
        return FromBits(m_bits & other.m_bits);
    }

    /// Compares two sets.
    /// @return True if they hold the same values.
    /// @since 0.1
    constexpr bool operator==(const Flags&) const = default;

private:
    [[nodiscard]] static constexpr u64 BitOf(TEnum value) {
        const auto position = static_cast<u64>(static_cast<UnderlyingType<TEnum>>(value));
        NX_ASSERT(position < 64, "A Flags enumerator must have a value from 0 to 63.");
        return u64{1} << position;
    }

    u64 m_bits = 0;
};

} // namespace Nexyl
