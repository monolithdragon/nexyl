#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Types.h>

// Shared by the floating-point formatter and parser; not part of the public API.

namespace Nexyl::Detail {

// An unsigned integer of up to 32 * TLimbs bits, on the stack. Exact arithmetic for decimal conversion.
template <usize TLimbs>
class BigInteger final {
public:
    void Set(u64 value) {
        m_count = 0;
        while (value != 0) {
            m_limbs[m_count++] = static_cast<u32>(value);
            value >>= 32U;
        }
    }

    void MultiplySmall(u32 factor) {
        u64 carry = 0;
        for (usize index = 0; index < m_count; ++index) {
            const u64 product = (u64{m_limbs[index]} * factor) + carry;
            m_limbs[index] = static_cast<u32>(product);
            carry = product >> 32U;
        }
        if (carry != 0) {
            NX_ASSERT(m_count < kMaxLimbs, "BigInteger overflow.");
            m_limbs[m_count++] = static_cast<u32>(carry);
        }
    }

    void MultiplyPowerOfTen(u32 exponent) {
        constexpr u32 kPowersOfTen[] = {1U,       10U,        100U,        1'000U,      10'000U,
                                        100'000U, 1'000'000U, 10'000'000U, 100'000'000U};
        constexpr u32 kBillion = 1'000'000'000U;
        while (exponent >= 9) {
            MultiplySmall(kBillion);
            exponent -= 9;
        }
        if (exponent > 0) {
            MultiplySmall(kPowersOfTen[exponent]);
        }
    }

    void ShiftLeft(u32 bits) {
        if (m_count == 0) {
            return;
        }
        const u32 limbShift = bits / 32U;
        const u32 bitShift = bits % 32U;
        NX_ASSERT(m_count + limbShift + 1 <= kMaxLimbs, "BigInteger overflow.");
        if (bitShift != 0) {
            u32 carry = 0;
            for (usize index = 0; index < m_count; ++index) {
                const u32 next = m_limbs[index] >> (32U - bitShift);
                m_limbs[index] = (m_limbs[index] << bitShift) | carry;
                carry = next;
            }
            if (carry != 0) {
                m_limbs[m_count++] = carry;
            }
        }
        if (limbShift != 0) {
            for (usize index = m_count; index-- > 0;) {
                m_limbs[index + limbShift] = m_limbs[index];
            }
            for (usize index = 0; index < limbShift; ++index) {
                m_limbs[index] = 0;
            }
            m_count += limbShift;
        }
    }

    void AddSmall(u32 value) {
        u64 carry = value;
        for (usize index = 0; index < m_count && carry != 0; ++index) {
            const u64 sum = u64{m_limbs[index]} + carry;
            m_limbs[index] = static_cast<u32>(sum);
            carry = sum >> 32U;
        }
        if (carry != 0) {
            NX_ASSERT(m_count < kMaxLimbs, "BigInteger overflow.");
            m_limbs[m_count++] = static_cast<u32>(carry);
        }
    }

    void Add(const BigInteger& other) {
        const usize count = m_count > other.m_count ? m_count : other.m_count;
        u64 carry = 0;
        for (usize index = 0; index < count; ++index) {
            const u64 sum = u64{index < m_count ? m_limbs[index] : 0U} +
                            u64{index < other.m_count ? other.m_limbs[index] : 0U} + carry;
            m_limbs[index] = static_cast<u32>(sum);
            carry = sum >> 32U;
        }
        m_count = count;
        if (carry != 0) {
            NX_ASSERT(m_count < kMaxLimbs, "BigInteger overflow.");
            m_limbs[m_count++] = static_cast<u32>(carry);
        }
    }

    // Requires *this >= other.
    void Subtract(const BigInteger& other) {
        u64 borrow = 0;
        for (usize index = 0; index < m_count; ++index) {
            const u64 subtrahend = u64{index < other.m_count ? other.m_limbs[index] : 0U} + borrow;
            const u64 minuend = m_limbs[index];
            if (minuend >= subtrahend) {
                m_limbs[index] = static_cast<u32>(minuend - subtrahend);
                borrow = 0;
            } else {
                m_limbs[index] = static_cast<u32>((minuend + (u64{1} << 32U)) - subtrahend);
                borrow = 1;
            }
        }
        NX_ASSERT(borrow == 0, "BigInteger subtraction underflow.");
        while (m_count > 0 && m_limbs[m_count - 1] == 0) {
            --m_count;
        }
    }

    // Replaces *this with the remainder of the division by `divisor` and returns the quotient, which is below 10.
    [[nodiscard]] u32 DivideSmall(const BigInteger& divisor) {
        u32 quotient = 0;
        while (Compare(*this, divisor) >= 0) {
            Subtract(divisor);
            ++quotient;
        }
        NX_ASSERT(quotient < 10, "Digit out of range.");
        return quotient;
    }

    [[nodiscard]] static i32 Compare(const BigInteger& first, const BigInteger& second) {
        if (first.m_count != second.m_count) {
            return first.m_count < second.m_count ? -1 : 1;
        }
        for (usize index = first.m_count; index-- > 0;) {
            if (first.m_limbs[index] != second.m_limbs[index]) {
                return first.m_limbs[index] < second.m_limbs[index] ? -1 : 1;
            }
        }
        return 0;
    }

private:
    static constexpr usize kMaxLimbs = TLimbs;
    u32 m_limbs[kMaxLimbs] = {};
    usize m_count = 0;
};

} // namespace Nexyl::Detail
