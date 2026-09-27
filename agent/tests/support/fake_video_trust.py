#!/usr/bin/env python3
"""Stand-in for video-trust used by core-client tests.

The mode is the executable name (argv[0]), so each test can point the
client at a symlink such as vt-valid or vt-exit2.
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path


def document(overall: str, schema_version: str = "0.1") -> dict:
    integrity = {
        "VALID": "ok",
        "INVALID": "not_ok",
        "UNSIGNED": "not_applicable",
        "PARTIAL": "ok",
        "NOT_VERIFIABLE": "not_feasible",
    }[overall]
    completeness = {
        "VALID": "complete",
        "INVALID": "complete",
        "UNSIGNED": "incomplete",
        "PARTIAL": "incomplete",
        "NOT_VERIFIABLE": "not_feasible",
    }[overall]
    continuity = {
        "VALID": "intact",
        "INVALID": "broken",
        "UNSIGNED": "not_applicable",
        "PARTIAL": "intact",
        "NOT_VERIFIABLE": "not_applicable",
    }[overall]
    return {
        "schema_version": schema_version,
        "codec": "h264",
        "media_signing": {"present": overall != "UNSIGNED"},
        "signature_integrity": integrity,
        "continuity": continuity,
        "verification_completeness": completeness,
        "certificate_status": "ok",
        "source_authenticity": "not_established",
        "public_key_has_changed": False,
        "overall": overall,
        "vendor": None,
        "versions": {"signing": "r25.12.6", "validation": "r25.12.6"},
        "findings": [],
    }


def emit(payload: dict, exit_code: int) -> None:
    sys.stdout.write(json.dumps(payload))
    raise SystemExit(exit_code)


def main() -> None:
    if "--version" in sys.argv:
        print("video-trust 0.1.0")
        raise SystemExit(0)

    mode = Path(sys.argv[0]).name
    if len(sys.argv) > 1 and sys.argv[1] == "verify":
        Path(sys.argv[0] + ".argv").write_text(json.dumps(sys.argv))
        Path(sys.argv[0] + ".env").write_text("\n".join(sorted(os.environ)))

    if mode == "vt-sleep":
        time.sleep(5)
        raise SystemExit(0)
    if mode == "vt-huge-out":
        sys.stdout.buffer.write(b"A" * (2 * 1024 * 1024))
        raise SystemExit(0)
    if mode == "vt-huge-err":
        sys.stderr.buffer.write(b"STDERR_SENTINEL " + b"B" * (200 * 1024))
        emit(document("VALID"), 0)
    if mode == "vt-exit2":
        sys.stderr.write(
            "error: cannot open CA file: /home/example/secret-anchor.pem\n"
            "error: empty Annex-B input\n"
        )
        sys.stdout.write(json.dumps(document("VALID")))
        raise SystemExit(2)
    if mode == "vt-exit3":
        sys.stderr.write("internal failure STDERR_SENTINEL\n")
        raise SystemExit(3)
    if mode == "vt-bad-json":
        sys.stdout.write("not-json STDOUT_SENTINEL")
        raise SystemExit(0)
    if mode == "vt-schema":
        emit(document("VALID", schema_version="9.9"), 0)
    if mode == "vt-mismatch":
        emit(document("INVALID"), 0)
    if mode == "vt-invalid":
        emit(document("INVALID"), 1)
    if mode == "vt-unsigned":
        emit(document("UNSIGNED"), 4)
    if mode == "vt-partial":
        emit(document("PARTIAL"), 4)
    if mode == "vt-not-verifiable":
        emit(document("NOT_VERIFIABLE"), 4)
    if mode == "vt-unknown-enum":
        payload = document("VALID")
        payload["signature_integrity"] = "perfect"
        emit(payload, 0)
    if mode == "vt-source":
        payload = document("VALID")
        payload["source_authenticity"] = "established"
        emit(payload, 0)
    emit(document("VALID"), 0)


if __name__ == "__main__":
    main()
