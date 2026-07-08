#!/usr/bin/env bash
set -euo pipefail

SYSROOT_DIR="${SYSROOT_DIR:-/home/lsh/Elevator_Smart_System/sysroots/board_rootfs}"

if [ ! -d "${SYSROOT_DIR}" ]; then
    echo "[clean] sysroot not found: ${SYSROOT_DIR}" >&2
    exit 1
fi

echo "[clean] target sysroot: ${SYSROOT_DIR}"

# These directories are not needed for this project's cross-compilation.
REMOVE_DIRS=(
    "${SYSROOT_DIR}/lib/firmware"
    "${SYSROOT_DIR}/lib/modules"
    "${SYSROOT_DIR}/usr/lib/debug"
    "${SYSROOT_DIR}/usr/lib/chromium-browser"
    "${SYSROOT_DIR}/usr/lib/firefox"
    "${SYSROOT_DIR}/usr/lib/python3"
    "${SYSROOT_DIR}/usr/lib/python3.10"
    "${SYSROOT_DIR}/usr/lib/python3.11"
    "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/dri"
    "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/vdpau"
)

for path in "${REMOVE_DIRS[@]}"; do
    if [ -e "${path}" ]; then
        echo "[clean] removing ${path}"
        rm -rf "${path}"
    fi
done

echo "[clean] done"
