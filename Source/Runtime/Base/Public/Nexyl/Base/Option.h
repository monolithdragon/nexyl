#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Std/New.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl {

/// Tag type of kNone.
/// @since 0.1
struct NoneType {
    /// Private-by-convention constructor, so that `{}` does not silently mean "none".
    constexpr explicit NoneType(int /*unused*/) noexcept {}
};

/// The empty state of an Option: `return kNone;`.
/// @since 0.1
inline constexpr NoneType kNone{0};

/// A value that may be absent, where absence is a normal outcome (docs/CODING_STANDARDS.md, section 5.2).
/// It is not an error channel; use Result for failures.
///
/// ```cpp
/// Option<u32> FindIndex(Span<const u32> values, u32 wanted);
/// const Option<u32> index = FindIndex(values, 7);
/// if (index.HasValue()) { Use(index.GetValue()); }
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename T>
class Option final {
    static_assert(!LvalueReference<T>, "Option does not hold references. Use a pointer or a handle.");

public:
    /// Creates an empty option. @since 0.1
    constexpr Option() noexcept : m_empty(0) {}

    // The three converting constructors below are implicit by design, so that `return kNone;` and
    // `return value;` work. This is the documented exception to rule CLS-6.

    /// Creates an empty option. @since 0.1
    constexpr explicit(false) Option(NoneType /*none*/) noexcept : m_empty(0) {}

    /// Creates an option holding a copy of `value`. @since 0.1
    constexpr explicit(false) Option(const T& value)
        requires CopyConstructible<T>
        : m_value(value), m_hasValue(true) {}

    /// Creates an option holding `value`, moved in. @since 0.1
    constexpr explicit(false) Option(T&& value) noexcept : m_value(Move(value)), m_hasValue(true) {}

    /// Copies another option. @since 0.1
    constexpr Option(const Option& other)
        requires CopyConstructible<T>
        : m_empty(0) {
        if (other.m_hasValue) {
            Construct(other.m_value);
        }
    }

    /// Moves from another option. `other` keeps its state, with its value moved from. @since 0.1
    constexpr Option(Option&& other) noexcept : m_empty(0) {
        if (other.m_hasValue) {
            Construct(Move(other.m_value));
        }
    }

    /// Copies another option. @since 0.1
    constexpr Option& operator=(const Option& other)
        requires CopyConstructible<T>
    {
        if (this != &other) {
            Reset();
            if (other.m_hasValue) {
                Construct(other.m_value);
            }
        }
        return *this;
    }

    /// Moves from another option. @since 0.1
    constexpr Option& operator=(Option&& other) noexcept {
        if (this != &other) {
            Reset();
            if (other.m_hasValue) {
                Construct(Move(other.m_value));
            }
        }
        return *this;
    }

    /// Destroys the held value, if any. @since 0.1
    constexpr ~Option() {
        Reset();
    }

    /// Checks whether a value is present.
    /// @return True if the option holds a value.
    /// @since 0.1
    [[nodiscard]] constexpr bool HasValue() const noexcept {
        return m_hasValue;
    }

    /// Returns the held value.
    /// @pre HasValue() is true.
    /// @return A reference to the value.
    /// @since 0.1
    [[nodiscard]] constexpr T& GetValue() & noexcept {
        NX_ASSERT(m_hasValue, "Option::GetValue called on an empty option.");
        return m_value;
    }

    /// Returns the held value.
    /// @pre HasValue() is true.
    /// @return A reference to the value.
    /// @since 0.1
    [[nodiscard]] constexpr const T& GetValue() const& noexcept {
        NX_ASSERT(m_hasValue, "Option::GetValue called on an empty option.");
        return m_value;
    }

    /// Moves the held value out. The option keeps a moved-from value.
    /// @pre HasValue() is true.
    /// @return The value.
    /// @since 0.1
    [[nodiscard]] constexpr T TakeValue() && noexcept {
        NX_ASSERT(m_hasValue, "Option::TakeValue called on an empty option.");
        return Move(m_value);
    }

    /// Returns the held value, or `fallback` when empty.
    /// @param fallback The value to return when the option is empty.
    /// @return A copy of the held value, or `fallback`.
    /// @since 0.1
    [[nodiscard]] constexpr T ValueOr(T fallback) const&
        requires CopyConstructible<T>
    {
        return m_hasValue ? m_value : Move(fallback);
    }

    /// Destroys any held value and constructs a new one in place.
    /// @param arguments Constructor arguments for T.
    /// @return A reference to the new value.
    /// @since 0.1
    template <typename... TArgs>
    constexpr T& Emplace(TArgs&&... arguments) {
        Reset();
        Construct(Forward<TArgs>(arguments)...);
        return m_value;
    }

    /// Destroys any held value and leaves the option empty. @since 0.1
    constexpr void Reset() noexcept {
        if (m_hasValue) {
            m_value.~T();
            m_hasValue = false;
        }
    }

private:
    template <typename... TArgs>
    constexpr void Construct(TArgs&&... arguments) {
        ::new (static_cast<void*>(&m_value)) T(Forward<TArgs>(arguments)...);
        m_hasValue = true;
    }

    union {
        u8 m_empty;
        T m_value;
    };
    bool m_hasValue = false;
};

} // namespace Nexyl
