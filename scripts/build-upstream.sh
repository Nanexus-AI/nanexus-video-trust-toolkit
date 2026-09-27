#!/usr/bin/env bash
# Build and install the pinned ONVIF media-signing-framework into a local prefix.
# Prerequisite: ./scripts/fetch-upstream.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${ROOT}/third_party/media-signing-framework"
PREFIX="${OMS_PREFIX:-${ROOT}/.oms-prefix}"
BUILD_DIR="${OMS_BUILD_DIR:-${ROOT}/build/oms}"

if [[ ! -f "${SRC}/meson.build" ]]; then
  echo "FAIL: upstream not found at ${SRC}" >&2
  echo "Run: ./scripts/fetch-upstream.sh" >&2
  exit 1
fi

mkdir -p "${BUILD_DIR}"
meson setup "${BUILD_DIR}" "${SRC}" \
  --prefix="${PREFIX}" \
  -Dsigningplugin=unthreaded \
  -Dwerror=false \
  --reconfigure 2>/dev/null || \
meson setup "${BUILD_DIR}" "${SRC}" \
  --prefix="${PREFIX}" \
  -Dsigningplugin=unthreaded \
  -Dwerror=false

meson compile -C "${BUILD_DIR}"
meson install -C "${BUILD_DIR}"

echo "PASS: media-signing-framework installed to ${PREFIX}"
echo "Use: meson setup build -Doms_prefix=${PREFIX}"
