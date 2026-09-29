#!/usr/bin/env python3
"""Preservation comparison CLI integration for both Annex-B codecs."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile

import jsonschema


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, check=False)


def tamper(
    executable: pathlib.Path,
    codec: str,
    operation: str,
    source: pathlib.Path,
    output: pathlib.Path,
    count: int | None = None,
) -> None:
    command = [
        str(executable),
        "tamper",
        "--codec",
        codec,
        "--operation",
        operation,
        "--quiet",
        "--force",
        "-o",
        str(output),
    ]
    if count is not None:
        command += ["--count", str(count)]
    result = run([*command, str(source)])
    assert result.returncode == 0, result.stderr
    assert result.stdout == ""
    assert result.stderr == ""


def rewrite_start_codes(source: pathlib.Path, output: pathlib.Path) -> None:
    data = source.read_bytes()
    starts: list[tuple[int, int]] = []
    index = 0
    while index + 3 <= len(data):
        if data[index : index + 4] == b"\x00\x00\x00\x01":
            starts.append((index, 4))
            index += 4
        elif data[index : index + 3] == b"\x00\x00\x01":
            starts.append((index, 3))
            index += 3
        else:
            index += 1
    assert starts and starts[0][0] == 0
    rewritten = bytearray()
    for item, (offset, length) in enumerate(starts):
        end = starts[item + 1][0] if item + 1 < len(starts) else len(data)
        rewritten.extend(b"\x00\x00\x01")
        rewritten.extend(data[offset + length : end])
    output.write_bytes(rewritten)


def compare(
    executable: pathlib.Path,
    schema: dict,
    codec: str,
    before: pathlib.Path,
    after: pathlib.Path,
    ca: pathlib.Path,
    expected_exit: int,
    expected_coverage: str,
    expected_applicability: str,
    expected_preservation: str,
    extra: list[str] | None = None,
) -> dict:
    common = [
        str(executable),
        "compare-preservation",
        "--codec",
        codec,
        "--before-ca",
        str(ca),
        "--after-ca",
        str(ca),
        *(extra or []),
        str(before),
        str(after),
    ]
    machine = run([*common[:2], "--json", *common[2:]])
    human = run(common)
    repeated = run(common)

    assert machine.returncode == human.returncode == repeated.returncode == expected_exit
    assert machine.stderr == human.stderr == repeated.stderr == ""
    assert machine.stdout.startswith("{") and machine.stdout.endswith("}\n")
    assert machine.stdout.count("\n") == 1
    document = json.loads(machine.stdout)
    jsonschema.validators.validator_for(schema)(schema).validate(document)
    assert document["coverage"]["state"] == expected_coverage
    assert (
        document["applicability"]["media_signing_preservation"]
        == expected_applicability
    )
    assert (
        document["preservation"]["media_signing_evidence"]
        == expected_preservation
    )

    assert human.stdout == repeated.stdout
    assert human.stdout.startswith("Nanexus Media Signing Preservation Assessment\n")
    assert "Artifact and Stream Relationship\n" in human.stdout
    assert f"  Source coverage: {expected_coverage}\n" in human.stdout
    assert f"  Applicability: {expected_applicability}\n" in human.stdout
    assert f"  Preservation: {expected_preservation}\n" in human.stdout
    assert "Transformation Context (caller-declared, untrusted)\n" in human.stdout
    assert "Findings\n" in human.stdout and "Limitations\n" in human.stdout
    assert str(before) not in human.stdout and str(after) not in human.stdout
    assert "validation_str" not in human.stdout
    assert "nalu_str" not in human.stdout
    assert "Overall: AUTHENTIC" not in human.stdout
    return document


def main() -> int:
    executable = pathlib.Path(sys.argv[1])
    fixture_root = pathlib.Path(sys.argv[2])
    schema = json.loads(pathlib.Path(sys.argv[3]).read_text(encoding="utf-8"))
    jsonschema.validators.validator_for(schema).check_schema(schema)
    ca = fixture_root / "pki/ca.pem"

    help_result = run([str(executable), "--help"])
    assert help_result.returncode == 0
    assert "compare-preservation" in help_result.stdout
    assert "compare exit:" in help_result.stdout

    with tempfile.TemporaryDirectory(prefix="nanexus-compare-") as temp:
        work = pathlib.Path(temp)
        for codec in ("h264", "h265"):
            signed = fixture_root / codec / f"signed.{codec}"

            exact = compare(
                executable,
                schema,
                codec,
                signed,
                signed,
                ca,
                0,
                "full",
                "applicable",
                "preserved",
                ["--transformation", "transparent", "--pipeline-id", "fixture-copy"],
            )
            assert exact["artifact_identity"]["byte_relation"] == "identical"
            assert exact["transformation"]["kind"] == "transparent"
            assert exact["transformation"]["trust"] == "caller_declared_untrusted"

            framing = work / f"framing.{codec}"
            rewrite_start_codes(signed, framing)
            framing_document = compare(
                executable,
                schema,
                codec,
                signed,
                framing,
                ca,
                0,
                "full",
                "applicable",
                "preserved",
                ["--transformation", "remux"],
            )
            assert framing_document["artifact_identity"]["byte_relation"] == "different"
            assert framing_document["correlation"]["stream_relation"] == "equivalent"

            truncated = work / f"truncated.{codec}"
            tamper(executable, codec, "truncate", signed, truncated, 3)
            compare(
                executable,
                schema,
                codec,
                signed,
                truncated,
                ca,
                1,
                "subset",
                "applicable",
                "partially_preserved",
                ["--transformation", "clip"],
            )

            stripped = work / f"stripped.{codec}"
            tamper(executable, codec, "strip-signing-sei", signed, stripped)
            compare(
                executable,
                schema,
                codec,
                signed,
                stripped,
                ca,
                1,
                "subset",
                "applicable",
                "not_preserved",
                ["--transformation", "metadata-change"],
            )

            corrupted = work / f"corrupted.{codec}"
            tamper(executable, codec, "corrupt-vcl", signed, corrupted)
            compare(
                executable,
                schema,
                codec,
                signed,
                corrupted,
                ca,
                1,
                "unknown",
                "applicable",
                "not_preserved",
            )

            # Existing M3-B semantics preserve the observed evidence of an
            # exact invalid or partial source without upgrading its validity.
            compare(
                executable,
                schema,
                codec,
                corrupted,
                corrupted,
                ca,
                0,
                "full",
                "applicable",
                "preserved",
            )
            compare(
                executable,
                schema,
                codec,
                truncated,
                truncated,
                ca,
                0,
                "full",
                "applicable",
                "preserved",
            )

            unsigned = fixture_root / codec / f"unsigned.{codec}"
            unsigned_document = compare(
                executable,
                schema,
                codec,
                unsigned,
                unsigned,
                ca,
                4,
                "full",
                "not_applicable",
                "indeterminate",
            )
            assert (
                unsigned_document["correlation"]["signing_metadata"]["relation"]
                == "not_applicable"
            )

        malformed_media = work / "malformed.h264"
        malformed_media.write_bytes(b"not-annex-b")
        malformed = run(
            [
                str(executable),
                "compare-preservation",
                "--codec",
                "h264",
                str(fixture_root / "h264/signed.h264"),
                str(malformed_media),
            ]
        )
        assert malformed.returncode == 2
        assert malformed.stdout == ""
        assert malformed.stderr.startswith("error:")

        bad_ca = work / "invalid-ca.pem"
        bad_ca.write_text("not a PEM certificate\n", encoding="utf-8")
        runtime_failure = run(
            [
                str(executable),
                "compare-preservation",
                "--codec",
                "h264",
                "--before-ca",
                str(bad_ca),
                str(fixture_root / "h264/signed.h264"),
                str(fixture_root / "h264/signed.h264"),
            ]
        )
        assert runtime_failure.returncode == 3
        assert runtime_failure.stdout == ""
        assert runtime_failure.stderr.startswith("error:")

    missing = run(
        [
            str(executable),
            "compare-preservation",
            "--codec",
            "h264",
            "/does/not/exist-before.h264",
            "/does/not/exist-after.h264",
        ]
    )
    assert missing.returncode == 2
    assert missing.stdout == ""
    assert missing.stderr.startswith("error:")

    bad_transformation = run(
        [
            str(executable),
            "compare-preservation",
            "--codec",
            "h264",
            "--transformation",
            "trusted-vms",
            "before.h264",
            "after.h264",
        ]
    )
    assert bad_transformation.returncode == 2
    assert bad_transformation.stdout == ""
    assert "unsupported transformation" in bad_transformation.stderr

    print("PASS: compare-preservation CLI (H.264 + H.265)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
