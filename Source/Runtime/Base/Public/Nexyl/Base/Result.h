#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Std/New.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl {

/// Either a value or an error: the return type of every function that can fail at run time
/// (docs/CODING_STANDARDS.md, section 5).
///
/// It converts implicitly from both `T` and `E`, so a function returns either directly. Propagate errors with
/// NX_TRY and NX_TRY_ASSIGN. A result cannot be dropped silently: discard one explicitly with NX_IGNORE_RESULT.
///
/// ```cpp
/// Result<u32, Error> ParseCount(StringView text) {
///     if (text.IsEmpty()) {
///         return Error(BaseError::InvalidArgument);
///     }
///     return count;
/// }
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename E = Error>
class [[nodiscard]] Result final {
    static_assert(!SameAs<RemoveCvRef<T>, RemoveCvRef<E>>, "The value and error types of a Result must differ.");
    static_assert(!LvalueReference<T>, "Result does not hold references. Use a pointer or a handle.");

public:
    // The four converting constructors below are implicit by design, so that a function can return either a
    // value or an error directly. This is the documented exception to rule CLS-6.

    /// Creates a successful result holding a copy of `value`. @since 0.1
    constexpr explicit(false) Result(const T& value)
        requires CopyConstructible<T>
        : m_value(value), m_hasValue(true) {}

    /// Creates a successful result holding `value`, moved in. @since 0.1
    constexpr explicit(false) Result(T&& value) noexcept : m_value(Move(value)), m_hasValue(true) {}

    /// Creates a failed result holding a copy of `error`. @since 0.1
    constexpr explicit(false) Result(const E& error) : m_error(error), m_hasValue(false) {}

    /// Creates a failed result holding `error`, moved in. @since 0.1
    constexpr explicit(false) Result(E&& error) noexcept : m_error(Move(error)), m_hasValue(false) {}

    /// Copies another result. @since 0.1
    constexpr Result(const Result& other)
        requires CopyConstructible<T>
        : m_hasValue(other.m_hasValue) {
        if (m_hasValue) {
            ::new (static_cast<void*>(&m_value)) T(other.m_value);
        } else {
            ::new (static_cast<void*>(&m_error)) E(other.m_error);
        }
    }

    /// Moves from another result. `other` keeps its state, with its content moved from. @since 0.1
    constexpr Result(Result&& other) noexcept : m_hasValue(other.m_hasValue) {
        if (m_hasValue) {
            ::new (static_cast<void*>(&m_value)) T(Move(other.m_value));
        } else {
            ::new (static_cast<void*>(&m_error)) E(Move(other.m_error));
        }
    }

    /// Copies another result. @since 0.1
    constexpr Result& operator=(const Result& other)
        requires CopyConstructible<T>
    {
        if (this != &other) {
            Destroy();
            m_hasValue = other.m_hasValue;
            if (m_hasValue) {
                ::new (static_cast<void*>(&m_value)) T(other.m_value);
            } else {
                ::new (static_cast<void*>(&m_error)) E(other.m_error);
            }
        }
        return *this;
    }

    /// Moves from another result. @since 0.1
    constexpr Result& operator=(Result&& other) noexcept {
        if (this != &other) {
            Destroy();
            m_hasValue = other.m_hasValue;
            if (m_hasValue) {
                ::new (static_cast<void*>(&m_value)) T(Move(other.m_value));
            } else {
                ::new (static_cast<void*>(&m_error)) E(Move(other.m_error));
            }
        }
        return *this;
    }

    /// Destroys the held value or error. @since 0.1
    constexpr ~Result() {
        Destroy();
    }

    /// Checks for success.
    /// @return True if the result holds a value.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsOk() const noexcept {
        return m_hasValue;
    }

    /// Checks for failure.
    /// @return True if the result holds an error.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsError() const noexcept {
        return !m_hasValue;
    }

    /// Returns the value.
    /// @pre IsOk() is true.
    /// @return A reference to the value.
    /// @since 0.1
    [[nodiscard]] constexpr T& GetValue() & noexcept {
        NX_ASSERT(m_hasValue, "Result::GetValue called on a failed result.");
        return m_value;
    }

    /// Returns the value.
    /// @pre IsOk() is true.
    /// @return A reference to the value.
    /// @since 0.1
    [[nodiscard]] constexpr const T& GetValue() const& noexcept {
        NX_ASSERT(m_hasValue, "Result::GetValue called on a failed result.");
        return m_value;
    }

    /// Moves the value out.
    /// @pre IsOk() is true.
    /// @return The value.
    /// @since 0.1
    [[nodiscard]] constexpr T TakeValue() && noexcept {
        NX_ASSERT(m_hasValue, "Result::TakeValue called on a failed result.");
        return Move(m_value);
    }

    /// Returns the value, or `fallback` on failure.
    /// @param fallback The value to return on failure.
    /// @return A copy of the value, or `fallback`.
    /// @since 0.1
    [[nodiscard]] constexpr T ValueOr(T fallback) const&
        requires CopyConstructible<T>
    {
        return m_hasValue ? m_value : Move(fallback);
    }

    /// Returns the error.
    /// @pre IsError() is true.
    /// @return A reference to the error.
    /// @since 0.1
    [[nodiscard]] constexpr const E& GetError() const& noexcept {
        NX_ASSERT(!m_hasValue, "Result::GetError called on a successful result.");
        return m_error;
    }

private:
    constexpr void Destroy() noexcept {
        if (m_hasValue) {
            m_value.~T();
        } else {
            m_error.~E();
        }
    }

    union {
        T m_value;
        E m_error;
    };
    bool m_hasValue;
};

/// Result of an operation that produces no value: success, or an error.
///
/// Default construction means success, so `return {};` reports success.
/// @threadsafety compatible
/// @since 0.1
template <typename E>
class [[nodiscard]] Result<void, E> final {
public:
    /// Creates a successful result. @since 0.1
    constexpr Result() noexcept = default;

    // Implicit by design; the documented exception to rule CLS-6.

    /// Creates a failed result holding a copy of `error`. @since 0.1
    constexpr explicit(false) Result(const E& error) : m_error(error) {}

    /// Creates a failed result holding `error`, moved in. @since 0.1
    constexpr explicit(false) Result(E&& error) noexcept : m_error(Move(error)) {}

    /// Checks for success.
    /// @return True if the operation succeeded.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsOk() const noexcept {
        return !m_error.HasValue();
    }

    /// Checks for failure.
    /// @return True if the operation failed.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsError() const noexcept {
        return m_error.HasValue();
    }

    /// Returns the error.
    /// @pre IsError() is true.
    /// @return A reference to the error.
    /// @since 0.1
    [[nodiscard]] constexpr const E& GetError() const& noexcept {
        NX_ASSERT(m_error.HasValue(), "Result::GetError called on a successful result.");
        return m_error.GetValue();
    }

private:
    Option<E> m_error;
};

} // namespace Nexyl

/// Evaluates an expression that yields a Result. On failure, returns its error from the enclosing function,
/// which must itself return a Result with the same error type (docs/CODING_STANDARDS.md, section 5.3).
/// @since 0.1
#define NX_TRY(expression)                                                                                             \
    do {                                                                                                               \
        const auto nxTryResult = (expression);                                                                         \
        if (nxTryResult.IsError()) [[unlikely]] {                                                                      \
            return nxTryResult.GetError();                                                                             \
        }                                                                                                              \
    } while (false)

/// Evaluates an expression that yields a Result<T>. On success, declares `declaration` and direct-initialises it
/// with the value; on failure, returns the error from the enclosing function (docs/CODING_STANDARDS.md, 5.3):
///
/// ```cpp
/// NX_TRY_ASSIGN(File file, File::Open(path));
/// NX_TRY_ASSIGN(const auto header, ReadHeader(file));
/// ```
///
/// At most one NX_TRY_ASSIGN may appear per source line, because the temporary is named after the line.
/// @since 0.1
#define NX_TRY_ASSIGN(declaration, expression)                                                                         \
    auto NX_CONCAT(nxTryResult, __LINE__) = (expression);                                                              \
    if (NX_CONCAT(nxTryResult, __LINE__).IsError()) [[unlikely]] {                                                     \
        return NX_CONCAT(nxTryResult, __LINE__).GetError();                                                            \
    }                                                                                                                  \
    declaration(::Nexyl::Move(NX_CONCAT(nxTryResult, __LINE__)).TakeValue())

/// Discards a Result deliberately (rule ERR-3). Write a comment next to it that says why ignoring is safe.
/// @since 0.1
#define NX_IGNORE_RESULT(expression) static_cast<void>(expression)
