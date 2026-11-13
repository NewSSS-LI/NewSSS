"""Compare the compiled C codec with Python's independent IEEE binary16 codec.

Usage: python verify_float16.py <path-to-codec-dll-or-shared-library>
The production model never imports Python or this oracle.
"""
import ctypes
import math
from pathlib import Path
import struct
import sys


def reference_encode(value):
    if math.isnan(value):
        return 0x7E00
    try:
        return struct.unpack('<H', struct.pack('<e', value))[0]
    except OverflowError:
        return 0xFC00 if math.copysign(1.0, value) < 0 else 0x7C00


def main():
    library = ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
    encode = library.LeaFtlFloat16Encode
    encode.argtypes = [ctypes.c_double]
    encode.restype = ctypes.c_uint16
    decode = library.LeaFtlFloat16Decode
    decode.argtypes = [ctypes.c_uint16]
    decode.restype = ctypes.c_double
    checks = 0
    for bits in range(65536):
        expected = struct.unpack('<e', struct.pack('<H', bits))[0]
        actual = decode(bits)
        if math.isnan(expected):
            assert math.isnan(actual), hex(bits)
        else:
            assert actual == expected, (hex(bits), actual, expected)
            if expected == 0:
                assert math.copysign(1.0, actual) == math.copysign(1.0, expected)
        assert encode(expected) == reference_encode(expected), hex(bits)
        checks += 2

    # Every adjacent positive finite half pair, and their negative mirrors:
    # exact midpoint and immediately neighboring double values test RNE ties.
    for bits in range(0x7BFF):
        lo = struct.unpack('<e', struct.pack('<H', bits))[0]
        hi = struct.unpack('<e', struct.pack('<H', bits + 1))[0]
        midpoint = (lo + hi) * 0.5
        for value in (math.nextafter(midpoint, -math.inf), midpoint,
                      math.nextafter(midpoint, math.inf)):
            for signed_value in (value, -value):
                actual = encode(signed_value)
                expected = reference_encode(signed_value)
                assert actual == expected, (signed_value, hex(actual), hex(expected))
                checks += 1

    for value in (65520.0, math.nextafter(65520.0, 0.0),
                  math.nextafter(65520.0, math.inf), 1e300,
                  2.0**-25, math.nextafter(2.0**-25, math.inf),
                  0.0, math.inf, math.nan):
        for signed_value in (value, -value):
            assert encode(signed_value) == reference_encode(signed_value)
            checks += 1
    print(f'PASS: {checks} independent binary16 comparisons; all 65,536 bit patterns')


if __name__ == '__main__':
    main()
