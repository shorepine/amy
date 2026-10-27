#!/usr/bin/env python3
"""Fail if AMY, built without AMY_DEBUG, needs any double-precision code.

Compiles each source for a Cortex-M4F, whose FPU is single precision only, and
lists what each object file needs from outside: a double operation shows up
there as a call into the compiler's software floating-point library
(__aeabi_f2d, __aeabi_dmul, ...) and a double math function as a call to it
(floor, pow, ...). This catches what -Wdouble-promotion can't: explicit casts,
double-returning functions, and floats passed to printf with a cast.

Usage: check_single_precision.py [--cc arm-none-eabi-gcc] [-DFLAG ...] src/a.c ...
"""
import os
import re
import subprocess
import sys
import tempfile

TARGET = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard", "-O2"]

# ARM EABI soft-double helpers, the libgcc names other targets use (__muldf3,
# __extendsfdf2, ...), and the double versions of the C library's math functions.
DOUBLE = re.compile(r"^(__aeabi_(d\w+|\w+2d)|__\w*(df[23i]|sfdf|dfsf|dfsi|dfdi|sidf|didf)\w*|"
                    r"(sin|cos|tan|asin|acos|atan|atan2|sinh|cosh|tanh|exp|exp2|expm1|log|log2|log10|log1p|"
                    r"pow|sqrt|cbrt|hypot|floor|ceil|round|lround|trunc|rint|lrint|fabs|fmod|fmin|fmax|"
                    r"frexp|ldexp|modf|drand48|atof|strtod))$")


def main(args):
    cc = "arm-none-eabi-gcc"
    if args[:1] == ["--cc"]:
        cc, args = args[1], args[2:]
    nm = re.sub(r"gcc$", "nm", cc)
    flags = [a for a in args if a.startswith("-")]
    sources = [a for a in args if not a.startswith("-")]
    found = {}
    with tempfile.TemporaryDirectory() as tmp:
        for src in sources:
            obj = os.path.join(tmp, os.path.basename(src) + ".o")
            subprocess.run([cc, *TARGET, *flags, "-Isrc", "-c", src, "-o", obj], check=True)
            undefined = subprocess.run([nm, "-u", obj], check=True, capture_output=True, text=True).stdout
            needs = sorted({line.split()[-1] for line in undefined.splitlines() if DOUBLE.match(line.split()[-1])})
            if needs:
                found[src] = needs
    for src, needs in found.items():
        print(f"{src}: needs double precision: {' '.join(needs)}")
    if found:
        print("AMY must stay single precision: use float literals (1.0f), float functions (floorf), "
              "and amy_snprint_float() to print a float.")
        return 1
    print(f"{len(sources)} sources, no double precision")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
