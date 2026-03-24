#!/usr/bin/env bash
#
# setup_godot.sh — Build and install the AMY GDExtension addon into a Godot project.
#
# Usage:
#   ./setup_godot.sh /path/to/your/godot/project
#
# Environment variables:
#   GODOT_CPP_PATH  Path to godot-cpp checkout (default: ../godot-cpp)
#   JOBS            Parallel build jobs (default: 8)
#
# This script:
#   1. Builds the native GDExtension library using scons
#   2. Creates addons/amy/ in your Godot project with:
#      - The GDScript Amy class (amy.gd)
#      - The GDExtension config and compiled library
#      - Web export support files
#      - The install.gd editor script for web setup

set -euo pipefail

AMY_DIR="$(cd "$(dirname "$0")" && pwd)"
GODOT_DIR="${AMY_DIR}/godot"
GODOT_CPP_PATH="${GODOT_CPP_PATH:-${AMY_DIR}/../godot-cpp}"
JOBS="${JOBS:-8}"

# ── Check arguments ──────────────────────────────────────────────
if [ $# -lt 1 ]; then
    echo "Usage: $0 <godot-project-path>"
    echo ""
    echo "  Builds the AMY GDExtension and installs it into a Godot project."
    echo ""
    echo "  Environment variables:"
    echo "    GODOT_CPP_PATH  Path to godot-cpp (default: ../godot-cpp)"
    echo "    JOBS            Parallel jobs (default: 8)"
    exit 1
fi

PROJECT_DIR="$(cd "$1" && pwd)"
ADDON_DIR="${PROJECT_DIR}/addons/amy"

if [ ! -f "${PROJECT_DIR}/project.godot" ]; then
    echo "ERROR: ${PROJECT_DIR} does not look like a Godot project (no project.godot found)"
    exit 1
fi

if [ ! -d "${GODOT_CPP_PATH}" ]; then
    echo "ERROR: godot-cpp not found at ${GODOT_CPP_PATH}"
    echo "  Clone it:  git clone --branch godot-4.4-stable https://github.com/godotengine/godot-cpp.git ${GODOT_CPP_PATH}"
    echo "  Or set:    GODOT_CPP_PATH=/path/to/godot-cpp $0 $1"
    exit 1
fi

echo "=== AMY Godot Addon Setup ==="
echo "  AMY source:    ${AMY_DIR}/src"
echo "  godot-cpp:     ${GODOT_CPP_PATH}"
echo "  Godot project: ${PROJECT_DIR}"
echo "  Addon output:  ${ADDON_DIR}"
echo ""

# ── Create addon directory structure ─────────────────────────────
echo "Creating addon directory structure..."
mkdir -p "${ADDON_DIR}/src"
mkdir -p "${ADDON_DIR}/bin"
mkdir -p "${ADDON_DIR}/web"

# ── Copy AMY C source (symlink-free, for scons) ─────────────────
echo "Copying AMY source..."
mkdir -p "${ADDON_DIR}/amy_src"
cp "${AMY_DIR}"/src/*.c "${ADDON_DIR}/amy_src/" 2>/dev/null || true
cp "${AMY_DIR}"/src/*.h "${ADDON_DIR}/amy_src/" 2>/dev/null || true
# Godot shouldn't try to import C files
touch "${ADDON_DIR}/amy_src/.gdignore"

# ── Copy GDExtension wrapper source ─────────────────────────────
echo "Copying GDExtension source..."
cp "${GODOT_DIR}/src/"* "${ADDON_DIR}/src/"

# ── Copy SConstruct ──────────────────────────────────────────────
echo "Copying SConstruct..."
cp "${GODOT_DIR}/SConstruct" "${ADDON_DIR}/"

# ── Copy GDExtension config ─────────────────────────────────────
echo "Copying GDExtension config..."
cp "${GODOT_DIR}/amy.gdextension" "${ADDON_DIR}/"

# ── Build native library ────────────────────────────────────────
echo ""
echo "Building native GDExtension library..."
cd "${ADDON_DIR}"
GODOT_CPP_PATH="${GODOT_CPP_PATH}" scons -j"${JOBS}" 2>&1 | tail -5
BUILD_STATUS=${PIPESTATUS[0]}

if [ ${BUILD_STATUS} -ne 0 ]; then
    echo ""
    echo "ERROR: Build failed. Check the output above."
    exit 1
fi

echo ""
echo "Build successful!"

# ── Copy GDScript wrapper ────────────────────────────────────────
echo "Copying GDScript files..."
cp "${GODOT_DIR}/amy.gd" "${ADDON_DIR}/"

# ── Copy web export files ────────────────────────────────────────
echo "Copying web export files..."
WEB_SRC="${AMY_DIR}/docs"
cp "${WEB_SRC}/amy.js"    "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.wasm"  "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.aw.js" "${ADDON_DIR}/web/"
cp "${WEB_SRC}/amy.ww.js" "${ADDON_DIR}/web/"
cp "${WEB_SRC}/enable-threads.js" "${ADDON_DIR}/web/"

# A copy for project root (service worker needs root scope)
cp "${WEB_SRC}/enable-threads.js" "${ADDON_DIR}/web/enable-threads-root.js"

# Godot<->AMY bridge and custom HTML shell
cp "${GODOT_DIR}/web/godot_amy_bridge.js" "${ADDON_DIR}/web/"
cp "${GODOT_DIR}/web/custom_shell.html" "${ADDON_DIR}/web/"

# ── Copy install.gd editor script ───────────────────────────────
echo "Copying install.gd..."
cp "${GODOT_DIR}/install.gd" "${ADDON_DIR}/"

# ── Clean up build artifacts from addon ──────────────────────────
echo "Cleaning build artifacts..."
find "${ADDON_DIR}/amy_src" -name "*.os" -delete 2>/dev/null || true
find "${ADDON_DIR}/src" -name "*.os" -delete 2>/dev/null || true

# ── Done ─────────────────────────────────────────────────────────
echo ""
echo "=== Done! ==="
echo ""
echo "The AMY addon has been installed to: ${ADDON_DIR}"
echo ""
echo "Next steps:"
echo "  1. Open the project in the Godot editor"
echo "  2. Use Amy in your scripts:"
echo "       var amy := Amy.new()"
echo "       add_child(amy)"
echo "       await get_tree().process_frame"
echo "       amy.send({\"osc\": 0, \"wave\": Amy.SINE, \"freq\": 440, \"vel\": 1.0})"
echo ""
echo "  For web export setup, see: https://github.com/shorepine/amy/blob/main/docs/godot.md"
