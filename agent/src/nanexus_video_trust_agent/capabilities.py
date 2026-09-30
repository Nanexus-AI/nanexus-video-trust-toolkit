"""Read-only capability functions over one video-trust verification.

`verify_file` and `assess_video_integrity` both call `CoreClient.verify`
once. The semantic capability maps that document. It does not start a
second core process and it does not call an agent runtime.
"""

from __future__ import annotations

from collections.abc import Mapping
from datetime import datetime, timezone
from typing import Any
from uuid import uuid4

from pydantic import ValidationError

from nanexus_video_trust_agent.contracts import (
    ASSESS_VIDEO_INTEGRITY_CAPABILITY,
    COMPARE_PRESERVATION_CAPABILITY,
    VERIFY_FILE_CAPABILITY,
    CapabilityEnvelope,
    CapabilityLevel,
    CertificateStatus,
    Codec,
    ErrorCode,
    Evidence,
    ExecutionStatus,
    FollowUpHint,
    IntegrityAssessment,
    IntegrityAssessmentResult,
    Limitation,
    OverallState,
    TrustAssessment,
    VerificationDocument,
    VerifyFileRequest,
    classify_request_validation,
)
from nanexus_video_trust_agent.core_client import CoreClient, CoreInvocation, _error
from nanexus_video_trust_agent.preservation_contracts import (
    ComparePreservationRequest,
    PreservationCapabilityEnvelope,
    PreservationEvidence,
)
from nanexus_video_trust_agent.policy import AllowedRoots

_INTEGRITY: dict[OverallState, IntegrityAssessment] = {
    OverallState.VALID: IntegrityAssessment.signing_integrity_intact,
    OverallState.INVALID: IntegrityAssessment.signing_integrity_failed,
    OverallState.UNSIGNED: IntegrityAssessment.no_media_signing,
    OverallState.PARTIAL: IntegrityAssessment.partial_evidence,
    OverallState.NOT_VERIFIABLE: IntegrityAssessment.not_verifiable,
}

_TRUST: dict[CertificateStatus, TrustAssessment] = {
    CertificateStatus.not_provided: TrustAssessment.certificate_trust_not_evaluated,
    CertificateStatus.ok: TrustAssessment.signing_key_validated_against_trust_anchor,
    CertificateStatus.not_ok: TrustAssessment.signing_key_not_trusted,
    CertificateStatus.not_feasible: TrustAssessment.certificate_provenance_not_feasible,
    CertificateStatus.feasible_without_trusted: (
        TrustAssessment.certificate_provenance_without_trusted_anchor
    ),
}

SOURCE_AUTHENTICITY_NOT_ESTABLISHED = Limitation(
    code="SOURCE_AUTHENTICITY_NOT_ESTABLISHED",
    message=(
        "Media Signing verification does not establish camera identity, "
        "source authenticity, or that the depicted event is real."
    ),
)
TRUST_ANCHOR_NOT_SUPPLIED = Limitation(
    code="TRUST_ANCHOR_NOT_SUPPLIED",
    message="Certificate-chain trust was not evaluated because no trust anchor was supplied.",
)
VALID_IS_NOT_CERTIFICATE_TRUST = Limitation(
    code="VALID_IS_NOT_CERTIFICATE_TRUST",
    message=(
        "overall VALID means the signature-integrity and completeness rules for VALID "
        "were met. It does not mean the signing certificate is trusted."
    ),
)
CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY = Limitation(
    code="CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY",
    message=(
        "certificate_status ok means the signing public key validated against the "
        "supplied trust anchor. It does not establish source authenticity."
    ),
)
NO_MEDIA_SIGNING_EVIDENCE = Limitation(
    code="NO_MEDIA_SIGNING_EVIDENCE",
    message="No ONVIF Media Signing evidence was detected in this file.",
)

REVERIFY_WITH_TRUST_ANCHOR = FollowUpHint(
    code="REVERIFY_WITH_TRUST_ANCHOR",
    message="Re-run verification with a trust_anchor PEM if certificate-chain evaluation is required.",
)
SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE = FollowUpHint(
    code="SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE",
    message=(
        "No Media Signing evidence was detected. This Agent interface cannot sign or modify the file."
    ),
)
INTEGRITY_FAILURE = FollowUpHint(
    code="INTEGRITY_FAILURE",
    message="Signature integrity or continuity failed. Do not treat the file as integrity-intact.",
)
DO_NOT_UPGRADE_PARTIAL = FollowUpHint(
    code="DO_NOT_UPGRADE_PARTIAL",
    message=(
        "Evidence is incomplete or not verifiable. Do not upgrade the result to VALID "
        "or certificate-trusted."
    ),
)


def integrity_assessment_for(overall: OverallState) -> IntegrityAssessment:
    return _INTEGRITY[overall]


def trust_assessment_for(certificate_status: CertificateStatus) -> TrustAssessment:
    return _TRUST[certificate_status]


def limitations_for(document: VerificationDocument, *, semantic: bool) -> list[Limitation]:
    """Standing and conditional limitations. Order is stable."""

    items = [SOURCE_AUTHENTICITY_NOT_ESTABLISHED]
    if document.certificate_status is CertificateStatus.not_provided:
        items.append(TRUST_ANCHOR_NOT_SUPPLIED)
    if (
        document.overall is OverallState.VALID
        and document.certificate_status is not CertificateStatus.ok
    ):
        items.append(VALID_IS_NOT_CERTIFICATE_TRUST)
    if semantic and document.certificate_status is CertificateStatus.ok:
        items.append(CERTIFICATE_OK_IS_NOT_SOURCE_AUTHENTICITY)
    if semantic and not document.media_signing.present:
        items.append(NO_MEDIA_SIGNING_EVIDENCE)
    return items


def follow_up_hints_for(document: VerificationDocument) -> list[FollowUpHint]:
    """Closed L2 catalog. verify_file does not use this list."""

    hints: list[FollowUpHint] = []
    if document.certificate_status is CertificateStatus.not_provided:
        hints.append(REVERIFY_WITH_TRUST_ANCHOR)
    if document.overall is OverallState.UNSIGNED:
        hints.append(SIGNING_NOT_AVAILABLE_ON_AGENT_INTERFACE)
    if document.overall is OverallState.INVALID:
        hints.append(INTEGRITY_FAILURE)
    if document.overall in (OverallState.PARTIAL, OverallState.NOT_VERIFIABLE):
        hints.append(DO_NOT_UPGRADE_PARTIAL)
    return hints


class CapabilityService:
    """Primitive and semantic verification. Neither imports an agent runtime."""

    def __init__(self, client: CoreClient, policy: AllowedRoots) -> None:
        self._client = client
        self._policy = policy

    def verify_file(self, request: VerifyFileRequest | Mapping[str, Any]) -> CapabilityEnvelope:
        return self._invoke(
            request,
            capability=VERIFY_FILE_CAPABILITY,
            level=CapabilityLevel.primitive,
            semantic=False,
        )

    def assess_video_integrity(
        self, request: VerifyFileRequest | Mapping[str, Any]
    ) -> CapabilityEnvelope:
        return self._invoke(
            request,
            capability=ASSESS_VIDEO_INTEGRITY_CAPABILITY,
            level=CapabilityLevel.domain_semantic,
            semantic=True,
        )

    def compare_preservation(
        self, request: ComparePreservationRequest | Mapping[str, Any]
    ) -> PreservationCapabilityEnvelope:
        try:
            parsed = request if isinstance(request, ComparePreservationRequest) else ComparePreservationRequest.model_validate(request)
        except ValidationError as exc:
            code = classify_request_validation(exc)
            codec = _codec_if_known(exc)
            return PreservationCapabilityEnvelope(
                execution_status=ExecutionStatus.failed,
                result=None,
                evidence=PreservationEvidence(
                    invocation_id=str(uuid4()), executed_at=datetime.now(timezone.utc), codec=codec
                ),
                errors=[_error(code)],
            )
        invocation = self._client.compare_preservation(request=parsed, policy=self._policy)
        if invocation.error is not None or invocation.document is None:
            return PreservationCapabilityEnvelope(
                execution_status=ExecutionStatus.failed, result=None, evidence=invocation.evidence,
                errors=[invocation.error or _error(ErrorCode.CONTRACT_MISMATCH)],
            )
        return PreservationCapabilityEnvelope(
            execution_status=ExecutionStatus.success,
            result=invocation.document,
            evidence=invocation.evidence,
            limitations=[],
            errors=[],
        )

    def _invoke(
        self,
        request: VerifyFileRequest | Mapping[str, Any],
        *,
        capability: str,
        level: CapabilityLevel,
        semantic: bool,
    ) -> CapabilityEnvelope:
        try:
            parsed = request if isinstance(request, VerifyFileRequest) else VerifyFileRequest.model_validate(
                request
            )
        except ValidationError as exc:
            return self._request_failure(exc, capability=capability, level=level)
        invocation = self._execute(parsed, capability=capability)
        return self._present(invocation, capability=capability, level=level, semantic=semantic)

    def _execute(self, request: VerifyFileRequest, *, capability: str) -> CoreInvocation:
        return self._client.verify(
            codec=request.codec,
            input_file=request.input_file,
            policy=self._policy,
            trust_anchor=request.trust_anchor,
            capability=capability,
        )

    def _present(
        self,
        invocation: CoreInvocation,
        *,
        capability: str,
        level: CapabilityLevel,
        semantic: bool,
    ) -> CapabilityEnvelope:
        if invocation.error is not None or invocation.document is None:
            error = invocation.error or _error(ErrorCode.CONTRACT_MISMATCH)
            return CapabilityEnvelope(
                capability=capability,
                capability_level=level,
                execution_status=ExecutionStatus.failed,
                result=None,
                evidence=invocation.evidence,
                limitations=[],
                errors=[error],
            )
        document = invocation.document
        try:
            result: VerificationDocument | IntegrityAssessmentResult
            if semantic:
                result = IntegrityAssessmentResult(
                    verification=document,
                    integrity_assessment=integrity_assessment_for(document.overall),
                    trust_assessment=trust_assessment_for(document.certificate_status),
                    follow_up_hints=follow_up_hints_for(document),
                )
            else:
                result = document
        except KeyError:
            return CapabilityEnvelope(
                capability=capability,
                capability_level=level,
                execution_status=ExecutionStatus.failed,
                result=None,
                evidence=invocation.evidence,
                limitations=[],
                errors=[_error(ErrorCode.CONTRACT_MISMATCH)],
            )
        return CapabilityEnvelope(
            capability=capability,
            capability_level=level,
            execution_status=ExecutionStatus.success,
            result=result,
            evidence=invocation.evidence,
            limitations=limitations_for(document, semantic=semantic),
            errors=[],
        )

    def _request_failure(
        self,
        exc: ValidationError,
        *,
        capability: str,
        level: CapabilityLevel,
    ) -> CapabilityEnvelope:
        code = classify_request_validation(exc)
        codec = _codec_if_known(exc)
        evidence = Evidence(
            capability=capability,
            invocation_id=str(uuid4()),
            executed_at=datetime.now(timezone.utc),
            codec=codec,
        )
        return CapabilityEnvelope(
            capability=capability,
            capability_level=level,
            execution_status=ExecutionStatus.failed,
            result=None,
            evidence=evidence,
            limitations=[],
            errors=[_error(code)],
        )


def _codec_if_known(exc: ValidationError) -> Codec | None:
    allowed = {item.value for item in Codec}
    for error in exc.errors():
        value = error.get("input")
        if error.get("loc") == ("codec",) and isinstance(value, str) and value in allowed:
            return Codec(value)
    return None
