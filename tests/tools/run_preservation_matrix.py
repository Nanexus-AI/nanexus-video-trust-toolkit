#!/usr/bin/env python3
"""Run fixed synthetic preservation recipes and emit a path-free matrix."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
from typing import Any

import jsonschema


SIGNING_UUID = bytes.fromhex("005bc93f2d715e95ada4796f90877a6f")
NON_SIGNING_UUID = bytes.fromhex("a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(argv: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(argv, capture_output=True, text=True, check=False)


def must_run(argv: list[str]) -> None:
    completed = run(argv)
    if completed.returncode != 0:
        raise AssertionError(
            f"fixed recipe failed ({completed.returncode}): "
            f"{pathlib.Path(argv[0]).name}: {completed.stderr.strip()}"
        )


def split_nalus(data: bytes) -> list[bytes]:
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
    if not starts or starts[0][0] != 0:
        raise AssertionError("fixture is not Annex-B")
    return [
        data[offset + length : starts[item + 1][0] if item + 1 < len(starts) else len(data)]
        for item, (offset, length) in enumerate(starts)
    ]


def write_nalus(path: pathlib.Path, nalus: list[bytes], start_code_size: int = 4) -> None:
    start = b"\x00\x00\x00\x01" if start_code_size == 4 else b"\x00\x00\x01"
    path.write_bytes(b"".join(start + nal for nal in nalus))


def signing_indices(nalus: list[bytes]) -> list[int]:
    return [index for index, nal in enumerate(nalus) if SIGNING_UUID in nal]


def second_gop_start(codec: str, nalus: list[bytes]) -> int:
    parameter_type = 7 if codec == "h264" else 32
    indexes = [
        index
        for index, nal in enumerate(nalus)
        if ((nal[0] & 0x1F) if codec == "h264" else ((nal[0] >> 1) & 0x3F))
        == parameter_type
    ]
    if len(indexes) < 2:
        raise AssertionError("fixture has no second GOP parameter-set boundary")
    return indexes[1]


def non_signing_sei(codec: str) -> bytes:
    header = b"\x06" if codec == "h264" else b"\x4e\x01"
    return header + b"\x05\x10" + NON_SIGNING_UUID + b"\x80"


def normalized_command(argv: list[str], replacements: dict[str, str]) -> list[str]:
    return [replacements.get(item, pathlib.Path(item).name if item == argv[0] else item) for item in argv]


class Harness:
    def __init__(self, args: argparse.Namespace, root: pathlib.Path, codec: str):
        self.args = args
        self.root = root
        self.codec = codec
        self.ext = codec
        self.signed = args.fixture_root / codec / f"signed.{codec}"
        self.unsigned = args.fixture_root / codec / f"unsigned.{codec}"
        self.ca = args.fixture_root / "pki/ca.pem"
        self.steps: list[list[str]] = []
        self.artifacts: list[dict[str, Any]] = []

    def command(self, argv: list[str], replacements: dict[str, str]) -> None:
        must_run(argv)
        self.steps.append(normalized_command(argv, replacements))

    def ffmpeg_roundtrip(self, source: pathlib.Path, after: pathlib.Path, shift: bool) -> None:
        container = after.with_suffix(".mp4")
        input_format = "h264" if self.codec == "h264" else "hevc"
        bitstream_filter = (
            "h264_mp4toannexb" if self.codec == "h264" else "hevc_mp4toannexb"
        )
        first = [
            str(self.args.ffmpeg), "-y", "-hide_banner", "-loglevel", "error",
            "-fflags", "+bitexact", "-r", "25",
        ]
        first += [
            "-f", input_format, "-i", str(source), "-map", "0:v:0", "-c:v", "copy",
            "-map_metadata", "-1", "-movflags", "+faststart",
        ]
        if shift:
            baseline = after.with_name("timestamp-baseline.mp4")
            baseline_argv = first + [str(baseline)]
            self.command(
                baseline_argv,
                {str(source): "<before>", str(baseline): "<baseline-container>"},
            )
            self.artifacts.append(
                {
                    "kind": "mp4-baseline",
                    "sha256": sha256(baseline),
                    "byte_size": baseline.stat().st_size,
                }
            )
            first += ["-output_ts_offset", "5"]
        first += [str(container)]
        replacements = {str(source): "<before>", str(container): "<container>"}
        self.command(first, replacements)
        if shift and sha256(baseline) == sha256(container):
            raise AssertionError("timestamp rewrite did not change the container artifact")
        second = [
            str(self.args.ffmpeg), "-y", "-hide_banner", "-loglevel", "error",
            "-fflags", "+bitexact", "-i", str(container), "-map", "0:v:0",
            "-c:v", "copy", "-bsf:v", bitstream_filter, "-f", input_format,
            str(after),
        ]
        self.command(
            second,
            {str(container): "<container>", str(after): "<after>"},
        )
        self.artifacts.append(
            {"kind": "mp4", "sha256": sha256(container), "byte_size": container.stat().st_size}
        )

    def transform(self, case: dict[str, Any]) -> tuple[pathlib.Path, pathlib.Path]:
        recipe = case["recipe"]
        before = self.signed
        after = self.root / f"{case['id']}.{self.ext}"
        nalus = split_nalus(self.signed.read_bytes())
        split = second_gop_start(self.codec, nalus)

        if recipe == "exact_copy":
            shutil.copyfile(before, after)
            self.steps.append(["copy", "<before>", "<after>"])
        elif recipe == "normalize_start_codes":
            write_nalus(after, nalus, 3)
            self.steps.append(["normalize-start-codes", "--size", "3", "<before>", "<after>"])
        elif recipe == "mp4_roundtrip":
            self.ffmpeg_roundtrip(before, after, False)
        elif recipe == "gop_aligned_prefix":
            end = signing_indices(nalus)[0] + 1
            write_nalus(after, nalus[:end])
            self.steps.append(["slice-nalus", "--start", "0", "--end", str(end), "<before>", "<after>"])
        elif recipe == "non_aligned_clip":
            write_nalus(after, nalus[4:-3])
            self.steps.append(["slice-nalus", "--start", "4", "--drop-end", "3", "<before>", "<after>"])
        elif recipe == "beginning_truncation":
            write_nalus(after, nalus[3:])
            self.steps.append(["slice-nalus", "--start", "3", "<before>", "<after>"])
        elif recipe == "ending_truncation":
            write_nalus(after, nalus[:-3])
            self.steps.append(["slice-nalus", "--drop-end", "3", "<before>", "<after>"])
        elif recipe == "segment_first":
            write_nalus(after, nalus[:split])
            self.steps.append(["segment-nalus", "--part", "0", "--split", str(split), "<before>", "<after>"])
        elif recipe == "segment_second":
            write_nalus(after, nalus[split:])
            self.steps.append(["segment-nalus", "--part", "1", "--split", str(split), "<before>", "<after>"])
        elif recipe == "concatenate_adjacent":
            write_nalus(after, nalus[:split] + nalus[split:])
            self.steps.append(["concatenate-nal-segments", "--order", "0,1", "<before>", "<after>"])
        elif recipe == "concatenate_reordered":
            write_nalus(after, nalus[split:] + nalus[:split])
            self.steps.append(["concatenate-nal-segments", "--order", "1,0", "<before>", "<after>"])
        elif recipe == "strip_signing_sei":
            argv = [
                str(self.args.video_trust), "tamper", "--codec", self.codec,
                "--operation", "strip-signing-sei", "--quiet", "--force", "-o",
                str(after), str(before),
            ]
            self.command(argv, {str(before): "<before>", str(after): "<after>"})
        elif recipe == "remove_non_signing_metadata":
            before = self.root / f"signed-with-metadata.{self.ext}"
            signed_nalus = list(nalus)
            signed_nalus.insert(1, non_signing_sei(self.codec))
            write_nalus(before, signed_nalus)
            self.steps.append(
                ["inject-fixed-non-signing-user-data-sei", "<signed-source>", "<before>"]
            )
            removed = [nal for nal in signed_nalus if NON_SIGNING_UUID not in nal]
            if len(removed) + 1 != len(signed_nalus):
                raise AssertionError("non-signing metadata control not uniquely removable")
            write_nalus(after, removed)
            self.steps.append(["remove-non-signing-user-data-sei", "<before>", "<after>"])
        elif recipe == "reorder_nal":
            reordered = list(nalus)
            reordered[4], reordered[5] = reordered[5], reordered[4]
            write_nalus(after, reordered)
            self.steps.append(["swap-nalus", "--indices", "4,5", "<before>", "<after>"])
        elif recipe == "duplicate_nal":
            duplicated = list(nalus)
            duplicated.insert(6, duplicated[5])
            write_nalus(after, duplicated)
            self.steps.append(["duplicate-nal", "--index", "5", "<before>", "<after>"])
        elif recipe == "timestamp_shift_roundtrip":
            self.ffmpeg_roundtrip(before, after, True)
        elif recipe == "transcode":
            output_format = "h264" if self.codec == "h264" else "hevc"
            encoder = "libx264" if self.codec == "h264" else "libx265"
            argv = [
                str(self.args.ffmpeg), "-y", "-hide_banner", "-loglevel", "error",
                "-fflags", "+bitexact", "-i", str(before), "-map", "0:v:0", "-an",
                "-c:v", encoder, "-preset", "medium", "-threads", "1",
                "-pix_fmt", "yuv420p", "-g", "15", "-map_metadata", "-1",
            ]
            if self.codec == "h264":
                argv += ["-bf", "0", "-crf", "23", "-flags", "+bitexact"]
            else:
                argv += [
                    "-crf", "28", "-x265-params",
                    "pools=1:frame-threads=1:wpp=0:no-info=1:keyint=15:min-keyint=15:scenecut=0",
                ]
            argv += ["-f", output_format, str(after)]
            self.command(argv, {str(before): "<before>", str(after): "<after>"})
        elif recipe == "unsigned_exact":
            before = self.unsigned
            shutil.copyfile(before, after)
            self.steps.append(["copy", "<unsigned-before>", "<after>"])
        else:
            raise AssertionError(f"unapproved recipe: {recipe}")
        return before, after


def assess(
    args: argparse.Namespace,
    schema: dict[str, Any],
    expectation_fields: list[str],
    case: dict[str, Any],
    codec: str,
    root: pathlib.Path,
) -> dict[str, Any]:
    harness = Harness(args, root, codec)
    before, after = harness.transform(case)
    command = [
        str(args.video_trust), "compare-preservation", "--codec", codec,
        "--before-ca", str(harness.ca), "--after-ca", str(harness.ca),
        "--transformation", case["transformation"], "--pipeline-id", case["id"],
        "--json", str(before), str(after),
    ]
    completed = run(command)
    if completed.stderr:
        raise AssertionError(f"comparison emitted stderr: {completed.stderr}")
    document = json.loads(completed.stdout)
    jsonschema.validators.validator_for(schema)(schema).validate(document)
    signing = document["correlation"]["signing_metadata"]
    result = {
        "case_id": case["id"],
        "codec": codec,
        "transformation": case["transformation"],
        "before_sha256": sha256(before),
        "after_sha256": sha256(after),
        "after_byte_size": after.stat().st_size,
        "byte_relation": document["artifact_identity"]["byte_relation"],
        "stream_relation": document["correlation"]["stream_relation"],
        "coverage": document["coverage"]["state"],
        "applicability": document["applicability"]["media_signing_preservation"],
        "preservation": document["preservation"]["media_signing_evidence"],
        "verification_completeness": document["after"]["verification"]["verification_completeness"],
        "signing_metadata_relation": signing["relation"],
        "exit_code": completed.returncode,
        "schema_valid": True,
        "findings": [finding["code"] for finding in document["findings"]],
        "steps": harness.steps,
        "intermediate_artifacts": harness.artifacts,
    }
    expected_values = case.get("expected", {}).get(codec)
    if expected_values is not None:
        if len(expected_values) != len(expectation_fields):
            raise AssertionError(f"{case['id']} {codec}: malformed expectation")
        for field, value in zip(expectation_fields, expected_values):
            if result[field] != value:
                raise AssertionError(
                    f"{case['id']} {codec}: {field}={result[field]!r}, expected {value!r}"
                )
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--video-trust", type=pathlib.Path, required=True)
    parser.add_argument("--fixture-root", type=pathlib.Path, required=True)
    parser.add_argument("--schema", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--ffmpeg", type=pathlib.Path, required=True)
    parser.add_argument("--ffprobe", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    jsonschema.validators.validator_for(schema).check_schema(schema)

    allowed = {
        "exact_copy", "normalize_start_codes", "mp4_roundtrip", "gop_aligned_prefix",
        "non_aligned_clip", "beginning_truncation", "ending_truncation",
        "segment_first", "segment_second", "concatenate_adjacent",
        "concatenate_reordered", "strip_signing_sei", "remove_non_signing_metadata",
        "reorder_nal", "duplicate_nal", "timestamp_shift_roundtrip", "transcode",
        "unsigned_exact",
    }
    if manifest.get("manifest_version") != "0.1":
        raise AssertionError("unsupported manifest version")
    if any(case.get("recipe") not in allowed for case in manifest["cases"]):
        raise AssertionError("manifest contains an unapproved recipe")

    ffmpeg_version = run([str(args.ffmpeg), "-version"]).stdout.splitlines()[0]
    ffprobe_version = run([str(args.ffprobe), "-version"]).stdout.splitlines()[0]
    results: list[dict[str, Any]] = []
    with tempfile.TemporaryDirectory(prefix="nanexus-preservation-matrix-") as temp:
        temp_root = pathlib.Path(temp)
        for case in manifest["cases"]:
            for codec in case["codecs"]:
                case_root = temp_root / codec / case["id"]
                case_root.mkdir(parents=True)
                results.append(
                    assess(args, schema, manifest["expectation_fields"], case, codec, case_root)
                )

    matrix = {
        "matrix_version": "0.1",
        "manifest_version": manifest["manifest_version"],
        "tools": {"ffmpeg": ffmpeg_version, "ffprobe": ffprobe_version},
        "results": results,
    }
    encoded = json.dumps(matrix, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded, encoding="utf-8")
    print(f"PASS: preservation matrix ({len(results)} codec-cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
