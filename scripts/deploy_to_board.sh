#!/usr/bin/env bash
set -euo pipefail

BOARD_USER="${BOARD_USER:-elf}"
BOARD_HOST="${BOARD_HOST:-192.168.100.11}"
REMOTE_DIR="${REMOTE_DIR:-/home/elf/elevator_app}"
REMOTE_BIN="${REMOTE_BIN:-${REMOTE_DIR}/my_test}"
REMOTE_LOG="${REMOTE_LOG:-${REMOTE_DIR}/my_test.log}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-arm64}"

ssh "${BOARD_USER}@${BOARD_HOST}" "mkdir -p '${REMOTE_DIR}'"

scp "${BUILD_DIR}/my_test" "${BOARD_USER}@${BOARD_HOST}:${REMOTE_DIR}/"
#scp "${ROOT_DIR}/frontend_py/mqtt_client.py" "${BOARD_USER}@${BOARD_HOST}:${REMOTE_DIR}/"
#scp "${ROOT_DIR}/frontend_py/云端.html" "${BOARD_USER}@${BOARD_HOST}:${REMOTE_DIR}/"

