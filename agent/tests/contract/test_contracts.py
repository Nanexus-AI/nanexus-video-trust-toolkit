"""Request, envelope, and version contract tests."""

from __future__ import annotations

import sys
from datetime import datetime, timezone

import pytest
from pydantic import ValidationError

from nanexus_video_trust_agent import __version__
from nanexus_video_trust_agent.contracts import (
    CAPABILITY_VERSION,
    CONTRACT_VERSION,
    CORE_SCHEMA_VERSION,
    PRODUCT_VERSION,
    CapabilityEnvelope,
    CapabilityError,
    CapabilityLevel,
    Codec,
    ErrorCode,
    Evidence,
    ExecutionStatus,
    VerifyFileRequest,
    classify_request_validation,
)


def _evidence(**overrides) -> Evidence:
    payload = {
        "capability": "video_trust.verify_file",
        "invocation_id": "3b6f1a7e-8c2d-4f0a-9b11-6e5d4c3b2a10",
        "executed_at": datetime.now(timezone.utc),
    }
    payload.update(overrides)
    return Evidence.model_validate(payload)


def _document() -> dict:
    return {
        "schema_version": "0.1",
        "codec": "h264",
        "media_signing": {"present": True},
        "signature_integrity": "ok",
        "continuity": "intact",
        "verification_completeness": "complete",
        "certificate_status": "ok",
        "source_authenticity": "not_established",
        "public_key_has_changed": False,
        "overall": "VALID",
        "vendor": None,
        "versions": {"signing": None, "validation": None},
        "findings": [],
    }


def test_package_imports_without_mcp() -> None:
    sys.modules.pop("mcp", None)
    import nanexus_video_trust_agent.contracts  # noqa: F401
    import nanexus_video_trust_agent.core_client  # noqa: F401
    import nanexus_video_trust_agent.policy  # noqa: F401

    assert "mcp" not in sys.modules
    assert __version__ == PRODUCT_VERSION


def test_versions_are_independent_constants() -> None:
    assert PRODUCT_VERSION == "0.1.0"
    assert CORE_SCHEMA_VERSION == "0.1"
    assert CONTRACT_VERSION == "0.1"
    assert CAPABILITY_VERSION == "0.1"
    assert PRODUCT_VERSION is not CORE_SCHEMA_VERSION


def test_request_requires_codec_and_input_file() -> None:
    schema = VerifyFileRequest.model_json_schema()
    assert set(schema["required"]) == {"input_file", "codec"}
    assert "trust_anchor" in schema["properties"]
    assert "prompt" not in schema["properties"]
    assert "command" not in schema["properties"]
    codec_schema = schema["properties"]["codec"]
    if "$ref" in codec_schema:
        codec_schema = schema["$defs"][codec_schema["$ref"].rsplit("/", 1)[-1]]
    assert set(codec_schema["enum"]) == {"h264", "h265"}


def test_request_rejects_bad_paths_and_extra_fields() -> None:
    with pytest.raises(ValidationError):
        VerifyFileRequest.model_validate({"codec": "h264"})
    with pytest.raises(ValidationError):
        VerifyFileRequest.model_validate({"input_file": "clip.h264", "codec": "h264", "prompt": "look"})
    with pytest.raises(ValidationError):
        VerifyFileRequest(input_file="", codec=Codec.h264)
    with pytest.raises(ValidationError):
        VerifyFileRequest(input_file="clip.h264\nrm", codec=Codec.h264)
    with pytest.raises(ValidationError):
        VerifyFileRequest(input_file="clip.h264", codec=Codec.h264, trust_anchor="")


def test_explicit_bad_codec_is_unsupported() -> None:
    with pytest.raises(ValidationError) as caught:
        VerifyFileRequest.model_validate({"input_file": "clip.h264", "codec": "av1"})
    assert classify_request_validation(caught.value) is ErrorCode.UNSUPPORTED_CODEC

    with pytest.raises(ValidationError) as missing:
        VerifyFileRequest.model_validate({"input_file": "clip.h264"})
    assert classify_request_validation(missing.value) is ErrorCode.INVALID_REQUEST


def test_success_and_failure_envelope_invariants() -> None:
    from nanexus_video_trust_agent.contracts import VerificationDocument

    document = VerificationDocument.model_validate(_document())
    success = CapabilityEnvelope(
        capability="video_trust.verify_file",
        capability_level=CapabilityLevel.primitive,
        execution_status=ExecutionStatus.success,
        result=document,
        evidence=_evidence(),
    )
    assert success.errors == []
    assert success.safety_level == "read"
    assert success.contract_version == "0.1"

    failure = CapabilityEnvelope(
        capability="video_trust.verify_file",
        capability_level=CapabilityLevel.primitive,
        execution_status=ExecutionStatus.failed,
        result=None,
        evidence=_evidence(),
        errors=[CapabilityError(code=ErrorCode.CORE_INPUT_REJECTED, message="rejected")],
    )
    assert failure.result is None

    with pytest.raises(ValidationError):
        CapabilityEnvelope(
            capability="video_trust.verify_file",
            capability_level=CapabilityLevel.primitive,
            execution_status=ExecutionStatus.success,
            result=None,
            evidence=_evidence(),
        )
    with pytest.raises(ValidationError):
        CapabilityEnvelope(
            capability="video_trust.verify_file",
            capability_level=CapabilityLevel.primitive,
            execution_status=ExecutionStatus.failed,
            result=document,
            evidence=_evidence(),
            errors=[CapabilityError(code=ErrorCode.CORE_INPUT_REJECTED, message="rejected")],
        )


def test_retryable_is_only_timeout() -> None:
    CapabilityError(code=ErrorCode.CORE_TIMEOUT, message="timed out", retryable=True)
    with pytest.raises(ValidationError):
        CapabilityError(code=ErrorCode.CORE_TIMEOUT, message="timed out", retryable=False)
    with pytest.raises(ValidationError):
        CapabilityError(code=ErrorCode.CORE_INPUT_REJECTED, message="rejected", retryable=True)


def test_evidence_rejects_absolute_references_and_host_fields() -> None:
    with pytest.raises(ValidationError):
        _evidence(input_reference="/tmp/video-lab/clip.h264")
    with pytest.raises(ValidationError):
        _evidence(trust_anchor_reference="../../etc/passwd")
    evidence = _evidence(input_reference="nested/clip.h264", input_sha256="ab" * 32)
    dumped = evidence.model_dump()
    assert "input_path" not in dumped
    assert "trust_anchor_path" not in dumped
    assert dumped["core_name"] == "video-trust"
    for code in (ErrorCode.MALFORMED_MEDIA, ErrorCode.INVALID_TRUST_ANCHOR, ErrorCode.CORE_INPUT_REJECTED):
        assert code.value
