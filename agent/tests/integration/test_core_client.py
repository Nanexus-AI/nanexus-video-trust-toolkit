"""Core client process boundary tests."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest

from nanexus_video_trust_agent.contracts import Codec, ErrorCode, OverallState
from nanexus_video_trust_agent.core_client import (
    MAX_TIMEOUT_SECONDS,
    STDERR_CAP_BYTES,
    CoreClient,
    ProcessObservation,
    build_verify_argv,
    child_environment,
    classify_exit_2,
    interpret_verify_process,
)
from nanexus_video_trust_agent.policy import AllowedRoots

SCRIPT = Path(__file__).resolve().parents[1] / "support" / "fake_video_trust.py"


def _document(overall: str) -> bytes:
    payload = {
        "schema_version": "0.1",
        "codec": "h264",
        "media_signing": {"present": overall != "UNSIGNED"},
        "signature_integrity": "ok" if overall != "INVALID" else "not_ok",
        "continuity": "broken" if overall == "INVALID" else "intact",
        "verification_completeness": "complete",
        "certificate_status": "not_provided",
        "source_authenticity": "not_established",
        "public_key_has_changed": False,
        "overall": overall,
        "vendor": None,
        "versions": {"signing": None, "validation": None},
        "findings": [],
        "future_field": True,
    }
    if overall == "UNSIGNED":
        payload["signature_integrity"] = "not_applicable"
        payload["continuity"] = "not_applicable"
        payload["verification_completeness"] = "incomplete"
    if overall == "PARTIAL":
        payload["verification_completeness"] = "incomplete"
    if overall == "NOT_VERIFIABLE":
        payload["signature_integrity"] = "not_feasible"
        payload["verification_completeness"] = "not_feasible"
    return json.dumps(payload).encode()


def test_exit_result_matrix_ignores_stderr() -> None:
    stderr = b"error: cannot open CA file: /home/example/secret-anchor.pem\nempty Annex-B input\n"
    cases = [
        (0, "VALID", None),
        (1, "INVALID", None),
        (4, "UNSIGNED", None),
        (4, "PARTIAL", None),
        (4, "NOT_VERIFIABLE", None),
    ]
    for exit_code, overall, _expected in cases:
        document, error, _schema = interpret_verify_process(
            ProcessObservation(exit_code=exit_code, stdout=_document(overall), stderr=stderr)
        )
        assert error is None
        assert document is not None
        assert document.overall is OverallState(overall)
        assert document.model_dump().get("future_field") is None

    document, error, _schema = interpret_verify_process(
        ProcessObservation(exit_code=0, stdout=_document("INVALID"), stderr=stderr)
    )
    assert document is None
    assert error is not None
    assert error.code is ErrorCode.CONTRACT_MISMATCH
    assert "secret-anchor" not in error.message

    document, error, schema = interpret_verify_process(
        ProcessObservation(exit_code=2, stdout=_document("VALID"), stderr=stderr)
    )
    assert document is None
    assert error is not None
    assert error.code is classify_exit_2()
    assert error.code is ErrorCode.CORE_INPUT_REJECTED
    assert error.code is not ErrorCode.MALFORMED_MEDIA
    assert error.code is not ErrorCode.INVALID_TRUST_ANCHOR
    assert "secret-anchor" not in error.message
    assert "Annex-B" not in error.message
    assert schema is None


def test_schema_and_json_failures() -> None:
    bad_schema = json.loads(_document("VALID"))
    bad_schema["schema_version"] = "9.9"
    document, error, schema = interpret_verify_process(
        ProcessObservation(exit_code=0, stdout=json.dumps(bad_schema).encode(), stderr=b"")
    )
    assert document is None
    assert error is not None
    assert error.code is ErrorCode.CONTRACT_MISMATCH
    assert schema == "9.9"

    document, error, _schema = interpret_verify_process(
        ProcessObservation(exit_code=0, stdout=b"not-json STDOUT_SENTINEL", stderr=b"")
    )
    assert document is None
    assert error is not None
    assert "STDOUT_SENTINEL" not in error.message

    established = json.loads(_document("VALID"))
    established["source_authenticity"] = "established"
    document, error, _schema = interpret_verify_process(
        ProcessObservation(exit_code=0, stdout=json.dumps(established).encode(), stderr=b"")
    )
    assert document is None
    assert error is not None
    assert error.code is ErrorCode.CONTRACT_MISMATCH


def test_argv_is_an_array_without_shell_commands() -> None:
    argv = build_verify_argv(
        Path("/opt/video-trust"),
        Codec.h265,
        Path("/data/video-lab/clip;rm.h265"),
        Path("/data/video-lab/ca.pem"),
    )
    assert argv == [
        "/opt/video-trust",
        "verify",
        "--codec",
        "h265",
        "--json",
        "--ca",
        "/data/video-lab/ca.pem",
        "/data/video-lab/clip;rm.h265",
    ]
    assert "sign" not in argv
    assert "tamper" not in argv
    assert isinstance(argv, list)
    with pytest.raises(ValueError):
        CoreClient("/opt/video-trust", timeout_seconds=MAX_TIMEOUT_SECONDS + 1)
    with pytest.raises(ValueError):
        CoreClient("/opt/video-trust", timeout_seconds=0)
    env = child_environment({"PATH": "/usr/bin", "HOME": "/home/example", "NANEXUS_ALLOWED_ROOTS": "/data", "LANG": "C.UTF-8"})
    assert "HOME" not in env
    assert "NANEXUS_ALLOWED_ROOTS" not in env
    assert env["PATH"] == "/usr/bin"


def _executable(tmp_path: Path, name: str) -> Path:
    SCRIPT.chmod(0o755)
    link = tmp_path / name
    link.symlink_to(SCRIPT)
    return link


def _client(tmp_path: Path, name: str, **kwargs) -> tuple[CoreClient, AllowedRoots, Path]:
    root = tmp_path / "root"
    clip = root / "nested" / "clip.h264"
    clip.parent.mkdir(parents=True, exist_ok=True)
    clip.write_bytes(b"annex-b-bytes")
    policy = AllowedRoots([root])
    return CoreClient(_executable(tmp_path, name), **kwargs), policy, clip


def test_subprocess_success_hides_host_paths(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("HOME", "/home/example")
    monkeypatch.setenv("NANEXUS_SECRET", "should-not-pass")
    client, policy, clip = _client(tmp_path, "vt-valid")
    seen: list[tuple] = []

    real_popen = subprocess.Popen

    def spy(*args, **kwargs):
        seen.append((args, kwargs))
        return real_popen(*args, **kwargs)

    monkeypatch.setattr(subprocess, "Popen", spy)
    result = client.verify(codec=Codec.h264, input_file=str(clip), policy=policy)
    verify_calls = [item for item in seen if item[0][0][1] == "verify"]
    assert len(verify_calls) == 1
    assert verify_calls[0][1]["shell"] is False
    assert "sign" not in verify_calls[0][0][0]
    assert "tamper" not in verify_calls[0][0][0]

    assert result.error is None
    assert result.document is not None
    assert result.document.overall is OverallState.VALID
    payload = json.dumps(result.public_payload())
    assert result.evidence.input_reference == "nested/clip.h264"
    assert result.evidence.input_sha256 == hashlib.sha256(b"annex-b-bytes").hexdigest()
    assert result.evidence.core_name == "video-trust"
    assert result.evidence.core_version == "0.1.0"
    assert result.evidence.core_schema_version == "0.1"
    assert result.evidence.core_exit_code == 0
    assert str(tmp_path) not in payload
    assert "/home/example" not in payload
    assert "NANEXUS_SECRET" not in payload
    assert "retained_stderr" not in payload
    recorded_env = (tmp_path / "vt-valid.env").read_text().splitlines()
    assert "HOME" not in recorded_env
    assert "NANEXUS_SECRET" not in recorded_env


def test_subprocess_negative_results_stay_success(tmp_path: Path) -> None:
    for name, overall, exit_code in (
        ("vt-invalid", OverallState.INVALID, 1),
        ("vt-unsigned", OverallState.UNSIGNED, 4),
        ("vt-partial", OverallState.PARTIAL, 4),
        ("vt-not-verifiable", OverallState.NOT_VERIFIABLE, 4),
    ):
        client, policy, clip = _client(tmp_path, name)
        result = client.verify(codec=Codec.h264, input_file=str(clip), policy=policy)
        assert result.error is None
        assert result.document is not None
        assert result.document.overall is overall
        assert result.evidence.core_exit_code == exit_code


def test_subprocess_exit2_timeout_caps_and_unavailable(tmp_path: Path) -> None:
    client, policy, clip = _client(tmp_path, "vt-exit2")
    rejected = client.verify(codec=Codec.h264, input_file=str(clip), policy=policy)
    assert rejected.document is None
    assert rejected.error is not None
    assert rejected.error.code is ErrorCode.CORE_INPUT_REJECTED
    payload = json.dumps(rejected.public_payload())
    assert "secret-anchor" not in payload
    assert "empty Annex-B" not in payload
    assert b"secret-anchor" in rejected.retained_stderr

    sleeping, sleep_policy, sleep_clip = _client(tmp_path, "vt-sleep", timeout_seconds=0.2)
    timed = sleeping.verify(codec=Codec.h264, input_file=str(sleep_clip), policy=sleep_policy)
    assert timed.error is not None
    assert timed.error.code is ErrorCode.CORE_TIMEOUT
    assert timed.error.retryable is True

    huge, huge_policy, huge_clip = _client(tmp_path, "vt-huge-out")
    oversized = huge.verify(codec=Codec.h264, input_file=str(huge_clip), policy=huge_policy)
    assert oversized.document is None
    assert oversized.error is not None
    assert oversized.error.code is ErrorCode.CORE_EXECUTION_FAILED
    assert "A" * 100 not in oversized.error.message

    noisy, noisy_policy, noisy_clip = _client(tmp_path, "vt-huge-err")
    noisy_result = noisy.verify(codec=Codec.h264, input_file=str(noisy_clip), policy=noisy_policy)
    assert noisy_result.error is None
    assert noisy_result.document is not None
    assert len(noisy_result.retained_stderr) <= STDERR_CAP_BYTES
    assert "STDERR_SENTINEL" not in json.dumps(noisy_result.public_payload())

    missing = CoreClient(tmp_path / "missing-video-trust")
    unavailable = missing.verify(codec=Codec.h264, input_file=str(clip), policy=policy)
    assert unavailable.error is not None
    assert unavailable.error.code is ErrorCode.CORE_UNAVAILABLE
    assert str(tmp_path) not in unavailable.error.message


def test_out_of_policy_path_is_not_hashed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = tmp_path / "video-lab"
    root.mkdir()
    outside = tmp_path / "video-lab-evil" / "clip.h264"
    outside.parent.mkdir()
    outside.write_bytes(b"do-not-hash")
    calls: list[Path] = []
    real_open = Path.open

    def spy_open(self: Path, *args, **kwargs):
        if self == outside:
            calls.append(self)
        return real_open(self, *args, **kwargs)

    monkeypatch.setattr(Path, "open", spy_open)
    client = CoreClient(_executable(tmp_path, "vt-valid"))
    result = client.verify(codec=Codec.h264, input_file=str(outside), policy=AllowedRoots([root]))
    assert result.error is not None
    assert result.error.code is ErrorCode.PATH_NOT_ALLOWED
    assert result.evidence.input_sha256 is None
    assert result.evidence.input_reference is None
    assert calls == []
    assert str(outside) not in json.dumps(result.public_payload())


_REAL_BINARY = Path(__file__).resolve().parents[3] / "build" / "nanexus" / "video-trust"


@pytest.mark.skipif(not _REAL_BINARY.is_file(), reason="video-trust is not built")
def test_real_video_trust_exit_2_is_not_malformed_media(tmp_path: Path) -> None:
    root = tmp_path / "video-lab"
    clip = root / "not-media.bin"
    clip.parent.mkdir()
    clip.write_bytes(b"this is not annex-b")
    result = CoreClient(_REAL_BINARY).verify(
        codec=Codec.h264,
        input_file=str(clip),
        policy=AllowedRoots([root]),
    )
    assert result.document is None
    assert result.error is not None
    assert result.error.code is ErrorCode.CORE_INPUT_REJECTED
    assert result.evidence.core_exit_code == 2
    assert result.evidence.core_version == "0.1.0"
    assert result.evidence.input_reference == "not-media.bin"
    payload = json.dumps(result.public_payload())
    assert str(tmp_path) not in payload
    stderr_text = result.retained_stderr.decode("utf-8", errors="replace").strip()
    if stderr_text:
        assert stderr_text not in payload
