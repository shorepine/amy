# Windows portability of the PCM Hann-window constant

## Symptom

The native Godot addon build on Windows failed while compiling `src/pcm.c`
with MSVC:

```text
error C2065: 'M_PI': undeclared identifier
```

Linux and macOS builds of the same source succeeded.

## Cause

ISO C does not require `<math.h>` to define `M_PI`. Many Unix toolchains
expose it as an extension, whereas MSVC exposes it only under additional
preprocessor conditions.

AMY commit `73b6fece` added a Hann-window calculation for PCM time stretching:

```c
cosf(2.0f * (float)M_PI * (float)i / (float)PCM_STRETCH_GRAIN)
```

Older Windows builds succeeded because their source predated that sampler
change. In particular, the last successful upstream three-platform Godot run,
[32322968524](https://github.com/shorepine/amy/actions/runs/32322968524),
used head `fa14fa2e`, which did not contain commit `73b6fece`. Current-main run
[33349266667](https://github.com/shorepine/amy/actions/runs/33349266667)
used head `0fb0a00b`: its Linux and macOS jobs passed, but its Windows job
failed at the new `M_PI` expression.

The failure is therefore independent of the reusable-sequence implementation.
It became visible while that work was being validated on all three Godot
desktop targets.

## Portable correction

`src/pcm.c` now supplies the conventional constant only when the toolchain has
not already supplied it:

```c
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
```

The guard has no effect on platforms that already define `M_PI`. On MSVC it
provides the missing compile-time value; the existing calculation explicitly
casts it to `float`. The Hann window is initialized once by `pcm_init()`, so
the correction adds no render-path work and does not change the algorithm.

Defining `_USE_MATH_DEFINES` instead would make the common source depend on
MSVC-specific include ordering. Computing pi through a trigonometric function
would add unnecessary runtime work. The guarded constant is the smallest
portable correction for the existing expression.

The correction is isolated in commit `397488b3` so it remains reviewable and
revertible independently of sequencer behavior.

## Validation

With the guarded fallback, fork run
[33954151514](https://github.com/linuxificator/amy/actions/runs/33954151514)
successfully built both Windows Godot debug and release libraries and uploaded
the resulting artifact. Linux and macOS Godot debug and release builds had
already passed from the same AMY source before the fallback was applied; their
preprocessors already supplied `M_PI`, so the guarded definition is inactive
there.

This is a build-portability correction. It does not alter the reusable-
sequence API, wire format, timing, publication model, or compatibility rules.

