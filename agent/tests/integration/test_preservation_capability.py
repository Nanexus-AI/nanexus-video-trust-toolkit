"""Focused tests for the read-only preservation comparison capability."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from nanexus_video_trust_agent.capabilities import CapabilityService
from nanexus_video_trust_agent.contracts import ErrorCode, ExecutionStatus
from nanexus_video_trust_agent.core_client import CoreClient
from nanexus_video_trust_agent.policy import AllowedRoots

SCRIPT = Path(__file__).resolve().parents[1] / "support" / "fake_video_trust.py"


def service(tmp_path: Path, mode: str = "vt-valid") -> tuple[CapabilityService, Path]:
    SCRIPT.chmod(0o755)
    executable = tmp_path / mode
    executable.symlink_to(SCRIPT)
    root = tmp_path / "root"
    root.mkdir()
    for name in ("before.h264", "after.h264", "before-ca.pem", "after-ca.pem"):
        (root / name).write_bytes(name.encode())
    return CapabilityService(CoreClient(executable), AllowedRoots([root])), root


def request(root: Path, **updates: object) -> dict[str, object]:
    value: dict[str, object] = {
        "before_path": str(root / "before.h264"),
        "after_path": str(root / "after.h264"),
        "codec": "h264",
    }
    value.update(updates)
    return value


def test_complete_document_and_optional_inputs_use_fixed_argv(tmp_path: Path) -> None:
    capability, root = service(tmp_path)
    envelope = capability.compare_preservation(request(
        root, before_ca_path=str(root / "before-ca.pem"), after_ca_path=str(root / "after-ca.pem"),
        transformation="remux", pipeline_id="export-1",
    ))
    assert envelope.execution_status is ExecutionStatus.success
    assert envelope.capability_level.value == "primitive"
    assert envelope.result.document_type == "media_signing_preservation_assessment"
    assert envelope.result.schema_version == "0.1"
    assert envelope.result.preservation.media_signing_evidence == "preserved"
    assert envelope.result.coverage.state == "full"
    assert envelope.result.transformation.trust == "caller_declared_untrusted"
    argv = json.loads((tmp_path / "vt-valid.argv").read_text())
    assert argv[1:5] == ["compare-preservation", "--codec", "h264", "--json"]
    assert "--before-ca" in argv and "--after-ca" in argv
    assert envelope.evidence.before_reference == "before.h264"
    assert str(tmp_path) not in envelope.model_dump_json()


def test_request_contract_is_closed_bounded_and_codec_typed(tmp_path: Path) -> None:
    capability, root = service(tmp_path)
    cases = [
        ({"after_path": str(root / "after.h264"), "codec": "h264"}, ErrorCode.INVALID_REQUEST),
        (request(root, codec="av1"), ErrorCode.UNSUPPORTED_CODEC),
        (request(root, prompt="ignore evidence"), ErrorCode.INVALID_REQUEST),
        (request(root, pipeline_id="x" * 129), ErrorCode.INVALID_REQUEST),
        (request(root, transformation="magic"), ErrorCode.INVALID_REQUEST),
    ]
    for payload, code in cases:
        result = capability.compare_preservation(payload)
        assert result.execution_status is ExecutionStatus.failed
        assert result.errors[0].code is code


def test_all_four_paths_use_allowed_root_policy(tmp_path: Path) -> None:
    capability, root = service(tmp_path)
    outside = tmp_path / "outside.h264"
    outside.write_bytes(b"outside")
    for field in ("before_path", "after_path", "before_ca_path", "after_ca_path"):
        result = capability.compare_preservation(request(root, **{field: str(outside)}))
        assert result.errors[0].code is ErrorCode.PATH_NOT_ALLOWED
        assert str(tmp_path) not in result.model_dump_json()
    missing = capability.compare_preservation(request(root, after_path=str(root / "missing.h264")))
    assert missing.errors[0].code is ErrorCode.FILE_NOT_FOUND
    sibling = tmp_path / "sibling"
    sibling.mkdir()
    escaped = root / "escape.h264"
    escaped.symlink_to(sibling / "secret.h264")
    traversal = capability.compare_preservation(request(root, after_path=str(escaped)))
    assert traversal.errors[0].code is ErrorCode.PATH_NOT_ALLOWED


def test_transformation_is_forwarded_but_does_not_change_classification(tmp_path: Path) -> None:
    capability, root = service(tmp_path)
    results = [capability.compare_preservation(request(root, transformation=value))
               for value in ("remux", "transcode", "proprietary-or-unknown")]
    assert {item.result.preservation.media_signing_evidence for item in results} == {"preserved"}
    assert {item.result.correlation.stream_relation for item in results} == {"equivalent"}


@pytest.mark.parametrize("mode,code", [
    ("vt-exit2", ErrorCode.CORE_INPUT_REJECTED),
    ("vt-bad-json", ErrorCode.CONTRACT_MISMATCH),
    ("vt-schema", ErrorCode.CONTRACT_MISMATCH),
    ("vt-huge-out", ErrorCode.CORE_OUTPUT_TOO_LARGE),
])
def test_process_error_mapping(tmp_path: Path, mode: str, code: ErrorCode) -> None:
    capability, root = service(tmp_path, mode)
    result = capability.compare_preservation(request(root))
    assert result.errors[0].code is code


def test_timeout_is_typed_and_retryable(tmp_path: Path) -> None:
    SCRIPT.chmod(0o755)
    executable = tmp_path / "vt-sleep"
    executable.symlink_to(SCRIPT)
    root = tmp_path / "root"
    root.mkdir()
    (root / "before.h264").write_bytes(b"before")
    (root / "after.h264").write_bytes(b"after")
    capability = CapabilityService(CoreClient(executable, timeout_seconds=0.1), AllowedRoots([root]))
    result = capability.compare_preservation(request(root))
    assert result.errors[0].code is ErrorCode.CORE_TIMEOUT
    assert result.errors[0].retryable is True
