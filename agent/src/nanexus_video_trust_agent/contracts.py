"""Typed contracts for the read-only Video Trust agent interface.

These models consume the released core JSON schema 0.1. They do not
redefine Media Signing semantics. Product, core-schema, agent-contract,
and capability versions are independent.
"""

from __future__ import annotations

from datetime import datetime, timezone
from enum import Enum
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError, field_validator, model_validator

PRODUCT_VERSION = "0.1.0"
CORE_SCHEMA_VERSION = "0.1"
CONTRACT_VERSION = "0.1"
CAPABILITY_VERSION = "0.1"
CORE_NAME = "video-trust"
VERIFY_FILE_CAPABILITY = "video_trust.verify_file"
ASSESS_VIDEO_INTEGRITY_CAPABILITY = "video_trust.assess_video_integrity"

_CONTROL_CHARACTERS = ("\x00", "\n", "\r")


class Codec(str, Enum):
    h264 = "h264"
    h265 = "h265"


class SignatureIntegrity(str, Enum):
    not_applicable = "not_applicable"
    not_feasible = "not_feasible"
    ok = "ok"
    ok_with_missing_info = "ok_with_missing_info"
    not_ok = "not_ok"
    version_mismatch = "version_mismatch"


class ContinuityStatus(str, Enum):
    not_applicable = "not_applicable"
    intact = "intact"
    missing_info = "missing_info"
    broken = "broken"


class VerificationCompleteness(str, Enum):
    complete = "complete"
    incomplete = "incomplete"
    not_feasible = "not_feasible"


class CertificateStatus(str, Enum):
    not_provided = "not_provided"
    not_feasible = "not_feasible"
    not_ok = "not_ok"
    ok = "ok"
    feasible_without_trusted = "feasible_without_trusted"


class OverallState(str, Enum):
    VALID = "VALID"
    INVALID = "INVALID"
    UNSIGNED = "UNSIGNED"
    NOT_VERIFIABLE = "NOT_VERIFIABLE"
    PARTIAL = "PARTIAL"


class ErrorCode(str, Enum):
    INVALID_REQUEST = "INVALID_REQUEST"
    PATH_NOT_ALLOWED = "PATH_NOT_ALLOWED"
    FILE_NOT_FOUND = "FILE_NOT_FOUND"
    UNSUPPORTED_CODEC = "UNSUPPORTED_CODEC"
    MALFORMED_MEDIA = "MALFORMED_MEDIA"
    INVALID_TRUST_ANCHOR = "INVALID_TRUST_ANCHOR"
    CORE_INPUT_REJECTED = "CORE_INPUT_REJECTED"
    CORE_UNAVAILABLE = "CORE_UNAVAILABLE"
    CORE_EXECUTION_FAILED = "CORE_EXECUTION_FAILED"
    CORE_TIMEOUT = "CORE_TIMEOUT"
    CONTRACT_MISMATCH = "CONTRACT_MISMATCH"


class CapabilityLevel(str, Enum):
    primitive = "primitive"
    domain_semantic = "domain_semantic"


class ExecutionStatus(str, Enum):
    success = "success"
    failed = "failed"


def _reject_path_text(value: str, field_name: str) -> str:
    if value.strip() == "":
        raise ValueError(f"{field_name} must not be empty")
    if any(char in value for char in _CONTROL_CHARACTERS):
        raise ValueError(f"{field_name} must be a single path")
    return value


def _reject_absolute_reference(value: str | None) -> str | None:
    if value is None:
        return None
    if value == "" or value.startswith("/") or value.startswith("~") or "\\" in value:
        raise ValueError("evidence reference must be root-relative")
    parts = value.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError("evidence reference must be root-relative")
    return value


class VerifyFileRequest(BaseModel):
    """File verification request shared by the read-only capabilities."""

    model_config = ConfigDict(extra="forbid")

    input_file: str
    codec: Codec
    trust_anchor: str | None = None

    @field_validator("input_file")
    @classmethod
    def input_file_is_a_single_path(cls, value: str) -> str:
        return _reject_path_text(value, "input_file")

    @field_validator("trust_anchor")
    @classmethod
    def trust_anchor_is_a_single_path(cls, value: str | None) -> str | None:
        if value is None:
            return None
        return _reject_path_text(value, "trust_anchor")


class MediaSigningPresence(BaseModel):
    model_config = ConfigDict(extra="ignore")

    present: bool


class CoreFinding(BaseModel):
    model_config = ConfigDict(extra="ignore")

    code: str
    message: str


class CoreVersions(BaseModel):
    model_config = ConfigDict(extra="ignore")

    signing: str | None = None
    validation: str | None = None


class VendorInfo(BaseModel):
    model_config = ConfigDict(extra="ignore")

    manufacturer: str


class VerificationDocument(BaseModel):
    """Closed reading of core JSON schema 0.1.

    Additional unknown fields are ignored. Unknown enum values and a
    source_authenticity value other than not_established fail validation.
    """

    model_config = ConfigDict(extra="ignore")

    schema_version: Literal["0.1"]
    codec: Codec
    media_signing: MediaSigningPresence
    signature_integrity: SignatureIntegrity
    continuity: ContinuityStatus
    verification_completeness: VerificationCompleteness
    certificate_status: CertificateStatus
    source_authenticity: Literal["not_established"]
    public_key_has_changed: bool
    overall: OverallState
    vendor: VendorInfo | None = None
    versions: CoreVersions
    findings: list[CoreFinding] = Field(default_factory=list)


class CapabilityError(BaseModel):
    model_config = ConfigDict(extra="forbid")

    code: ErrorCode
    message: str
    retryable: bool = False

    @model_validator(mode="after")
    def retryable_matches_timeout(self) -> CapabilityError:
        if self.retryable != (self.code is ErrorCode.CORE_TIMEOUT):
            raise ValueError("retryable is true only for CORE_TIMEOUT")
        if self.message.strip() == "":
            raise ValueError("error message must not be empty")
        return self


class Limitation(BaseModel):
    model_config = ConfigDict(extra="forbid")

    code: str
    message: str


class IntegrityAssessment(str, Enum):
    signing_integrity_intact = "signing_integrity_intact"
    signing_integrity_failed = "signing_integrity_failed"
    no_media_signing = "no_media_signing"
    partial_evidence = "partial_evidence"
    not_verifiable = "not_verifiable"


class TrustAssessment(str, Enum):
    certificate_trust_not_evaluated = "certificate_trust_not_evaluated"
    signing_key_validated_against_trust_anchor = "signing_key_validated_against_trust_anchor"
    signing_key_not_trusted = "signing_key_not_trusted"
    certificate_provenance_not_feasible = "certificate_provenance_not_feasible"
    certificate_provenance_without_trusted_anchor = "certificate_provenance_without_trusted_anchor"


class FollowUpHint(BaseModel):
    model_config = ConfigDict(extra="forbid")

    code: str
    message: str


class IntegrityAssessmentResult(BaseModel):
    """Deterministic L2 view of one verification document.

    `verification` is that document. It is not a second core execution.
    """

    model_config = ConfigDict(extra="forbid")

    verification: VerificationDocument
    integrity_assessment: IntegrityAssessment
    trust_assessment: TrustAssessment
    follow_up_hints: list[FollowUpHint]


class Evidence(BaseModel):
    """Execution evidence. SHA-256 identifies bytes read by this layer.

    It is not an ONVIF signature and it is not core provenance.
    """

    model_config = ConfigDict(extra="forbid")

    contract_version: Literal["0.1"] = CONTRACT_VERSION
    capability: str
    capability_version: Literal["0.1"] = CAPABILITY_VERSION
    invocation_id: str
    executed_at: datetime
    codec: Codec | None = None
    input_sha256: str | None = None
    trust_anchor_sha256: str | None = None
    input_reference: str | None = None
    trust_anchor_reference: str | None = None
    core_name: Literal["video-trust"] = CORE_NAME
    core_version: str | None = None
    core_schema_version: str | None = None
    core_exit_code: int | None = None

    @field_validator("input_reference", "trust_anchor_reference")
    @classmethod
    def references_are_root_relative(cls, value: str | None) -> str | None:
        return _reject_absolute_reference(value)

    @field_validator("input_sha256", "trust_anchor_sha256")
    @classmethod
    def hashes_are_lowercase_sha256(cls, value: str | None) -> str | None:
        if value is None:
            return None
        if len(value) != 64 or any(char not in "0123456789abcdef" for char in value):
            raise ValueError("SHA-256 evidence must be lowercase hex")
        return value

    @model_validator(mode="after")
    def executed_at_is_utc(self) -> Evidence:
        if self.executed_at.tzinfo is None or self.executed_at.utcoffset() != timezone.utc.utcoffset(
            self.executed_at
        ):
            raise ValueError("executed_at must be UTC")
        return self


class CapabilityEnvelope(BaseModel):
    model_config = ConfigDict(extra="forbid")

    contract_version: Literal["0.1"] = CONTRACT_VERSION
    capability: str
    capability_version: Literal["0.1"] = CAPABILITY_VERSION
    capability_level: CapabilityLevel
    safety_level: Literal["read"] = "read"
    execution_status: ExecutionStatus
    result: VerificationDocument | IntegrityAssessmentResult | None = None
    evidence: Evidence
    limitations: list[Limitation] = Field(default_factory=list)
    errors: list[CapabilityError] = Field(default_factory=list)

    @model_validator(mode="after")
    def status_matches_payload(self) -> CapabilityEnvelope:
        if self.execution_status is ExecutionStatus.success:
            if self.result is None or self.errors:
                raise ValueError("success requires a result and no errors")
        elif self.result is not None or len(self.errors) != 1:
            raise ValueError("failure requires a null result and one error")
        return self


def parse_verification_document(payload: object) -> VerificationDocument:
    """Validate a core JSON value. Raises pydantic.ValidationError on mismatch."""

    return VerificationDocument.model_validate(payload)


def classify_request_validation(exc: ValidationError) -> ErrorCode:
    """Map a request validation error onto the agent error enum.

    An explicit codec outside h264/h265 is UNSUPPORTED_CODEC. A missing
    codec or any other request problem is INVALID_REQUEST.
    """

    for error in exc.errors():
        if error.get("loc") == ("codec",) and error.get("type", "").startswith("enum"):
            return ErrorCode.UNSUPPORTED_CODEC
        if error.get("loc") == ("codec",) and error.get("input") not in (None, ""):
            if error.get("type") not in {"missing"}:
                return ErrorCode.UNSUPPORTED_CODEC
    return ErrorCode.INVALID_REQUEST
