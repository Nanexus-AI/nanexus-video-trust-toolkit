#!/usr/bin/env bash
set -euo pipefail

: "${VIDEO_TRUST:?set VIDEO_TRUST to the live-enabled video-trust binary}"
: "${LIVE_MATRIX_FIXTURES:?set LIVE_MATRIX_FIXTURES to the generated private fixture directory}"
: "${LIVE_MATRIX_CA:?set LIVE_MATRIX_CA to the synthetic trust anchor}"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT=${LIVE_MATRIX_OUTPUT:-"${ROOT}/build/live-matrix-results"}
IMAGE=${MEDIAMTX_IMAGE:-bluenviron/mediamtx:1.15.3}
NAME=${MEDIAMTX_NAME:-nanexus-live-matrix}
PORT=${MEDIAMTX_RTSP_PORT:-18554}

cleanup() {
  docker stop "${NAME}" >/dev/null 2>&1 || true
  docker rm "${NAME}" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

cleanup
docker run -d --name "${NAME}" \
  -p "127.0.0.1:${PORT}:8554" \
  -p 127.0.0.1:19000:19000/udp \
  -v "${ROOT}/tests/fixtures/mediamtx-live-matrix.yml:/mediamtx.yml:ro" \
  "${IMAGE}" >/dev/null

python3 "${ROOT}/tests/tools/run_live_matrix.py" \
  --manifest "${ROOT}/tests/fixtures/live-matrix.json" \
  --video-trust "${VIDEO_TRUST}" \
  --schema "${ROOT}/docs/schemas/media-signing-live-session-event-0.1.json" \
  --fixtures "${LIVE_MATRIX_FIXTURES}" \
  --ca "${LIVE_MATRIX_CA}" \
  --endpoint-base "rtsp://127.0.0.1:${PORT}" \
  --output "${OUTPUT}" \
  "$@"
