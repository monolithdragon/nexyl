"""Computes the reference fingerprints of the number parsing tests in Base.

Usage:
    python Scripts/format/parse_fingerprints.py

Prints the constants that `Source/Runtime/Base/Tests/ParseTests.cpp` compares against. The test and this script
build the same decimal strings from SplitMix64 and parse them; the fingerprint covers the resulting bits, with
a marker for overflow. Uses only the Python standard library: `float()` rounds correctly to f64, and f32 is
rounded here from the exact rational value, half to even.
"""

from __future__ import annotations

import struct
from fractions import Fraction

SEED = 0x4E58_5041_5253_0001
COUNT = 20_000
MASK64 = (1 << 64) - 1
OVERFLOW = MASK64


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

    def add(self, bits: int) -> None:
        for byte in bits.to_bytes(8, "little"):
            self.value ^= byte
            self.value = (self.value * 0x100_0000_01B3) & MASK64


def make_text(generator: SplitMix64) -> str:
    """A decimal string: up to 40 digits, a decimal point somewhere or none, and an exponent from -360 to 360."""
    digit_count = 1 + generator.next() % 40
    digits = "".join(str(generator.next() % 10) for _ in range(digit_count))
    point = generator.next() % (digit_count + 1)
    if point not in (0, digit_count):
        digits = digits[:point] + "." + digits[point:]
    exponent = int(generator.next() % 721) - 360
    return f"{digits}e{exponent}"


def f64_bits(text: str) -> int:
    value = float(text)
    if value == float("inf"):
        return OVERFLOW
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def f32_bits(text: str) -> int:
    value = Fraction(text)
    if value == 0:
        return 0
    exponent = value.numerator.bit_length() - value.denominator.bit_length() - 24
    while Fraction(2) ** exponent * (1 << 24) <= value:
        exponent += 1
    while Fraction(2) ** exponent * (1 << 23) > value:
        exponent -= 1
    exponent = max(exponent, -149)
    scaled = value / Fraction(2) ** exponent
    mantissa = scaled.numerator // scaled.denominator
    remainder = scaled - mantissa
    if remainder > Fraction(1, 2) or (remainder == Fraction(1, 2) and mantissa % 2 == 1):
        mantissa += 1
    if mantissa == 1 << 24:
        mantissa >>= 1
        exponent += 1
    if exponent > 104:
        return OVERFLOW
    if mantissa < 1 << 23:
        return mantissa
    return ((exponent + 150) << 23) | (mantissa - (1 << 23))


def main() -> None:
    generator = SplitMix64(SEED)
    f64 = Fingerprint()
    f32 = Fingerprint()
    for _ in range(COUNT):
        text = make_text(generator)
        f64.add(f64_bits(text))
        f32.add(f32_bits(text))
    print(f"kF64ParseFingerprint = 0x{f64.value:016X}")
    print(f"kF32ParseFingerprint = 0x{f32.value:016X}")


if __name__ == "__main__":
    main()
