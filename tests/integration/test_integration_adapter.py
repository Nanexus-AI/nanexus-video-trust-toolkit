#!/usr/bin/env python3
"""M6 finite integration contract, parity, and policy validation."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import tempfile

import jsonschema


ADAPTER = pathlib.Path(sys.argv[1])
CLI = pathlib.Path(sys.argv[2])
FIXTURES = pathlib.Path(sys.argv[3]).resolve()
REQUEST_SCHEMA = json.loads(pathlib.Path(sys.argv[4]).read_text(encoding="utf-8"))
RESPONSE_SCHEMA = json.loads(pathlib.Path(sys.argv[5]).read_text(encoding="utf-8"))
REQUEST_VALIDATOR = jsonschema.Draft202012Validator(REQUEST_SCHEMA)
RESPONSE_VALIDATOR = jsonschema.Draft202012Validator(RESPONSE_SCHEMA)
CA = FIXTURES / "pki" / "ca.pem"


def request(operation: str, body: dict, *, request_id: str = "test-1", roots=None):
    payload = {
        "document_type": "nanexus_video_trust_integration_request",
        "schema_version": "0.1",
        "request_id": request_id,
        "operation": operation,
        "input": body,
    }
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "NANEXUS_INTEGRATION_ALLOWED_ROOTS": os.pathsep.join(
            str(path) for path in (roots or [FIXTURES])
        ),
    }
    for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if key in os.environ:
            env[key] = os.environ[key]
    run = subprocess.run(
        [ADAPTER], input=json.dumps(payload), text=True, capture_output=True, env=env, timeout=30
    )
    assert run.stderr == "", (body, run.returncode, run.stderr)
    assert len(run.stdout.encode()) <= 2 * 1024 * 1024
    response = json.loads(run.stdout)
    RESPONSE_VALIDATOR.validate(response)
    assert str(FIXTURES) not in run.stdout
    return run.returncode, payload, response


def cli_json(arguments: list[str]):
    run = subprocess.run([CLI, *arguments], text=True, capture_output=True, timeout=30)
    assert run.stdout, (arguments, run.returncode, run.stderr)
    return json.loads(run.stdout)


def parity_single(operation: str, command: str, codec: str, path: pathlib.Path):
    code, payload, response = request(
        operation,
        {"codec": codec, "input_file": str(path), "trust_anchor": str(CA)},
        request_id=f"{operation}-{codec}-{path.stem}",
    )
    REQUEST_VALIDATOR.validate(payload)
    assert code == 0
    assert response["execution"] == {"status": "completed", "error": None}
    assert all(len(item["sha256"]) == 64 for item in response["evidence"]["inputs"])
    expected = cli_json([command, "--codec", codec, "--ca", str(CA), "--json", str(path)])
    assert response["result"]["document"] == expected


def make_mutation(codec: str, source: pathlib.Path, operation: str, output: pathlib.Path):
    args = [CLI, "tamper", "--codec", codec, "--operation", operation, "--force"]
    if operation == "truncate":
        args += ["--count", "3"]
    args += ["-o", output, source]
    subprocess.run(args, check=True, capture_output=True, text=True, timeout=30)


def finite_parity():
    work = FIXTURES / "m6-work"
    work.mkdir(exist_ok=True)
    for codec in ("h264", "h265"):
        suffix = codec
        signed = FIXTURES / codec / f"signed.{suffix}"
        unsigned = FIXTURES / codec / f"unsigned.{suffix}"
        corrupt = work / f"corrupt.{suffix}"
        truncated = work / f"truncated.{suffix}"
        make_mutation(codec, signed, "corrupt-vcl", corrupt)
        make_mutation(codec, signed, "truncate", truncated)
        for path in (signed, unsigned, corrupt, truncated):
            parity_single("verify_file", "verify", codec, path)
            parity_single("inspect_file", "inspect", codec, path)

        pairs = (
            (signed, signed),
            (signed, corrupt),
            (signed, truncated),
            (unsigned, unsigned),
        )
        for index, (before, after) in enumerate(pairs):
            body = {
                "codec": codec,
                "before_file": str(before),
                "after_file": str(after),
                "before_trust_anchor": str(CA),
                "after_trust_anchor": str(CA),
            }
            code, payload, response = request(
                "compare_preservation", body, request_id=f"compare-{codec}-{index}"
            )
            REQUEST_VALIDATOR.validate(payload)
            assert code == 0
            expected = cli_json(
                [
                    "compare-preservation", "--codec", codec,
                    "--before-ca", str(CA), "--after-ca", str(CA), "--json",
                    str(before), str(after),
                ]
            )
            assert response["result"]["document"] == expected


def failed(raw: str, expected: str, *, roots=None):
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "NANEXUS_INTEGRATION_ALLOWED_ROOTS": os.pathsep.join(
            str(path) for path in (roots or [FIXTURES])
        ),
    }
    for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if key in os.environ:
            env[key] = os.environ[key]
    run = subprocess.run([ADAPTER], input=raw, text=True, capture_output=True, env=env, timeout=30)
    assert run.returncode == 1
    assert run.stderr == ""
    response = json.loads(run.stdout)
    RESPONSE_VALIDATOR.validate(response)
    assert response["execution"]["status"] == "failed"
    assert response["execution"]["error"]["code"] == expected
    assert response["result"] is None
    assert response["evidence"] is None
    assert response["limitations"] == []
    assert str(FIXTURES) not in run.stdout


def negative_policy():
    failed("{", "malformed_request")
    failed("[]", "malformed_request")
    failed(" " * (64 * 1024 + 1), "resource_limit")

    base = {
        "document_type": "nanexus_video_trust_integration_request",
        "schema_version": "0.1",
        "request_id": "negative-1",
        "operation": "verify_file",
        "input": {"codec": "h264", "input_file": str(FIXTURES / "h264" / "signed.h264")},
    }

    value = dict(base)
    value.pop("input")
    failed(json.dumps(value), "malformed_request")
    value = dict(base, extra=True)
    failed(json.dumps(value), "malformed_request")
    value = dict(base, request_id=["wrong-type"])
    failed(json.dumps(value), "malformed_request")
    value = dict(base, schema_version="9")
    failed(json.dumps(value), "unsupported_contract_version")
    value = dict(base, operation="run_anything")
    failed(json.dumps(value), "unsupported_operation")
    value = dict(base)
    value["input"] = {"codec": "h264", "input_file": str(FIXTURES / "missing.h264")}
    failed(json.dumps(value), "input_not_found")
    value = dict(base)
    value["input"] = {"codec": "h264", "input_file": str(FIXTURES)}
    failed(json.dumps(value), "input_rejected")
    value = dict(base)
    value["input"] = {"codec": "h264", "input_file": "/etc/hosts"}
    failed(json.dumps(value), "path_not_allowed")

    with tempfile.TemporaryDirectory(dir=FIXTURES) as directory:
        link = pathlib.Path(directory) / "escape"
        link.symlink_to("/etc/hosts")
        value = dict(base)
        value["input"] = {"codec": "h264", "input_file": str(link)}
        failed(json.dumps(value), "path_not_allowed")

    value = dict(base)
    value["input"] = {"codec": "vp9", "input_file": str(FIXTURES / "h264" / "signed.h264")}
    failed(json.dumps(value), "input_rejected")
    value = dict(base)
    value["input"] = {
        "codec": "h264",
        "input_file": str(FIXTURES / "h264" / "signed.h264"),
        "trust_anchor": str(FIXTURES / "pki" / "ca.key.pem"),
    }
    failed(json.dumps(value), "internal_failure")


def environment_and_lifecycle():
    payload = {
        "document_type": "nanexus_video_trust_integration_request",
        "schema_version": "0.1",
        "request_id": "env-1",
        "operation": "verify_file",
        "input": {"codec": "h264", "input_file": str(FIXTURES / "h264" / "signed.h264")},
    }
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "SECRET_MARKER": "must-not-leak"}
    for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS"):
        if key in os.environ:
            env[key] = os.environ[key]
    run = subprocess.run([ADAPTER], input=json.dumps(payload), text=True, capture_output=True, env=env)
    response = json.loads(run.stdout)
    assert response["execution"]["error"]["code"] == "dependency_unavailable"
    assert "SECRET_MARKER" not in run.stdout and "must-not-leak" not in run.stdout

    process = subprocess.Popen([ADAPTER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        process.wait(timeout=0.1)
        raise AssertionError("adapter exited before request EOF")
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=2)
    assert process.poll() is not None


def main():
    REQUEST_VALIDATOR.check_schema(REQUEST_SCHEMA)
    RESPONSE_VALIDATOR.check_schema(RESPONSE_SCHEMA)
    finite_parity()
    negative_policy()
    environment_and_lifecycle()
    print("PASS: M6 integration adapter parity and policy")


if __name__ == "__main__":
    main()
