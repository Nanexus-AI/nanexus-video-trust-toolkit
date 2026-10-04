#!/usr/bin/env python3
"""Synthetic reference workflow for the generic M6 adjacent-system consumer."""

from __future__ import annotations

import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile

CONSUMER = pathlib.Path(sys.argv[1])
ADAPTER = pathlib.Path(sys.argv[2])
CLI = pathlib.Path(sys.argv[3])
FIXTURES = pathlib.Path(sys.argv[4]).resolve()
CA = FIXTURES / "pki" / "ca.pem"


def run(arguments, expected=0):
    result = subprocess.run(
        [sys.executable, CONSUMER, "--adapter", ADAPTER, "--allowed-root", FIXTURES,
         "--request-id", "synthetic-1", *arguments],
        text=True,
        capture_output=True,
        timeout=30,
        env={"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "LANG": "C.UTF-8"},
    )
    assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
    assert result.stderr == ""
    document = json.loads(result.stdout)
    assert document["document_type"] == "nanexus_video_trust_reference_summary"
    assert document["schema_version"] == "0.1"
    def keys(value):
        if isinstance(value, dict):
            for key, item in value.items():
                yield key
                yield from keys(item)
        elif isinstance(value, list):
            for item in value:
                yield from keys(item)
    assert "authentic" not in set(keys(document))
    return document


def main():
    signed = FIXTURES / "h264" / "signed.h264"
    unsigned = FIXTURES / "h264" / "unsigned.h264"
    work = FIXTURES / "adjacent-work"
    work.mkdir(exist_ok=True)
    exported = work / "exported-truncated.h264"
    subprocess.run(
        [CLI, "tamper", "--codec", "h264", "--operation", "truncate", "--count", "3",
         "--force", "-o", exported, signed],
        check=True, capture_output=True, text=True, timeout=30,
    )

    valid = run([
        "--operation", "verify_file", "--codec", "h264",
        "--input-file", signed, "--trust-anchor", CA,
    ])
    assert valid["execution_status"] == "completed"
    assert valid["domain"]["overall"] == "VALID"

    unsigned_result = run([
        "--operation", "inspect_file", "--codec", "h264", "--input-file", unsigned,
    ])
    assert unsigned_result["execution_status"] == "completed"
    assert unsigned_result["domain"]["overall"] == "UNSIGNED"

    preserved = run([
        "--operation", "compare_preservation", "--codec", "h264",
        "--before-file", signed, "--after-file", signed,
        "--before-trust-anchor", CA, "--after-trust-anchor", CA,
    ])
    assert preserved["execution_status"] == "completed"
    assert preserved["domain"]["preservation"] == "preserved"
    assert preserved["domain"]["source_coverage"] == "full"

    exported_result = run([
        "--operation", "compare_preservation", "--codec", "h264",
        "--before-file", signed, "--after-file", exported,
        "--before-trust-anchor", CA, "--after-trust-anchor", CA,
    ])
    assert exported_result["execution_status"] == "completed"
    assert exported_result["domain"]["preservation"] in {
        "preserved", "partially_preserved", "not_preserved", "indeterminate"
    }
    assert exported_result["domain"]["source_coverage"] in {"subset", "unknown"}

    rejected = run([
        "--operation", "verify_file", "--codec", "h264", "--input-file", "/etc/hosts",
    ], expected=1)
    assert rejected["execution_status"] == "failed"
    assert rejected["error"]["code"] == "path_not_allowed"
    assert rejected["domain"] is None

    with tempfile.TemporaryDirectory() as directory:
        fake = pathlib.Path(directory) / "fake-adapter"
        fake.write_text("#!/usr/bin/env python3\nprint('not-json')\n", encoding="utf-8")
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        malformed = subprocess.run(
            [sys.executable, CONSUMER, "--adapter", fake, "--allowed-root", FIXTURES,
             "--operation", "verify_file", "--codec", "h264", "--input-file", signed],
            text=True, capture_output=True, timeout=30,
        )
        assert malformed.returncode == 2
        assert json.loads(malformed.stdout)["error"]["code"] == "adapter_protocol_error"

        fake.write_text(
            "#!/usr/bin/env python3\nimport time\ntime.sleep(5)\n", encoding="utf-8"
        )
        timed = subprocess.run(
            [sys.executable, CONSUMER, "--adapter", fake, "--allowed-root", FIXTURES,
             "--timeout", "0.1", "--operation", "verify_file", "--codec", "h264",
             "--input-file", signed],
            text=True, capture_output=True, timeout=5,
        )
        assert timed.returncode == 2
        assert json.loads(timed.stdout)["error"]["code"] == "adapter_timeout"

    print("PASS: adjacent-system reference consumer")


if __name__ == "__main__":
    main()
