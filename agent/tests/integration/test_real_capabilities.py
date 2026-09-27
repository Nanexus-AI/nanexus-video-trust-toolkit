"""Real video-trust 0.1.0 matrix for both codecs.

Fixture preparation uses ffmpeg, OpenSSL, and the human CLI. The
capability service itself only calls verify.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import pytest

from nanexus_video_trust_agent.capabilities import (
    DO_NOT_UPGRADE_PARTIAL,
    INTEGRITY_FAILURE,
    SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE,
    SOURCE_AUTHENTICITY_NOT_ESTABLISHED,
    TRUST_ANCHOR_NOT_SUPPLIED,
    VALID_IS_NOT_CERTIFICATE_TRUST,
    CapabilityService,
)
from nanexus_video_trust_agent.contracts import (
    CapabilityLevel,
    CertificateStatus,
    Codec,
    ErrorCode,
    ExecutionStatus,
    IntegrityAssessment,
    IntegrityAssessmentResult,
    OverallState,
    TrustAssessment,
    VerificationDocument,
    VerifyFileRequest,
)
from nanexus_video_trust_agent.core_client import CoreClient
from nanexus_video_trust_agent.policy import AllowedRoots

BINARY = Path(__file__).resolve().parents[3] / "build" / "nanexus" / "video-trust"
pytestmark = pytest.mark.skipif(
    not BINARY.is_file() or shutil.which("ffmpeg") is None or shutil.which("openssl") is None,
    reason="video-trust, ffmpeg, and openssl are required for the real-core matrix",
)


def _run(argv: list[str]) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(argv, capture_output=True, check=False)
    if completed.returncode != 0:
        raise AssertionError(
            f"{argv[0]} failed ({completed.returncode}): {completed.stderr.decode(errors='replace')}"
        )
    return completed


def _generate(root: Path) -> None:
    pki = root / "pki"
    pki.mkdir()
    _run(
        [
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-days", "3650", "-nodes",
            "-keyout", str(pki / "ca.key.pem"), "-out", str(pki / "ca.pem"),
            "-subj", "/CN=Nanexus Test CA",
        ]
    )
    _run(
        [
            "openssl", "req", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(pki / "signer.key.pem"), "-out", str(pki / "signer.csr.pem"),
            "-subj", "/CN=Nanexus Test Signer",
        ]
    )
    _run(
        [
            "openssl", "x509", "-req", "-in", str(pki / "signer.csr.pem"),
            "-CA", str(pki / "ca.pem"), "-CAkey", str(pki / "ca.key.pem"),
            "-CAcreateserial", "-out", str(pki / "signer.cert.pem"),
            "-days", "3650", "-sha256",
        ]
    )
    (pki / "signer-chain.pem").write_bytes(
        (pki / "signer.cert.pem").read_bytes() + (pki / "ca.pem").read_bytes()
    )
    for codec, encoder, container, ext, extra in (
        ("h264", "libx264", "h264", "h264", ["-bf", "0"]),
        ("h265", "libx265", "hevc", "h265", []),
    ):
        folder = root / codec
        folder.mkdir()
        unsigned = folder / f"unsigned.{ext}"
        signed = folder / f"signed.{ext}"
        invalid = folder / f"invalid.{ext}"
        partial = folder / f"partial.{ext}"
        _run(
            [
                "ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                "-f", "lavfi", "-i", "color=c=black:s=64x64:d=1",
                "-c:v", encoder, "-pix_fmt", "yuv420p", "-g", "15", *extra, "-an",
                "-f", container, str(unsigned),
            ]
        )
        _run(
            [
                str(BINARY), "sign", "--codec", codec,
                "--key", str(pki / "signer.key.pem"),
                "--cert", str(pki / "signer-chain.pem"),
                "-o", str(signed), str(unsigned),
            ]
        )
        _run(
            [
                str(BINARY), "tamper", "--codec", codec,
                "--operation", "corrupt-vcl", "-o", str(invalid), str(signed),
            ]
        )
        _run(
            [
                str(BINARY), "tamper", "--codec", codec,
                "--operation", "truncate", "--count", "3",
                "-o", str(partial), str(signed),
            ]
        )
    (root / "not-media.bin").write_bytes(b"this is not annex-b")


@pytest.fixture(scope="module")
def lab(tmp_path_factory: pytest.TempPathFactory) -> tuple[Path, CapabilityService]:
    root = tmp_path_factory.mktemp("synthetic-media")
    _generate(root)
    service = CapabilityService(CoreClient(BINARY), AllowedRoots([root]))
    return root, service


def _relative(root: Path, path: Path) -> str:
    return path.relative_to(root).as_posix()


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _cli_document(codec: str, path: Path, trust_anchor: Path | None) -> tuple[int, dict]:
    argv = [str(BINARY), "verify", "--codec", codec, "--json"]
    if trust_anchor is not None:
        argv.extend(["--ca", str(trust_anchor)])
    argv.append(str(path))
    completed = subprocess.run(argv, capture_output=True, check=False)
    return completed.returncode, json.loads(completed.stdout)


@pytest.mark.parametrize("codec", ["h264", "h265"])
@pytest.mark.parametrize(
    ("label", "name", "use_anchor", "overall", "exit_code"),
    [
        ("unsigned", "unsigned", False, OverallState.UNSIGNED, 4),
        ("signed_with_anchor", "signed", True, OverallState.VALID, 0),
        ("signed_without_anchor", "signed", False, OverallState.VALID, 0),
        ("corrupted", "invalid", True, OverallState.INVALID, 1),
        ("truncated", "partial", True, OverallState.PARTIAL, 4),
    ],
)
def test_real_core_matrix(
    lab: tuple[Path, CapabilityService],
    codec: str,
    label: str,
    name: str,
    use_anchor: bool,
    overall: OverallState,
    exit_code: int,
) -> None:
    root, service = lab
    ext = "h264" if codec == "h264" else "h265"
    media = root / codec / f"{name}.{ext}"
    anchor = root / "pki" / "ca.pem" if use_anchor else None
    cli_exit, cli_payload = _cli_document(codec, media, anchor)
    request = VerifyFileRequest(
        input_file=str(media),
        codec=Codec(codec),
        trust_anchor=None if anchor is None else str(anchor),
    )
    primitive = service.verify_file(request)
    semantic = service.assess_video_integrity(request)

    assert cli_exit == exit_code
    assert primitive.execution_status is ExecutionStatus.success
    assert primitive.capability_level is CapabilityLevel.primitive
    assert primitive.errors == []
    assert isinstance(primitive.result, VerificationDocument)
    assert primitive.result.model_dump(mode="json") == VerificationDocument.model_validate(
        cli_payload
    ).model_dump(mode="json")
    assert primitive.result.overall is overall
    assert primitive.result.source_authenticity == "not_established"
    assert semantic.execution_status is ExecutionStatus.success
    assert isinstance(semantic.result, IntegrityAssessmentResult)
    assert semantic.result.verification == primitive.result
    assert semantic.evidence.invocation_id != primitive.evidence.invocation_id
    assert semantic.evidence.core_exit_code == exit_code
    assert semantic.evidence.core_version == "0.1.0"
    assert semantic.evidence.core_schema_version == "0.1"
    assert semantic.evidence.core_name == "video-trust"
    assert semantic.evidence.input_reference == _relative(root, media)
    assert semantic.evidence.input_sha256 == _sha256(media)
    assert semantic.evidence.executed_at.utcoffset() is not None
    assert semantic.evidence.executed_at.utcoffset().total_seconds() == 0
    if anchor is None:
        assert semantic.evidence.trust_anchor_reference is None
        assert semantic.evidence.trust_anchor_sha256 is None
    else:
        assert semantic.evidence.trust_anchor_reference == "pki/ca.pem"
        assert semantic.evidence.trust_anchor_sha256 == _sha256(anchor)

    dumped = json.dumps({"l1": primitive.model_dump(mode="json"), "l2": semantic.model_dump(mode="json")})
    assert str(root) not in dumped
    assert str(media) not in dumped
    assert "BEGIN CERTIFICATE" not in dumped
    assert "PRIVATE KEY" not in dumped

    codes = {item.code for item in semantic.limitations}
    assert "SOURCE_AUTHENTICITY_NOT_ESTABLISHED" in codes
    assert semantic.limitations[0] == SOURCE_AUTHENTICITY_NOT_ESTABLISHED
    if overall is OverallState.UNSIGNED:
        assert semantic.result.integrity_assessment is IntegrityAssessment.no_media_signing
        assert semantic.result.verification.media_signing.present is False
        assert SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE in semantic.result.follow_up_hints
        assert "NO_MEDIA_SIGNING_EVIDENCE" in codes
    if overall is OverallState.INVALID:
        assert semantic.result.integrity_assessment is IntegrityAssessment.signing_integrity_failed
        assert INTEGRITY_FAILURE in semantic.result.follow_up_hints
    if overall is OverallState.PARTIAL:
        assert semantic.result.integrity_assessment is IntegrityAssessment.partial_evidence
        assert DO_NOT_UPGRADE_PARTIAL in semantic.result.follow_up_hints
    if overall is OverallState.VALID and use_anchor:
        assert semantic.result.integrity_assessment is IntegrityAssessment.signing_integrity_intact
        assert semantic.result.verification.certificate_status is CertificateStatus.ok
        assert (
            semantic.result.trust_assessment
            is TrustAssessment.signing_key_validated_against_trust_anchor
        )
        assert "CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY" in codes
        assert TRUST_ANCHOR_NOT_SUPPLIED not in semantic.limitations
    if overall is OverallState.VALID and not use_anchor:
        assert semantic.result.integrity_assessment is IntegrityAssessment.signing_integrity_intact
        assert semantic.result.verification.certificate_status is CertificateStatus.not_provided
        assert (
            semantic.result.trust_assessment is TrustAssessment.certificate_trust_not_evaluated
        )
        assert TRUST_ANCHOR_NOT_SUPPLIED in semantic.limitations
        assert VALID_IS_NOT_CERTIFICATE_TRUST in semantic.limitations
        assert SOURCE_AUTHENTICITY_NOT_ESTABLISHED in semantic.limitations
        assert "REVERIFY_WITH_TRUST_ANCHOR" in {hint.code for hint in semantic.result.follow_up_hints}

    print(
        "MATRIX"
        f" {codec} {label} overall={primitive.result.overall.value}"
        f" exit={primitive.evidence.core_exit_code}"
        f" cert={primitive.result.certificate_status.value}"
        f" integrity={semantic.result.integrity_assessment.value}"
        f" trust={semantic.result.trust_assessment.value}"
        f" limitations={','.join(item.code for item in semantic.limitations)}"
        f" hints={','.join(hint.code for hint in semantic.result.follow_up_hints)}"
    )


def test_real_core_rejects_non_media_without_stderr(lab: tuple[Path, CapabilityService]) -> None:
    root, service = lab
    media = root / "not-media.bin"
    envelope = service.verify_file(
        VerifyFileRequest(input_file=str(media), codec=Codec.h264)
    )
    assert envelope.execution_status is ExecutionStatus.failed
    assert envelope.result is None
    assert envelope.errors[0].code is ErrorCode.CORE_INPUT_REJECTED
    assert envelope.errors[0].code is not ErrorCode.MALFORMED_MEDIA
    dumped = json.dumps(envelope.model_dump(mode="json"))
    assert "Annex-B" not in dumped
    assert "start code" not in dumped
    assert str(root) not in dumped
    assert envelope.evidence.input_reference == "not-media.bin"
    assert envelope.evidence.input_sha256 == _sha256(media)
