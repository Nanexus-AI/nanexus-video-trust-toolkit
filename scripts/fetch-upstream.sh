#!/usr/bin/env bash
# Fetch the pinned ONVIF media-signing-framework revision into third_party/.
# The checkout is local working material by default and should not be committed
# unless a later, license-reviewed decision explicitly vendors it.
#
# Pin: tag r25.12.6
# media-signing-framework project/release version at pin: 25.12.6
# (This is NOT the Meson build-system tool version.)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/media-signing-framework"
REPO_URL="${MEDIA_SIGNING_REPO_URL:-https://github.com/onvif/media-signing-framework.git}"
PIN_TAG="${MEDIA_SIGNING_PIN_TAG:-r25.12.6}"
PIN_COMMIT="${MEDIA_SIGNING_PIN_COMMIT:-cf7785ab993c18d921094e8e505c2c34a1350f28}"

mkdir -p "${ROOT}/third_party"

if [[ -d "${DEST}/.git" ]]; then
  git -C "${DEST}" fetch --tags origin
else
  git clone "${REPO_URL}" "${DEST}"
fi

git -C "${DEST}" checkout --detach "${PIN_COMMIT}"

actual="$(git -C "${DEST}" rev-parse HEAD)"
if [[ "${actual}" != "${PIN_COMMIT}" ]]; then
  echo "FAIL: expected ${PIN_COMMIT}, got ${actual}" >&2
  exit 1
fi

echo "PASS: media-signing-framework at ${PIN_TAG} (${PIN_COMMIT})"
echo "Path: ${DEST}"
echo "Note: leave third_party/media-signing-framework untracked unless vendoring is approved."
