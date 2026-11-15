"""Computes the reference fingerprints of the floating-point formatting tests in Base.

Usage:
    python Scripts/format/format_fingerprints.py

Prints the constants that `Source/Runtime/Base/Tests/FormatFloatTests.cpp` compares against. Uses only the Python
standard library: `repr` gives the shortest string that reads back to the same f64, and `%e`, `%f` and `%g` round
the exact value correctly, half to even. The script must produce the same values sequence as the test:
SplitMix64 from the seed below, non-finite patterns skipped.
"""

from __future__ import annotations

import struct
from decimal import Decimal

SEED = 0x4E58_464D_5430_0001
COUNT = 10_000
MASK64 = (1 << 64) - 1


class SplitMix64:
    def __init__(self, seed: int) -> None:
        self.state = seed

    def next(self) -> int:
        self.state = (self.state + 0x9E37_79B9_7F4A_7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58_476D_1CE4_E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D0_49BB_1331_11EB) & MASK64
        return z ^ (z >> 31)


class Fingerprint:
    def __init__(self) -> None:
        self.value = 0xCBF2_9CE4_8422_2325

    def add(self, text: str) -> None:
        for byte in (text + "\n").encode("ascii"):
            self.value ^= byte
            self.value = (self.value * 0x100_0000_01B3) & MASK64


def render_shortest(negative: bool, digits: str, exponent: int) -> str:
    """Formats 0.digits * 10^exponent the way Base does: fixed or scientific, whichever is shorter."""
    count = len(digits)
    scientific_exponent = exponent - 1
    exponent_digits = 3 if abs(scientific_exponent) >= 100 else 2
    scientific_length = count + (1 if count > 1 else 0) + 2 + exponent_digits
    if exponent <= 0:
        fixed_length = 2 - exponent + count
    elif exponent >= count:
        fixed_length = exponent
    else:
        fixed_length = count + 1
    if fixed_length <= scientific_length:
        if exponent <= 0:
            body = "0." + "0" * (-exponent) + digits
        elif exponent >= count:
            body = digits + "0" * (exponent - count)
        else:
            body = digits[:exponent] + "." + digits[exponent:]
    else:
        mantissa = digits[0] + ("." + digits[1:] if count > 1 else "")
        sign = "-" if scientific_exponent < 0 else "+"
        body = f"{mantissa}e{sign}{abs(scientific_exponent):0{exponent_digits}d}"
    return ("-" if negative else "") + body


def decimal_digits(text: str) -> tuple[str, int]:
    """Returns the significant digits of a decimal string and k such that its value is 0.digits * 10^k."""
    sign, digit_tuple, exponent = Decimal(text).as_tuple()
    digits = "".join(str(digit) for digit in digit_tuple)
    k = len(digits) + exponent
    stripped = digits.rstrip("0") or "0"
    return stripped, k


def shortest_f64(value: float) -> str:
    if value == 0.0:
        return "-0" if struct.pack(">d", value)[0] & 0x80 else "0"
    digits, k = decimal_digits(repr(abs(value)))
    return render_shortest(value < 0, digits, k)


def to_f32(value: float) -> float:
    return struct.unpack("<f", struct.pack("<f", value))[0]


def shortest_f32(value: float) -> str:
    if value == 0.0:
        return "-0" if struct.pack(">d", value)[0] & 0x80 else "0"
    magnitude = abs(value)
    for precision in range(1, 10):
        candidate = "%.*e" % (precision - 1, magnitude)
        if to_f32(float(candidate)) == magnitude:
            digits, k = decimal_digits(candidate)
            return render_shortest(value < 0, digits, k)
    raise AssertionError("no f32 round-trip within 9 digits")


def main() -> None:
    generator = SplitMix64(SEED)
    f64_shortest = Fingerprint()
    f64_scientific = Fingerprint()
    f64_general = Fingerprint()
    f64_fixed = Fingerprint()
    produced = 0
    while produced < COUNT:
        bits = generator.next()
        biased = (bits >> 52) & 0x7FF
        if biased == 0x7FF:
            continue
        value = struct.unpack("<d", struct.pack("<Q", bits))[0]
        f64_shortest.add(shortest_f64(value))
        f64_scientific.add("%.17e" % value)
        f64_general.add("%.6g" % value)
        # Fixed notation only for moderate magnitudes; it prints every integer digit.
        if abs(biased - 1023) <= 70:
            f64_fixed.add("%.3f" % value)
        produced += 1

    f32_shortest = Fingerprint()
    f32_scientific = Fingerprint()
    produced = 0
    while produced < COUNT:
        bits = generator.next() >> 32
        if (bits >> 23) & 0xFF == 0xFF:
            continue
        value = struct.unpack("<f", struct.pack("<I", bits))[0]
        f32_shortest.add(shortest_f32(value))
        f32_scientific.add("%.8e" % value)
        produced += 1

    print(f"kF64ShortestFingerprint   = 0x{f64_shortest.value:016X}")
    print(f"kF64ScientificFingerprint = 0x{f64_scientific.value:016X}")
    print(f"kF64GeneralFingerprint    = 0x{f64_general.value:016X}")
    print(f"kF64FixedFingerprint      = 0x{f64_fixed.value:016X}")
    print(f"kF32ShortestFingerprint   = 0x{f32_shortest.value:016X}")
    print(f"kF32ScientificFingerprint = 0x{f32_scientific.value:016X}")


if __name__ == "__main__":
    main()
