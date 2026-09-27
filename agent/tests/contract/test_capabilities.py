"""Direct capability calls. No MCP server is started."""

from __future__ import annotations

import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from uuid import uuid4

import pytest

from nanexus_video_trust_agent.capabilities import (
    CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY,
    DO_NOT_UPGRADE_PARTIAL,
    INTEGRITY_FAILURE,
    NO_MEDIA_SIGNING_EVIDENCE,
    REVERIFY_WITH_TRUST_ANCHOR,
    SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE,
    SOURCE_AUTHENTICITY_NOT_ESTABLISHED,
    TRUST_ANCHOR_NOT_SUPPLIED,
    VALID_IS_NOT_CERTIFICATE_TRUST,
    CapabilityService,
    follow_up_hints_for,
    integrity_assessment_for,
    limitations_for,
    trust_assessment_for,
)
from nanexus_video_trust_agent.contracts import (
    ASSESS_VIDEO_INTEGRITY_CAPABILITY,
    VERIFY_FILE_CAPABILITY,
    CapabilityLevel,
    CertificateStatus,
    Codec,
    ErrorCode,
    Evidence,
    ExecutionStatus,
    IntegrityAssessment,
    IntegrityAssessmentResult,
    OverallState,
    TrustAssessment,
    VerificationDocument,
    VerifyFileRequest,
)
from nanexus_video_trust_agent.core_client import CoreClient, CoreInvocation, _error
from nanexus_video_trust_agent.policy import AllowedRoots

SCRIPT = Path(__file__).resolve().parents[1] / "support" / "fake_video_trust.py"
AXES = {
    "schema_version",
    "codec",
    "media_signing",
    "signature_integrity",
    "continuity",
    "verification_completeness",
    "certificate_status",
    "source_authenticity",
    "public_key_has_changed",
    "overall",
    "vendor",
    "versions",
    "findings",
}


def _document(**overrides: object) -> VerificationDocument:
    payload: dict[str, object] = {
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
        "findings": [{"code": "example", "message": "kept"}],
    }
    payload.update(overrides)
    return VerificationDocument.model_validate(payload)


class ScriptedClient:
    def __init__(self, document: VerificationDocument | None = None, error=None) -> None:
        self.document = document
        self.error = error
        self.calls: list[dict] = []
        self.stderr = b"stderr-sentinel /home/example/leak.pem Annex-B input"

    def verify(self, **kwargs):
        self.calls.append(kwargs)
        failed = self.error is not None
        return CoreInvocation(
            evidence=Evidence(
                capability=kwargs["capability"],
                invocation_id=str(uuid4()),
                executed_at=datetime.now(timezone.utc),
                codec=kwargs["codec"],
                input_reference=None if failed else "nested/clip.h264",
                input_sha256=None if failed else "ab" * 32,
                core_version=None if failed else "0.1.0",
                core_schema_version=None if failed else "0.1",
                core_exit_code=None if failed else 0,
            ),
            document=None if failed else self.document,
            error=self.error,
            retained_stderr=self.stderr,
        )


def _service(client: ScriptedClient, tmp_path: Path) -> CapabilityService:
    root = tmp_path / "root"
    root.mkdir(exist_ok=True)
    return CapabilityService(client, AllowedRoots([root]))  # type: ignore[arg-type]


def test_capabilities_import_without_mcp_or_agent_frameworks() -> None:
    sys.modules.pop("mcp", None)
    import nanexus_video_trust_agent.capabilities as capabilities

    assert "mcp" not in sys.modules
    source = Path(capabilities.__file__).read_text(encoding="utf-8").lower()
    for banned in ("import mcp", "langgraph", "openai", "autogen", "crewai", "ollama"):
        assert banned not in source


def test_integrity_and_trust_maps_cover_every_enum() -> None:
    assert {integrity_assessment_for(item) for item in OverallState} == set(IntegrityAssessment)
    assert {trust_assessment_for(item) for item in CertificateStatus} == set(TrustAssessment)
    assert integrity_assessment_for(OverallState.VALID) is IntegrityAssessment.signing_integrity_intact
    assert integrity_assessment_for(OverallState.INVALID) is IntegrityAssessment.signing_integrity_failed
    assert integrity_assessment_for(OverallState.UNSIGNED) is IntegrityAssessment.no_media_signing
    assert integrity_assessment_for(OverallState.PARTIAL) is IntegrityAssessment.partial_evidence
    assert integrity_assessment_for(OverallState.NOT_VERIFIABLE) is IntegrityAssessment.not_verifiable
    assert (
        trust_assessment_for(CertificateStatus.not_provided)
        is TrustAssessment.certificate_trust_not_evaluated
    )
    assert (
        trust_assessment_for(CertificateStatus.ok)
        is TrustAssessment.signing_key_validated_against_trust_anchor
    )
    assert trust_assessment_for(CertificateStatus.not_ok) is TrustAssessment.signing_key_not_trusted
    assert (
        trust_assessment_for(CertificateStatus.not_feasible)
        is TrustAssessment.certificate_provenance_not_feasible
    )
    assert (
        trust_assessment_for(CertificateStatus.feasible_without_trusted)
        is TrustAssessment.certificate_provenance_without_trusted_anchor
    )


def test_limitations_and_hints_follow_the_closed_catalog() -> None:
    valid_ok = _document()
    valid_open = _document(certificate_status="not_provided")
    invalid_ok = _document(overall="INVALID", signature_integrity="not_ok", continuity="broken")
    unsigned = _document(
        overall="UNSIGNED",
        media_signing={"present": False},
        signature_integrity="not_applicable",
        continuity="not_applicable",
        verification_completeness="incomplete",
        certificate_status="not_provided",
    )
    partial = _document(overall="PARTIAL", verification_completeness="incomplete")
    opaque = _document(overall="NOT_VERIFIABLE", certificate_status="not_feasible")

    assert limitations_for(valid_ok, semantic=False) == [SOURCE_AUTHENTICITY_NOT_ESTABLISHED]
    assert limitations_for(valid_ok, semantic=True) == [
        SOURCE_AUTHENTICITY_NOT_ESTABLISHED,
        CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY,
    ]
    assert limitations_for(valid_open, semantic=True) == [
        SOURCE_AUTHENTICITY_NOT_ESTABLISHED,
        TRUST_ANCHOR_NOT_SUPPLIED,
        VALID_IS_NOT_CERTIFICATE_TRUST,
    ]
    assert NO_MEDIA_SIGNING_EVIDENCE in limitations_for(unsigned, semantic=True)
    assert NO_MEDIA_SIGNING_EVIDENCE not in limitations_for(unsigned, semantic=False)
    assert [hint.code for hint in follow_up_hints_for(invalid_ok)] == ["INTEGRITY_FAILURE"]
    assert follow_up_hints_for(invalid_ok) == [INTEGRITY_FAILURE]
    assert REVERIFY_WITH_TRUST_ANCHOR in follow_up_hints_for(valid_open)
    assert follow_up_hints_for(unsigned) == [
        REVERIFY_WITH_TRUST_ANCHOR,
        SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE,
    ]
    assert follow_up_hints_for(partial) == [DO_NOT_UPGRADE_PARTIAL]
    assert follow_up_hints_for(opaque) == [DO_NOT_UPGRADE_PARTIAL]
    assert follow_up_hints_for(valid_ok) == []


def test_verify_file_and_assess_share_one_client_call_each(tmp_path: Path) -> None:
    document = _document()
    client = ScriptedClient(document)
    service = _service(client, tmp_path)
    request = VerifyFileRequest(input_file="nested/clip.h264", codec=Codec.h264)

    primitive = service.verify_file(request)
    semantic = service.assess_video_integrity(request)

    assert len(client.calls) == 2
    assert client.calls[0]["capability"] == VERIFY_FILE_CAPABILITY
    assert client.calls[1]["capability"] == ASSESS_VIDEO_INTEGRITY_CAPABILITY
    assert primitive.execution_status is ExecutionStatus.success
    assert primitive.capability_level is CapabilityLevel.primitive
    assert primitive.errors == []
    assert isinstance(primitive.result, VerificationDocument)
    assert set(primitive.result.model_dump()) == AXES
    assert primitive.result == document
    assert primitive.limitations == [SOURCE_AUTHENTICITY_NOT_ESTABLISHED]
    assert semantic.capability_level is CapabilityLevel.domain_semantic
    assert isinstance(semantic.result, IntegrityAssessmentResult)
    assert semantic.result.verification == primitive.result
    assert semantic.result.integrity_assessment is IntegrityAssessment.signing_integrity_intact
    assert (
        semantic.result.trust_assessment
        is TrustAssessment.signing_key_validated_against_trust_anchor
    )
    assert semantic.result.follow_up_hints == []
    assert semantic.evidence.invocation_id != primitive.evidence.invocation_id
    dumped = json.dumps(semantic.model_dump(mode="json"))
    assert "stderr-sentinel" not in dumped
    assert "/home/example" not in dumped
    assert semantic.evidence.input_reference == "nested/clip.h264"


def test_negative_domain_results_stay_successful(tmp_path: Path) -> None:
    document = _document(
        overall="UNSIGNED",
        media_signing={"present": False},
        signature_integrity="not_applicable",
        continuity="not_applicable",
        verification_completeness="incomplete",
        certificate_status="not_provided",
    )
    service = _service(ScriptedClient(document), tmp_path)
    envelope = service.verify_file({"input_file": "nested/clip.h264", "codec": "h264"})
    assert envelope.execution_status is ExecutionStatus.success
    assert envelope.result is not None
    assert envelope.result.overall is OverallState.UNSIGNED
    assert envelope.errors == []


def test_typed_errors_pass_through_without_stderr(tmp_path: Path) -> None:
    for code in ErrorCode:
        error = _error(code, f"stable-marker-{code.value}")
        service = _service(ScriptedClient(error=error), tmp_path)
        envelope = service.assess_video_integrity(
            VerifyFileRequest(input_file="nested/clip.h264", codec=Codec.h265)
        )
        assert envelope.execution_status is ExecutionStatus.failed
        assert envelope.result is None
        assert envelope.errors == [error]
        assert envelope.limitations == []
        dumped = json.dumps(envelope.model_dump(mode="json"))
        assert "stderr-sentinel" not in dumped
        assert "Annex-B" not in dumped
        assert f"stable-marker-{code.value}" in dumped


def test_request_errors_do_not_call_the_core(tmp_path: Path) -> None:
    client = ScriptedClient(_document())
    service = _service(client, tmp_path)
    unsupported = service.verify_file({"input_file": "clip.h264", "codec": "av1"})
    invalid = service.assess_video_integrity({"input_file": "clip.h264"})
    assert unsupported.errors[0].code is ErrorCode.UNSUPPORTED_CODEC
    assert invalid.errors[0].code is ErrorCode.INVALID_REQUEST
    assert client.calls == []
    assert unsupported.evidence.input_sha256 is None
    assert unsupported.evidence.input_reference is None


def test_policy_and_unavailable_errors_use_the_core_client(tmp_path: Path) -> None:
    root = tmp_path / "video-lab"
    root.mkdir()
    sibling = tmp_path / "video-lab-evil"
    sibling.mkdir()
    outside = sibling / "clip.h264"
    outside.write_bytes(b"outside")
    missing = root / "missing.h264"
    directory = root / "directory"
    directory.mkdir()
    present = root / "present.h264"
    present.write_bytes(b"annex-b")
    service = CapabilityService(CoreClient(tmp_path / "missing-video-trust"), AllowedRoots([root]))
    cases = [
        (str(outside), ErrorCode.PATH_NOT_ALLOWED),
        (str(missing), ErrorCode.FILE_NOT_FOUND),
        (str(directory), ErrorCode.INVALID_REQUEST),
        (str(present), ErrorCode.CORE_UNAVAILABLE),
    ]
    for path, code in cases:
        envelope = service.verify_file(VerifyFileRequest(input_file=path, codec=Codec.h264))
        assert envelope.errors[0].code is code
        dumped = json.dumps(envelope.model_dump(mode="json"))
        assert str(tmp_path) not in dumped
        assert envelope.evidence.input_sha256 is None


def _executable(tmp_path: Path, name: str) -> Path:
    SCRIPT.chmod(0o755)
    link = tmp_path / name
    link.symlink_to(SCRIPT)
    return link


@pytest.mark.parametrize(
    ("mode", "overall"),
    [("vt-invalid", "INVALID"), ("vt-unsigned", "UNSIGNED"), ("vt-partial", "PARTIAL")],
)
def test_fake_core_negative_verification_is_success(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, mode: str, overall: str
) -> None:
    root = tmp_path / "root"
    clip = root / "nested" / "clip.h264"
    clip.parent.mkdir(parents=True)
    clip.write_bytes(b"annex-b-bytes")
    calls: list[list[str]] = []
    real_popen = subprocess.Popen

    def spy(*args, **kwargs):
        argv = args[0] if args else kwargs["args"]
        calls.append([str(item) for item in argv])
        return real_popen(*args, **kwargs)

    monkeypatch.setattr(subprocess, "Popen", spy)
    service = CapabilityService(
        CoreClient(_executable(tmp_path, mode)),
        AllowedRoots([root]),
    )
    envelope = service.assess_video_integrity(
        VerifyFileRequest(input_file=str(clip), codec=Codec.h264)
    )
    verify_calls = [argv for argv in calls if len(argv) > 1 and argv[1] == "verify"]
    assert verify_calls == [[verify_calls[0][0], "verify", "--codec", "h264", "--json", str(clip)]]
    assert envelope.execution_status is ExecutionStatus.success
    assert envelope.result.verification.overall is OverallState(overall)
    assert envelope.errors == []
    dumped = json.dumps(envelope.model_dump(mode="json"))
    assert str(tmp_path) not in dumped
    assert envelope.evidence.input_reference == "nested/clip.h264"
    assert envelope.evidence.core_name == "video-trust"


def test_source_authenticity_drift_is_contract_mismatch(tmp_path: Path) -> None:
    root = tmp_path / "root"
    clip = root / "clip.h264"
    clip.parent.mkdir()
    clip.write_bytes(b"annex-b-bytes")
    service = CapabilityService(CoreClient(_executable(tmp_path, "vt-source")), AllowedRoots([root]))
    envelope = service.verify_file(VerifyFileRequest(input_file=str(clip), codec=Codec.h264))
    assert envelope.errors[0].code is ErrorCode.CONTRACT_MISMATCH
    assert envelope.result is None
    dumped = json.dumps(envelope.model_dump(mode="json"))
    assert "established" not in dumped
