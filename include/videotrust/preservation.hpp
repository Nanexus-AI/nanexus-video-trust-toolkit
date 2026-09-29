#pragma once

#include "videotrust/correlation.hpp"
#include "videotrust/inspection.hpp"

#include <optional>
#include <string>
#include <vector>

namespace videotrust {

enum class ArtifactRelation { Identical, Different, Indeterminate };

enum class StreamRelation {
  Equivalent,
  OrderedSubset,
  StructurallyChanged,
  Indeterminate,
};

enum class SourceCoverage { Full, Subset, Unknown };

enum class PreservationApplicability { Applicable, NotApplicable, Indeterminate };

enum class MediaSigningPreservation {
  Preserved,
  PartiallyPreserved,
  NotPreserved,
  Indeterminate,
};

enum class SigningMetadataRelation {
  Equivalent,
  RetainedForSubset,
  PartiallyRetained,
  MissingAfter,
  ReplacedOrUnmatched,
  NotApplicable,
  Indeterminate,
};

enum class ChangeKind { Unchanged, Improved, Degraded, Changed, Indeterminate };

enum class CorrelationQuality { Complete, Ambiguous, ResourceBounded, Incomplete };

enum class TransformationKind {
  Unspecified,
  Transparent,
  Remux,
  Clip,
  Segment,
  Concatenate,
  Transcode,
  MetadataChange,
  TimestampRewrite,
  ProprietaryOrUnknown,
};

/// Caller-declared context only. These values never drive preservation.
struct TransformationContext {
  TransformationKind kind{TransformationKind::Unspecified};
  std::optional<std::string> label;
  std::optional<std::string> pipeline_id;
  std::optional<std::string> tool_version;
  std::optional<std::string> log_digest;
};

struct VerificationTransitions {
  OverallState before_overall{OverallState::NotVerifiable};
  OverallState after_overall{OverallState::NotVerifiable};
  ChangeKind media_signing{ChangeKind::Indeterminate};
  ChangeKind signature_integrity{ChangeKind::Indeterminate};
  ChangeKind continuity{ChangeKind::Indeterminate};
  ChangeKind completeness{ChangeKind::Indeterminate};
  ChangeKind certificate{ChangeKind::Indeterminate};
  ChangeKind public_key_observation{ChangeKind::Indeterminate};
};

struct InspectionTransitions {
  ChangeKind pending_nalus{ChangeKind::Indeterminate};
  ChangeKind pending_frames{ChangeKind::Indeterminate};
  ChangeKind accumulated_timestamps{ChangeKind::Indeterminate};
  ChangeKind latest_timestamps{ChangeKind::Indeterminate};
};

enum class PreservationFindingCode {
  ExactArtifactMatch,
  NormalizedStreamEquivalent,
  OrderedSubsetEstablished,
  SourceCoverageUnknown,
  SigningEvidenceRetained,
  SigningEvidencePartiallyRetained,
  SigningEvidenceMissing,
  SigningEvidenceUnmatched,
  SigningContextIncomplete,
  CorrelationAmbiguous,
  CorrelationResourceBounded,
  VerificationDegraded,
  PreservationNotApplicable,
};

struct PreservationFinding {
  PreservationFindingCode code;
  std::string message;
};

enum class PreservationLimitationCode {
  SourceAuthenticityNotEstablished,
  VmsTrustNotEstablished,
  SuppliedBeforeCompletenessNotEstablished,
  ChainOfCustodyNotEstablished,
  TransformationContextUntrusted,
  SubsetDoesNotEstablishFullExport,
};

struct PreservationLimitation {
  PreservationLimitationCode code;
  std::string message;
};

/// Deterministic M3-B interpretation. The composed M1/M2 snapshots and M3-A
/// evidence retain their original meanings and are not rewritten here.
struct PreservationAssessment {
  InspectionResult before;
  InspectionResult after;
  MediaCorrelationEvidence correlation;
  TransformationContext transformation;
  ArtifactRelation artifact_relation{ArtifactRelation::Indeterminate};
  StreamRelation stream_relation{StreamRelation::Indeterminate};
  SourceCoverage source_coverage{SourceCoverage::Unknown};
  PreservationApplicability applicability{
      PreservationApplicability::Indeterminate};
  MediaSigningPreservation media_signing_preservation{
      MediaSigningPreservation::Indeterminate};
  SigningMetadataRelation signing_metadata_relation{
      SigningMetadataRelation::Indeterminate};
  CorrelationQuality correlation_quality{CorrelationQuality::Incomplete};
  VerificationTransitions verification_transitions;
  InspectionTransitions inspection_transitions;
  std::vector<PreservationFinding> findings;
  std::vector<PreservationLimitation> limitations;
};

PreservationAssessment DerivePreservationAssessment(
    const InspectionResult& before,
    const InspectionResult& after,
    const MediaCorrelationEvidence& correlation,
    const TransformationContext& transformation = {});

const char* ToString(ArtifactRelation value) noexcept;
const char* ToString(StreamRelation value) noexcept;
const char* ToString(SourceCoverage value) noexcept;
const char* ToString(PreservationApplicability value) noexcept;
const char* ToString(MediaSigningPreservation value) noexcept;
const char* ToString(SigningMetadataRelation value) noexcept;
const char* ToString(ChangeKind value) noexcept;
const char* ToString(CorrelationQuality value) noexcept;
const char* ToString(TransformationKind value) noexcept;
const char* ToString(PreservationFindingCode value) noexcept;
const char* ToString(PreservationLimitationCode value) noexcept;

}  // namespace videotrust
