#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-arm64}"
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-${ROOT_DIR}/toolchains/arm64-board.cmake}"

cmake -S "${ROOT_DIR}/backend_cpp" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"

cmake --build "${BUILD_DIR}" -j"$(nproc)"

echo "[build] output: ${BUILD_DIR}/my_test"
