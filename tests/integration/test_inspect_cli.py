#!/usr/bin/env python3
"""Inspect CLI text/JSON integration and schema contract checks."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile

import jsonschema


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, check=False)


def create_variant(
    executable: pathlib.Path,
    source: pathlib.Path,
    output: pathlib.Path,
    codec: str,
    operation: str,
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
        "-o",
        str(output),
    ]
    if count is not None:
        command += ["--count", str(count)]
    result = run([*command, str(source)])
    assert result.returncode == 0, result.stderr
    assert result.stdout == ""
    assert result.stderr == ""


def inspect_and_compare(
    executable: pathlib.Path,
    schema: dict,
    media: pathlib.Path,
    codec: str,
    expected_overall: str,
    expected_exit: int,
    ca: pathlib.Path | None = None,
) -> tuple[dict, str]:
    common = ["--codec", codec]
    if ca is not None:
        common += ["--ca", str(ca)]

    verified = run([str(executable), "verify", *common, "--json", str(media)])
    inspected = run([str(executable), "inspect", *common, "--json", str(media)])
    inspected_text = run([str(executable), "inspect", *common, str(media)])
    verified_text = run([str(executable), "verify", *common, str(media)])

    assert inspected.returncode == verified.returncode, (
        media,
        inspected.returncode,
        verified.returncode,
        inspected.stderr,
    )
    assert inspected.stderr == "", (media, inspected.stderr)
    assert inspected.stdout.startswith("{") and inspected.stdout.endswith("}\n")
    assert inspected.stdout.count("\n") == 1

    document = json.loads(inspected.stdout)
    verification = json.loads(verified.stdout)
    jsonschema.validators.validator_for(schema)(schema).validate(document)
    assert document["document_type"] == "media_signing_inspection"
    assert document["schema_version"] == "0.1"
    assert document["verification"] == verification
    assert verification["overall"] == expected_overall
    assert inspected.returncode == expected_exit
    assert set(document["accumulated_validation"]) == {
        "number_of_received_nalus",
        "number_of_validated_nalus",
        "number_of_pending_nalus",
        "number_of_received_frames",
        "number_of_validated_frames",
        "number_of_pending_frames",
        "first_timestamp",
        "last_timestamp",
    }
    assert all(
        isinstance(document["accumulated_validation"][field], int)
        for field in ("first_timestamp", "last_timestamp")
    )
    assert all(
        value is None or isinstance(value, int)
        for key, value in document["latest_validation"].items()
        if "timestamp" not in key
    )
    assert all(value is None for value in document["vendor"].values())
    assert "validation_str" not in inspected.stdout
    assert "nalu_str" not in inspected.stdout
    assert "authenticity_and_provenance" not in inspected.stdout
    assert document["verification"]["source_authenticity"] == "not_established"
    assert inspected_text.returncode == inspected.returncode == verified_text.returncode
    assert inspected_text.stderr == "", (media, inspected_text.stderr)
    assert inspected_text.stdout.startswith("Nanexus Video Trust Inspection\n")
    assert not inspected_text.stdout.startswith("{")
    assert f"  Overall: {document['verification']['overall']}\n" in inspected_text.stdout
    assert f"  {document['verification']['overall']}\n" in verified_text.stdout
    assert str(media) not in inspected_text.stdout
    assert "validation_str" not in inspected_text.stdout
    assert "nalu_str" not in inspected_text.stdout
    assert "AUTHENTIC=true" not in inspected_text.stdout
    assert "Source authenticity: not_established" in inspected_text.stdout
    assert "Certificate status:" in inspected_text.stdout
    assert "Vendor Observations\n" in inspected_text.stdout
    assert "Vendor observations do not establish device identity or trust." in inspected_text.stdout
    repeated_text = run([str(executable), "inspect", *common, str(media)])
    assert repeated_text.returncode == inspected_text.returncode
    assert repeated_text.stdout == inspected_text.stdout
    assert repeated_text.stderr == ""
    return document, inspected_text.stdout


def main() -> int:
    executable = pathlib.Path(sys.argv[1])
    fixture_dir = pathlib.Path(sys.argv[2])
    schema_path = pathlib.Path(sys.argv[3])
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    jsonschema.validators.validator_for(schema).check_schema(schema)
    ca = fixture_dir / "pki/ca.pem"

    help_result = run([str(executable), "--help"])
    assert help_result.returncode == 0
    assert "video-trust inspect" in help_result.stdout

    report_result = run([str(executable), "report"])
    assert report_result.returncode == 2
    assert report_result.stdout == ""
    assert "unknown subcommand" in report_result.stderr

    signed_h264, signed_h264_text = inspect_and_compare(
        executable, schema, fixture_dir / "h264/signed.h264", "h264", "VALID", 0, ca
    )
    assert signed_h264["verification"]["overall"] == "VALID"
    assert signed_h264["verification"]["certificate_status"] == "ok"
    assert "Certificate status: ok" in signed_h264_text

    signed_h265, _ = inspect_and_compare(
        executable, schema, fixture_dir / "h265/signed.h265", "h265", "VALID", 0, ca
    )
    assert signed_h265["verification"]["overall"] == "VALID"

    unsigned, unsigned_text = inspect_and_compare(
        executable, schema, fixture_dir / "h264/unsigned.h264", "h264", "UNSIGNED", 4
    )
    assert unsigned["verification"]["overall"] == "UNSIGNED"
    assert all(value is None for value in unsigned["vendor"].values())
    assert "Manufacturer: unavailable" in unsigned_text
    assert "Firmware version: unavailable" in unsigned_text
    assert "Serial number: unavailable" in unsigned_text
    assert (
        "First timestamp (FILETIME 100 ns ticks): "
        f"{unsigned['accumulated_validation']['first_timestamp']}"
        in unsigned_text
    )
    assert "Pending hashable NALUs: 0" in unsigned_text

    unsigned_h265, unsigned_h265_text = inspect_and_compare(
        executable, schema, fixture_dir / "h265/unsigned.h265", "h265", "UNSIGNED", 4
    )
    assert unsigned_h265["latest_validation"]["number_of_expected_hashable_nalus"] is None
    assert unsigned_h265["latest_validation"]["number_of_received_hashable_nalus"] is None
    assert unsigned_h265["accumulated_validation"]["first_timestamp"] == -1
    assert "Signing: (empty)" in unsigned_h265_text

    tampered, _ = inspect_and_compare(
        executable, schema, fixture_dir / "h264/tampered.h264", "h264", "INVALID", 1, ca
    )
    assert tampered["verification"]["overall"] == "INVALID"

    with tempfile.TemporaryDirectory(prefix="nanexus-inspect-") as temp_dir:
        variant_dir = pathlib.Path(temp_dir)
        for codec in ("h264", "h265"):
            signed = fixture_dir / codec / f"signed.{codec}"

            corrupted = variant_dir / f"corrupted.{codec}"
            create_variant(executable, signed, corrupted, codec, "corrupt-vcl")
            inspect_and_compare(executable, schema, corrupted, codec, "INVALID", 1, ca)

            stripped = variant_dir / f"stripped.{codec}"
            create_variant(executable, signed, stripped, codec, "strip-signing-sei")
            stripped_document, stripped_text = inspect_and_compare(
                executable, schema, stripped, codec, "UNSIGNED", 4, ca
            )
            assert stripped_document["accumulated_validation"]["first_timestamp"] == -1
            assert "Expected hashable NALUs: unavailable" in stripped_text

            for count in (1, 8):
                truncated = variant_dir / f"truncated-{count}.{codec}"
                create_variant(executable, signed, truncated, codec, "truncate", count)
                truncated_document, truncated_text = inspect_and_compare(
                    executable, schema, truncated, codec, "PARTIAL", 4, ca
                )
                accumulated = truncated_document["accumulated_validation"]
                latest = truncated_document["latest_validation"]
                assert accumulated["number_of_pending_nalus"] > 0
                assert accumulated["number_of_pending_frames"] > 0
                assert latest["number_of_pending_hashable_nalus"] == 1
                assert "Pending hashable NALUs: 1" in truncated_text

    assert "inspect currently requires --json" not in signed_h264_text

    print("PASS: inspect CLI text and JSON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
