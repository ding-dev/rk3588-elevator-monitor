#!/usr/bin/env bash
set -euo pipefail

BOARD_USER="${BOARD_USER:-elf}"
BOARD_HOST="${BOARD_HOST:-192.168.100.11}"
SYSROOT_DIR="${SYSROOT_DIR:-/home/lsh/Elevator_Smart_System/sysroots/board_rootfs}"

mkdir -p "${SYSROOT_DIR}"

echo "[sync] syncing sysroot from ${BOARD_USER}@${BOARD_HOST} -> ${SYSROOT_DIR}"
mkdir -p "${SYSROOT_DIR}/usr" "${SYSROOT_DIR}/lib"

# Runtime loader and core shared libs.
rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/lib/" "${SYSROOT_DIR}/lib/"
if ssh "${BOARD_USER}@${BOARD_HOST}" "test -d /lib/aarch64-linux-gnu"; then
    mkdir -p "${SYSROOT_DIR}/lib/aarch64-linux-gnu"
    rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/lib/aarch64-linux-gnu/" "${SYSROOT_DIR}/lib/aarch64-linux-gnu/"
fi

# Headers needed by CMake and the compiler.
mkdir -p "${SYSROOT_DIR}/usr/include"
rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/usr/include/" "${SYSROOT_DIR}/usr/include/"

# Shared libraries and CMake/pkg-config metadata.
mkdir -p "${SYSROOT_DIR}/usr/lib"
rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/usr/lib/" "${SYSROOT_DIR}/usr/lib/" \
    --include='*/' \
    --include='*.so' \
    --include='*.so.*' \
    --include='*.a' \
    --include='pkgconfig/***' \
    --include='cmake/***' \
    --exclude='*'

if ssh "${BOARD_USER}@${BOARD_HOST}" "test -d /usr/lib/aarch64-linux-gnu"; then
    mkdir -p "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu"
    rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/usr/lib/aarch64-linux-gnu/" "${SYSROOT_DIR}/usr/lib/aarch64-linux-gnu/" \
        --include='*/' \
        --include='*.so' \
        --include='*.so.*' \
        --include='*.a' \
        --include='pkgconfig/***' \
        --include='cmake/***' \
        --exclude='*'
fi

if ssh "${BOARD_USER}@${BOARD_HOST}" "test -d /usr/share/pkgconfig"; then
    mkdir -p "${SYSROOT_DIR}/usr/share"
    rsync -avz --delete "${BOARD_USER}@${BOARD_HOST}:/usr/share/pkgconfig/" "${SYSROOT_DIR}/usr/share/pkgconfig/"
fi

echo "[sync] done"
