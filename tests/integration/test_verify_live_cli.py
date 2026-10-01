#!/usr/bin/env python3
"""Bounded verify-live CLI policy, summary, schema, and privacy checks."""

import json
import os
import pathlib
import subprocess
import sys

import jsonschema


def run(binary, *args, env=None):
    return subprocess.run([binary, "verify-live", *args], text=True,
                          capture_output=True, env=env, timeout=15)


def main():
    binary = sys.argv[1]
    schema = json.loads(pathlib.Path(sys.argv[2]).read_text())
    validator = jsonschema.validators.validator_for(schema)(schema)

    invalid = (
        ("--duration", "1", "rtsp://example.invalid/stream"),
        ("--codec", "vp9", "--duration", "1", "rtsp://example.invalid/stream"),
        ("--codec", "h264", "rtsp://example.invalid/stream"),
        ("--codec", "h264", "--duration", "0", "rtsp://example.invalid/stream"),
        ("--codec", "h264", "--duration", "301", "rtsp://example.invalid/stream"),
        ("--codec", "h264", "--duration", "1", "https://example.invalid/stream"),
        ("--codec", "h264", "--duration", "1", "rtsp://user:pass@example.invalid/stream"),
        ("--codec", "h264", "--duration", "1", "--summary-only", "rtsp://example.invalid/stream"),
    )
    for args in invalid:
        result = run(binary, *args)
        assert result.returncode == 2, (args, result)

    env = os.environ.copy()
    env["NANEXUS_RTSP_USERNAME"] = "ONLY_USER_MARKER"
    result = run(binary, "--codec", "h264", "--duration", "1",
                 "rtsp://example.invalid/stream", env=env)
    assert result.returncode == 2
    assert "ONLY_USER_MARKER" not in result.stdout + result.stderr

    env["NANEXUS_RTSP_PASSWORD"] = "SECRET_PASSWORD_MARKER"
    result = run(binary, "--codec", "h264", "--duration", "1", "--jsonl",
                 "rtsp://127.0.0.1:1/PRIVATE_PATH?PRIVATE_QUERY", env=env)
    assert result.returncode == 3
    documents = [json.loads(line) for line in result.stdout.splitlines()]
    for document in documents:
        validator.validate(document)
    assert [d["sequence"] for d in documents] == list(range(len(documents)))
    assert [d["event_type"] for d in documents].count("session_summary") == 1
    assert documents[-1]["event_type"] == "session_summary"
    assert documents[-1]["stop_reason"] == "transport_failure"
    public = result.stdout + result.stderr
    for marker in ("SECRET_PASSWORD_MARKER", "PRIVATE_PATH", "PRIVATE_QUERY", "127.0.0.1"):
        assert marker not in public

    result = run(binary, "--codec", "h265", "--duration", "1", "--jsonl",
                 "--summary-only", "rtsp://127.0.0.1:1/stream")
    assert result.returncode == 3
    only = result.stdout.splitlines()
    assert len(only) == 1 and json.loads(only[0])["event_type"] == "session_summary"

    print("PASS: verify-live CLI policy and contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
