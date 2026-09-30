"""Typed contract for the frozen preservation assessment schema 0.1."""

from __future__ import annotations

from enum import Enum
from datetime import datetime, timezone
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

from nanexus_video_trust_agent.contracts import (
    CertificateStatus,
    CapabilityError,
    CapabilityLevel,
    Codec,
    ContinuityStatus,
    OverallState,
    ExecutionStatus,
    Limitation,
    SignatureIntegrity,
    VerificationCompleteness,
    _reject_path_text,
    _reject_absolute_reference,
)


class ClosedModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class TransformationKind(str, Enum):
    transparent = "transparent"
    remux = "remux"
    clip = "clip"
    segment = "segment"
    concatenate = "concatenate"
    transcode = "transcode"
    metadata_change = "metadata-change"
    timestamp_rewrite = "timestamp-rewrite"
    proprietary_or_unknown = "proprietary-or-unknown"


class ComparePreservationRequest(ClosedModel):
    before_path: str
    after_path: str
    codec: Codec
    before_ca_path: str | None = None
    after_ca_path: str | None = None
    transformation: TransformationKind | None = None
    pipeline_id: str | None = Field(default=None, min_length=1, max_length=128)

    @field_validator("before_path", "after_path", "before_ca_path", "after_ca_path")
    @classmethod
    def paths_are_single_paths(cls, value: str | None, info) -> str | None:
        if value is None:
            return None
        return _reject_path_text(value, info.field_name)

    @field_validator("pipeline_id")
    @classmethod
    def pipeline_id_is_plain_text(cls, value: str | None) -> str | None:
        if value is not None and any(char in value for char in ("\x00", "\n", "\r")):
            raise ValueError("pipeline_id must be bounded single-line text")
        return value


class HashValue(ClosedModel):
    algorithm: Literal["sha256"]
    value: str = Field(pattern=r"^[0-9a-f]{64}$")


class MediaSigningState(str, Enum):
    not_detected = "not_detected"
    detected = "detected"


class VerificationSnapshot(ClosedModel):
    media_signing: MediaSigningState
    signature_integrity: SignatureIntegrity
    continuity: ContinuityStatus
    verification_completeness: VerificationCompleteness
    certificate_status: CertificateStatus
    source_authenticity: Literal["not_established"]
    public_key_has_changed: bool
    overall: OverallState


class InspectionSnapshot(ClosedModel):
    pending_nalus: int = Field(ge=0)
    pending_frames: int = Field(ge=0)
    pending_hashable_nalus: int | None = Field(default=None, ge=0)


class ArtifactSnapshot(ClosedModel):
    codec: Codec
    byte_size: int = Field(ge=0)
    sha256: HashValue
    nal_count: int = Field(ge=0)
    signing_sei_count: int = Field(ge=0)
    verification: VerificationSnapshot
    inspection: InspectionSnapshot


class TransformationContext(ClosedModel):
    kind: Literal[
        "unspecified", "transparent", "remux", "clip", "segment", "concatenate",
        "transcode", "metadata_change", "timestamp_rewrite", "proprietary_or_unknown",
    ]
    label: str | None
    pipeline_id: str | None
    tool_version: str | None
    log_digest: str | None
    trust: Literal["caller_declared_untrusted"]


class ArtifactIdentity(ClosedModel):
    byte_relation: Literal["identical", "different", "indeterminate"]


EvidenceState = Literal["no", "yes", "indeterminate"]
ChangeKind = Literal["unchanged", "improved", "degraded", "changed", "indeterminate"]


class RoleCounts(ClosedModel):
    vcl: int = Field(ge=0)
    sei: int = Field(ge=0)
    signing_sei: int = Field(ge=0)
    parameter_set: int = Field(ge=0)
    other: int = Field(ge=0)


class UnmatchedSummary(ClosedModel):
    total_count: int = Field(ge=0)
    sample_count: int = Field(ge=0)
    samples_truncated: bool
    roles: RoleCounts


class SigningMetadata(ClosedModel):
    relation: Literal[
        "equivalent", "retained_for_subset", "partially_retained", "missing_after",
        "replaced_or_unmatched", "not_applicable", "indeterminate",
    ]
    before_count: int = Field(ge=0)
    after_count: int = Field(ge=0)
    matched_payload_count: int = Field(ge=0)
    missing_from_after_count: int = Field(ge=0)
    unmatched_after_count: int = Field(ge=0)
    correlation_complete: EvidenceState
    before_classification_complete: bool
    after_classification_complete: bool


class NalRange(ClosedModel):
    start_nal_index: int = Field(ge=0)
    end_nal_index: int = Field(ge=0)


class Correlation(ClosedModel):
    quality: Literal["complete", "ambiguous", "resource_bounded", "incomplete"]
    stream_relation: Literal["equivalent", "ordered_subset", "structurally_changed", "indeterminate"]
    sequence_equivalent: EvidenceState
    after_is_ordered_subsequence: EvidenceState
    unique_subsequence_alignment: EvidenceState
    reordered: EvidenceState
    duplicated_after: EvidenceState
    normalized_payload_matches: int = Field(ge=0)
    unique_before_range: NalRange | None
    unmatched_before: UnmatchedSummary
    unmatched_after: UnmatchedSummary
    diagnostic_detail_bounded: bool
    signing_metadata: SigningMetadata


class Coverage(ClosedModel):
    state: Literal["full", "subset", "unknown"]


class Applicability(ClosedModel):
    media_signing_preservation: Literal["applicable", "not_applicable", "indeterminate"]


class Preservation(ClosedModel):
    media_signing_evidence: Literal["preserved", "partially_preserved", "not_preserved", "indeterminate"]


class VerificationTransitions(ClosedModel):
    before_overall: OverallState
    after_overall: OverallState
    media_signing: ChangeKind
    signature_integrity: ChangeKind
    continuity: ChangeKind
    verification_completeness: ChangeKind
    certificate: ChangeKind
    public_key_observation: ChangeKind


class InspectionTransitions(ClosedModel):
    pending_nalus: ChangeKind
    pending_frames: ChangeKind
    accumulated_timestamps: ChangeKind
    latest_timestamps: ChangeKind


class Transitions(ClosedModel):
    verification: VerificationTransitions
    inspection: InspectionTransitions


class PreservationFinding(ClosedModel):
    code: Literal[
        "EXACT_ARTIFACT_MATCH", "NORMALIZED_STREAM_EQUIVALENT", "ORDERED_SUBSET_ESTABLISHED",
        "SOURCE_COVERAGE_UNKNOWN", "SIGNING_EVIDENCE_RETAINED",
        "SIGNING_EVIDENCE_PARTIALLY_RETAINED", "SIGNING_EVIDENCE_MISSING",
        "SIGNING_EVIDENCE_UNMATCHED", "SIGNING_CONTEXT_INCOMPLETE", "CORRELATION_AMBIGUOUS",
        "CORRELATION_RESOURCE_BOUNDED", "VERIFICATION_DEGRADED", "PRESERVATION_NOT_APPLICABLE",
    ]
    message: str


class PreservationLimitation(ClosedModel):
    code: Literal[
        "SOURCE_AUTHENTICITY_NOT_ESTABLISHED", "VMS_TRUST_NOT_ESTABLISHED",
        "SUPPLIED_BEFORE_COMPLETENESS_NOT_ESTABLISHED", "CHAIN_OF_CUSTODY_NOT_ESTABLISHED",
        "TRANSFORMATION_CONTEXT_UNTRUSTED", "SUBSET_DOES_NOT_ESTABLISH_FULL_EXPORT",
    ]
    message: str


class PreservationAssessment(ClosedModel):
    document_type: Literal["media_signing_preservation_assessment"]
    schema_version: Literal["0.1"]
    before: ArtifactSnapshot
    after: ArtifactSnapshot
    transformation: TransformationContext
    artifact_identity: ArtifactIdentity
    correlation: Correlation
    coverage: Coverage
    applicability: Applicability
    preservation: Preservation
    transitions: Transitions
    findings: list[PreservationFinding]
    limitations: list[PreservationLimitation]


class PreservationEvidence(ClosedModel):
    contract_version: Literal["0.1"] = "0.1"
    capability: Literal["video_trust.compare_preservation"] = "video_trust.compare_preservation"
    capability_version: Literal["0.1"] = "0.1"
    invocation_id: str
    executed_at: datetime
    codec: Codec | None = None
    before_sha256: str | None = None
    after_sha256: str | None = None
    before_ca_sha256: str | None = None
    after_ca_sha256: str | None = None
    before_reference: str | None = None
    after_reference: str | None = None
    before_ca_reference: str | None = None
    after_ca_reference: str | None = None
    core_name: Literal["video-trust"] = "video-trust"
    core_version: str | None = None
    core_schema_version: str | None = None
    core_exit_code: int | None = None

    @field_validator("before_reference", "after_reference", "before_ca_reference", "after_ca_reference")
    @classmethod
    def references_are_root_relative(cls, value: str | None) -> str | None:
        return _reject_absolute_reference(value)

    @field_validator("before_sha256", "after_sha256", "before_ca_sha256", "after_ca_sha256")
    @classmethod
    def hashes_are_sha256(cls, value: str | None) -> str | None:
        if value is not None and (len(value) != 64 or any(c not in "0123456789abcdef" for c in value)):
            raise ValueError("SHA-256 evidence must be lowercase hex")
        return value

    @field_validator("executed_at")
    @classmethod
    def executed_at_is_utc(cls, value: datetime) -> datetime:
        if value.tzinfo is None or value.utcoffset() != timezone.utc.utcoffset(value):
            raise ValueError("executed_at must be UTC")
        return value


class PreservationCapabilityEnvelope(ClosedModel):
    contract_version: Literal["0.1"] = "0.1"
    capability: Literal["video_trust.compare_preservation"] = "video_trust.compare_preservation"
    capability_version: Literal["0.1"] = "0.1"
    capability_level: Literal[CapabilityLevel.primitive] = CapabilityLevel.primitive
    safety_level: Literal["read"] = "read"
    execution_status: ExecutionStatus
    result: PreservationAssessment | None = None
    evidence: PreservationEvidence
    limitations: list[Limitation] = Field(default_factory=list)
    errors: list[CapabilityError] = Field(default_factory=list)

    @model_validator(mode="after")
    def status_matches_payload(self):
        if self.execution_status is ExecutionStatus.success:
            if self.result is None or self.errors:
                raise ValueError("success requires a result and no errors")
        elif self.result is not None or len(self.errors) != 1:
            raise ValueError("failure requires a null result and one error")
        return self
