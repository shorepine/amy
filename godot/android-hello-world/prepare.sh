#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
GRADLE_BIN="${GRADLE_BIN:-gradle}"

# Android intentionally packages the independent AMY service AAR. The Godot
# application itself does not build or load the AmySynth GDExtension.
(
  cd "${ROOT}/android"
  "${GRADLE_BIN}" :amy-service:assembleDebug :amy-service:assembleRelease
)

ADDON="${HERE}/addons/amy_android"
mkdir -p "${ADDON}"
cp "${ROOT}/android/amy-service/build/outputs/aar/amy-service-debug.aar" \
   "${ADDON}/amy-service-debug.aar"
cp "${ROOT}/android/amy-service/build/outputs/aar/amy-service-release.aar" \
   "${ADDON}/amy-service-release.aar"

# Exercise the exact shared high-level API used by normal Godot projects.
cp "${ROOT}/godot/amy.gd" "${HERE}/amy.gd"

printf 'Prepared AMY Android service AARs and shared amy.gd in %s\n' "${HERE}"
