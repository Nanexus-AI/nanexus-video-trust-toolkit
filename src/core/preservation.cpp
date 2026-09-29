#include "videotrust/preservation.hpp"

namespace videotrust {
namespace {

template <typename T>
ChangeKind ChangedOnly(T before, T after) {
  return before == after ? ChangeKind::Unchanged : ChangeKind::Changed;
}

ChangeKind SigningChange(SigningPresence before, SigningPresence after) {
  if (before == after) return ChangeKind::Unchanged;
  return before == SigningPresence::Detected ? ChangeKind::Degraded
                                             : ChangeKind::Improved;
}

int IntegrityRank(SignatureIntegrity value) {
  switch (value) {
    case SignatureIntegrity::Ok: return 4;
    case SignatureIntegrity::OkWithMissingInfo: return 3;
    case SignatureIntegrity::NotFeasible: return 2;
    case SignatureIntegrity::VersionMismatch: return 1;
    case SignatureIntegrity::NotOk: return 0;
    case SignatureIntegrity::NotApplicable: return -1;
  }
  return -1;
}

int ContinuityRank(ContinuityStatus value) {
  switch (value) {
    case ContinuityStatus::Intact: return 3;
    case ContinuityStatus::MissingInfo: return 2;
    case ContinuityStatus::Broken: return 1;
    case ContinuityStatus::NotApplicable: return 0;
  }
  return 0;
}

int CompletenessRank(VerificationCompleteness value) {
  switch (value) {
    case VerificationCompleteness::Complete: return 2;
    case VerificationCompleteness::Incomplete: return 1;
    case VerificationCompleteness::NotFeasible: return 0;
  }
  return 0;
}

template <typename T, typename Rank>
ChangeKind RankedChange(T before, T after, Rank rank) {
  if (before == after) return ChangeKind::Unchanged;
  const int lhs = rank(before);
  const int rhs = rank(after);
  if (rhs > lhs) return ChangeKind::Improved;
  if (rhs < lhs) return ChangeKind::Degraded;
  return ChangeKind::Changed;
}

ChangeKind CountChange(unsigned before, unsigned after) {
  if (before == after) return ChangeKind::Unchanged;
  return after < before ? ChangeKind::Improved : ChangeKind::Degraded;
}

void AddFinding(PreservationAssessment& out,
                PreservationFindingCode code,
                const char* message) {
  out.findings.push_back({code, message});
}

void AddLimitation(PreservationAssessment& out,
                   PreservationLimitationCode code,
                   const char* message) {
  out.limitations.push_back({code, message});
}

bool VerificationEvidenceEqual(const VerificationResult& before,
                               const VerificationResult& after) {
  return before.media_signing == after.media_signing &&
         before.signature_integrity == after.signature_integrity &&
         before.continuity == after.continuity &&
         before.completeness == after.completeness &&
         before.certificate == after.certificate &&
         before.public_key_has_changed == after.public_key_has_changed;
}

bool HasPending(const InspectionResult& value) {
  return value.accumulated.pending_nalus != 0 ||
         value.accumulated.pending_frames != 0 ||
         (value.latest.pending_hashable_nalus.has_value() &&
          *value.latest.pending_hashable_nalus != 0);
}

}  // namespace

PreservationAssessment DerivePreservationAssessment(
    const InspectionResult& before,
    const InspectionResult& after,
    const MediaCorrelationEvidence& correlation,
    const TransformationContext& transformation) {
  PreservationAssessment out;
  out.before = before;
  out.after = after;
  out.correlation = correlation;
  out.transformation = transformation;

  out.artifact_relation = correlation.byte_identical == EvidenceState::Yes
                              ? ArtifactRelation::Identical
                          : correlation.byte_identical == EvidenceState::No
                              ? ArtifactRelation::Different
                              : ArtifactRelation::Indeterminate;

  const bool sequence_complete = correlation.before.sequence_details_complete &&
                                 correlation.after.sequence_details_complete;
  const bool classifier_complete =
      correlation.before.signing_sei_classification_complete &&
      correlation.after.signing_sei_classification_complete;
  const bool signing_correlation_complete =
      correlation.signing_sei.correlation_complete == EvidenceState::Yes;

  if (correlation.nal.diagnostic_detail_bounded) {
    out.correlation_quality = CorrelationQuality::ResourceBounded;
    AddFinding(out, PreservationFindingCode::CorrelationResourceBounded,
               "Correlation detail was limited by configured resource bounds");
  } else if (correlation.nal.after_is_ordered_subsequence == EvidenceState::Yes &&
             correlation.nal.unique_subsequence_alignment == EvidenceState::No) {
    out.correlation_quality = CorrelationQuality::Ambiguous;
    AddFinding(out, PreservationFindingCode::CorrelationAmbiguous,
               "Repeated payloads permit more than one source alignment");
  } else if (sequence_complete && classifier_complete &&
             signing_correlation_complete) {
    out.correlation_quality = CorrelationQuality::Complete;
  } else {
    out.correlation_quality = CorrelationQuality::Incomplete;
  }

  if (sequence_complete &&
      correlation.nal.sequence_equivalent == EvidenceState::Yes) {
    out.stream_relation = StreamRelation::Equivalent;
    out.source_coverage = SourceCoverage::Full;
    AddFinding(out, PreservationFindingCode::NormalizedStreamEquivalent,
               "Normalized NAL payload sequence is equivalent");
  } else if (sequence_complete &&
             correlation.nal.after_is_ordered_subsequence == EvidenceState::Yes &&
             correlation.nal.unique_subsequence_alignment == EvidenceState::Yes) {
    out.stream_relation = StreamRelation::OrderedSubset;
    out.source_coverage = SourceCoverage::Subset;
    AddFinding(out, PreservationFindingCode::OrderedSubsetEstablished,
               "After artifact is a uniquely aligned ordered source subset");
  } else if (sequence_complete &&
             correlation.nal.sequence_equivalent == EvidenceState::No &&
             correlation.nal.after_is_ordered_subsequence == EvidenceState::No) {
    out.stream_relation = StreamRelation::StructurallyChanged;
    out.source_coverage = SourceCoverage::Unknown;
    AddFinding(out, PreservationFindingCode::SourceCoverageUnknown,
               "Structural correlation does not establish source coverage");
  } else {
    out.stream_relation = StreamRelation::Indeterminate;
    out.source_coverage = SourceCoverage::Unknown;
    AddFinding(out, PreservationFindingCode::SourceCoverageUnknown,
               "Correlation evidence is insufficient to establish source coverage");
  }

  if (out.artifact_relation == ArtifactRelation::Identical) {
    AddFinding(out, PreservationFindingCode::ExactArtifactMatch,
               "Before and after artifact bytes are identical");
  }

  if (!classifier_complete) {
    out.applicability = PreservationApplicability::Indeterminate;
  } else if (before.verification.media_signing == SigningPresence::NotDetected) {
    out.applicability = PreservationApplicability::NotApplicable;
  } else {
    out.applicability = PreservationApplicability::Applicable;
  }

  const auto& signing = correlation.signing_sei;
  if (out.applicability == PreservationApplicability::NotApplicable) {
    out.signing_metadata_relation = SigningMetadataRelation::NotApplicable;
  } else if (!classifier_complete || !signing_correlation_complete) {
    out.signing_metadata_relation = SigningMetadataRelation::Indeterminate;
  } else if (signing.after_count == 0 && signing.missing_from_after_count > 0) {
    out.signing_metadata_relation = SigningMetadataRelation::MissingAfter;
  } else if (signing.unmatched_after_count > 0 &&
             signing.matched_payload_count == 0) {
    out.signing_metadata_relation = SigningMetadataRelation::ReplacedOrUnmatched;
  } else if (signing.matched_payload_count > 0 &&
             (signing.missing_from_after_count > 0 ||
              signing.unmatched_after_count > 0)) {
    out.signing_metadata_relation =
        out.source_coverage == SourceCoverage::Subset &&
                signing.unmatched_after_count == 0 &&
                signing.matched_payload_count == signing.after_count
            ? SigningMetadataRelation::RetainedForSubset
            : SigningMetadataRelation::PartiallyRetained;
  } else if (signing.before_count == signing.after_count &&
             signing.matched_payload_count == signing.before_count) {
    out.signing_metadata_relation = SigningMetadataRelation::Equivalent;
  } else {
    out.signing_metadata_relation = SigningMetadataRelation::Indeterminate;
  }

  auto& vt = out.verification_transitions;
  vt.before_overall = DeriveOverallState(before.verification);
  vt.after_overall = DeriveOverallState(after.verification);
  vt.media_signing = SigningChange(before.verification.media_signing,
                                   after.verification.media_signing);
  vt.signature_integrity = RankedChange(before.verification.signature_integrity,
                                        after.verification.signature_integrity,
                                        IntegrityRank);
  vt.continuity = RankedChange(before.verification.continuity,
                               after.verification.continuity, ContinuityRank);
  vt.completeness = RankedChange(before.verification.completeness,
                                 after.verification.completeness,
                                 CompletenessRank);
  vt.certificate = ChangedOnly(before.verification.certificate,
                               after.verification.certificate);
  vt.public_key_observation = ChangedOnly(
      before.verification.public_key_has_changed,
      after.verification.public_key_has_changed);

  auto& it = out.inspection_transitions;
  it.pending_nalus = CountChange(before.accumulated.pending_nalus,
                                 after.accumulated.pending_nalus);
  it.pending_frames = CountChange(before.accumulated.pending_frames,
                                  after.accumulated.pending_frames);
  it.accumulated_timestamps =
      before.accumulated.first_timestamp == after.accumulated.first_timestamp &&
              before.accumulated.last_timestamp == after.accumulated.last_timestamp
          ? ChangeKind::Unchanged
          : ChangeKind::Changed;
  it.latest_timestamps =
      before.latest.start_timestamp == after.latest.start_timestamp &&
              before.latest.end_timestamp == after.latest.end_timestamp
          ? ChangeKind::Unchanged
          : ChangeKind::Changed;

  if (out.applicability == PreservationApplicability::NotApplicable) {
    out.media_signing_preservation = MediaSigningPreservation::Indeterminate;
    AddFinding(out, PreservationFindingCode::PreservationNotApplicable,
               "Before artifact contains no detected Media Signing evidence");
  } else if (out.applicability == PreservationApplicability::Indeterminate ||
             out.correlation_quality == CorrelationQuality::ResourceBounded ||
             out.correlation_quality == CorrelationQuality::Ambiguous ||
             !signing_correlation_complete) {
    out.media_signing_preservation = MediaSigningPreservation::Indeterminate;
    AddFinding(out, PreservationFindingCode::SigningContextIncomplete,
               "Signing evidence is insufficient for a preservation conclusion");
  } else {
    const bool equivalent_evidence =
        out.source_coverage == SourceCoverage::Full &&
        signing.missing_from_after_count == 0 &&
        signing.unmatched_after_count == 0 &&
        signing.matched_payload_count == signing.before_count &&
        signing.before_count == signing.after_count &&
        VerificationEvidenceEqual(before.verification, after.verification);
    const bool complete_subset_evidence =
        out.source_coverage == SourceCoverage::Subset && signing.after_count > 0 &&
        signing.matched_payload_count == signing.after_count &&
        signing.unmatched_after_count == 0 &&
        after.verification.media_signing == SigningPresence::Detected &&
        after.verification.signature_integrity == SignatureIntegrity::Ok &&
        after.verification.continuity == ContinuityStatus::Intact &&
        after.verification.completeness == VerificationCompleteness::Complete &&
        !HasPending(after);
    const bool affirmative_loss =
        (signing.before_count > 0 && signing.after_count == 0 &&
         signing.missing_from_after_count == signing.before_count) ||
        (signing.before_count > 0 && signing.matched_payload_count == 0 &&
         signing.unmatched_after_count > 0) ||
        after.verification.signature_integrity == SignatureIntegrity::NotOk ||
        after.verification.continuity == ContinuityStatus::Broken;
    const bool meaningful_partial =
        signing.matched_payload_count > 0 &&
        after.verification.media_signing == SigningPresence::Detected &&
        (after.verification.signature_integrity ==
             SignatureIntegrity::OkWithMissingInfo ||
         after.verification.continuity == ContinuityStatus::MissingInfo ||
         after.verification.completeness == VerificationCompleteness::Incomplete ||
         HasPending(after) ||
         (out.source_coverage == SourceCoverage::Full &&
          signing.missing_from_after_count > 0));

    if (equivalent_evidence || complete_subset_evidence) {
      out.media_signing_preservation = MediaSigningPreservation::Preserved;
      AddFinding(out, PreservationFindingCode::SigningEvidenceRetained,
                 complete_subset_evidence
                     ? "Applicable signing evidence is retained for the correlated subset"
                     : "Applicable signing evidence and verification state are retained");
    } else if (affirmative_loss) {
      out.media_signing_preservation = MediaSigningPreservation::NotPreserved;
      AddFinding(out,
                 signing.after_count == 0
                     ? PreservationFindingCode::SigningEvidenceMissing
                     : PreservationFindingCode::SigningEvidenceUnmatched,
                 "Applicable original signing evidence is absent, replaced, or invalidated");
    } else if (meaningful_partial) {
      out.media_signing_preservation =
          MediaSigningPreservation::PartiallyPreserved;
      AddFinding(out, PreservationFindingCode::SigningEvidencePartiallyRetained,
                 "Some original signing evidence remains while relevant validation is incomplete");
    } else {
      out.media_signing_preservation = MediaSigningPreservation::Indeterminate;
      AddFinding(out, PreservationFindingCode::SigningContextIncomplete,
                 "Available correlated evidence does not support a definitive preservation state");
    }
  }

  if (vt.media_signing == ChangeKind::Degraded ||
      vt.signature_integrity == ChangeKind::Degraded ||
      vt.continuity == ChangeKind::Degraded ||
      vt.completeness == ChangeKind::Degraded) {
    AddFinding(out, PreservationFindingCode::VerificationDegraded,
               "One or more verification dimensions degraded after transformation");
  }

  AddLimitation(out, PreservationLimitationCode::SourceAuthenticityNotEstablished,
                "Preservation does not establish source or event authenticity");
  AddLimitation(out, PreservationLimitationCode::VmsTrustNotEstablished,
                "Preservation does not establish a trustworthy VMS or transformer");
  AddLimitation(
      out, PreservationLimitationCode::SuppliedBeforeCompletenessNotEstablished,
      "The supplied before artifact is not proven to be the complete real-world recording");
  AddLimitation(out, PreservationLimitationCode::ChainOfCustodyNotEstablished,
                "This assessment does not establish chain of custody");
  AddLimitation(out, PreservationLimitationCode::TransformationContextUntrusted,
                "Transformation labels and provenance fields are caller-declared observations");
  if (out.source_coverage == SourceCoverage::Subset) {
    AddLimitation(out,
                  PreservationLimitationCode::SubsetDoesNotEstablishFullExport,
                  "A correlated subset does not show that the entire source was exported");
  }
  return out;
}

#define VT_ENUM_STRING_CASE(value) case value: return #value

const char* ToString(ArtifactRelation value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(ArtifactRelation::Identical); VT_ENUM_STRING_CASE(ArtifactRelation::Different); VT_ENUM_STRING_CASE(ArtifactRelation::Indeterminate); }
  return "ArtifactRelation::Indeterminate";
}
const char* ToString(StreamRelation value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(StreamRelation::Equivalent); VT_ENUM_STRING_CASE(StreamRelation::OrderedSubset); VT_ENUM_STRING_CASE(StreamRelation::StructurallyChanged); VT_ENUM_STRING_CASE(StreamRelation::Indeterminate); }
  return "StreamRelation::Indeterminate";
}
const char* ToString(SourceCoverage value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(SourceCoverage::Full); VT_ENUM_STRING_CASE(SourceCoverage::Subset); VT_ENUM_STRING_CASE(SourceCoverage::Unknown); }
  return "SourceCoverage::Unknown";
}
const char* ToString(PreservationApplicability value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(PreservationApplicability::Applicable); VT_ENUM_STRING_CASE(PreservationApplicability::NotApplicable); VT_ENUM_STRING_CASE(PreservationApplicability::Indeterminate); }
  return "PreservationApplicability::Indeterminate";
}
const char* ToString(MediaSigningPreservation value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(MediaSigningPreservation::Preserved); VT_ENUM_STRING_CASE(MediaSigningPreservation::PartiallyPreserved); VT_ENUM_STRING_CASE(MediaSigningPreservation::NotPreserved); VT_ENUM_STRING_CASE(MediaSigningPreservation::Indeterminate); }
  return "MediaSigningPreservation::Indeterminate";
}
const char* ToString(SigningMetadataRelation value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(SigningMetadataRelation::Equivalent); VT_ENUM_STRING_CASE(SigningMetadataRelation::RetainedForSubset); VT_ENUM_STRING_CASE(SigningMetadataRelation::PartiallyRetained); VT_ENUM_STRING_CASE(SigningMetadataRelation::MissingAfter); VT_ENUM_STRING_CASE(SigningMetadataRelation::ReplacedOrUnmatched); VT_ENUM_STRING_CASE(SigningMetadataRelation::NotApplicable); VT_ENUM_STRING_CASE(SigningMetadataRelation::Indeterminate); }
  return "SigningMetadataRelation::Indeterminate";
}
const char* ToString(ChangeKind value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(ChangeKind::Unchanged); VT_ENUM_STRING_CASE(ChangeKind::Improved); VT_ENUM_STRING_CASE(ChangeKind::Degraded); VT_ENUM_STRING_CASE(ChangeKind::Changed); VT_ENUM_STRING_CASE(ChangeKind::Indeterminate); }
  return "ChangeKind::Indeterminate";
}
const char* ToString(CorrelationQuality value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(CorrelationQuality::Complete); VT_ENUM_STRING_CASE(CorrelationQuality::Ambiguous); VT_ENUM_STRING_CASE(CorrelationQuality::ResourceBounded); VT_ENUM_STRING_CASE(CorrelationQuality::Incomplete); }
  return "CorrelationQuality::Incomplete";
}
const char* ToString(TransformationKind value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(TransformationKind::Unspecified); VT_ENUM_STRING_CASE(TransformationKind::Transparent); VT_ENUM_STRING_CASE(TransformationKind::Remux); VT_ENUM_STRING_CASE(TransformationKind::Clip); VT_ENUM_STRING_CASE(TransformationKind::Segment); VT_ENUM_STRING_CASE(TransformationKind::Concatenate); VT_ENUM_STRING_CASE(TransformationKind::Transcode); VT_ENUM_STRING_CASE(TransformationKind::MetadataChange); VT_ENUM_STRING_CASE(TransformationKind::TimestampRewrite); VT_ENUM_STRING_CASE(TransformationKind::ProprietaryOrUnknown); }
  return "TransformationKind::Unspecified";
}
const char* ToString(PreservationFindingCode value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(PreservationFindingCode::ExactArtifactMatch); VT_ENUM_STRING_CASE(PreservationFindingCode::NormalizedStreamEquivalent); VT_ENUM_STRING_CASE(PreservationFindingCode::OrderedSubsetEstablished); VT_ENUM_STRING_CASE(PreservationFindingCode::SourceCoverageUnknown); VT_ENUM_STRING_CASE(PreservationFindingCode::SigningEvidenceRetained); VT_ENUM_STRING_CASE(PreservationFindingCode::SigningEvidencePartiallyRetained); VT_ENUM_STRING_CASE(PreservationFindingCode::SigningEvidenceMissing); VT_ENUM_STRING_CASE(PreservationFindingCode::SigningEvidenceUnmatched); VT_ENUM_STRING_CASE(PreservationFindingCode::SigningContextIncomplete); VT_ENUM_STRING_CASE(PreservationFindingCode::CorrelationAmbiguous); VT_ENUM_STRING_CASE(PreservationFindingCode::CorrelationResourceBounded); VT_ENUM_STRING_CASE(PreservationFindingCode::VerificationDegraded); VT_ENUM_STRING_CASE(PreservationFindingCode::PreservationNotApplicable); }
  return "PreservationFindingCode::SigningContextIncomplete";
}
const char* ToString(PreservationLimitationCode value) noexcept {
  switch (value) { VT_ENUM_STRING_CASE(PreservationLimitationCode::SourceAuthenticityNotEstablished); VT_ENUM_STRING_CASE(PreservationLimitationCode::VmsTrustNotEstablished); VT_ENUM_STRING_CASE(PreservationLimitationCode::SuppliedBeforeCompletenessNotEstablished); VT_ENUM_STRING_CASE(PreservationLimitationCode::ChainOfCustodyNotEstablished); VT_ENUM_STRING_CASE(PreservationLimitationCode::TransformationContextUntrusted); VT_ENUM_STRING_CASE(PreservationLimitationCode::SubsetDoesNotEstablishFullExport); }
  return "PreservationLimitationCode::SourceAuthenticityNotEstablished";
}

#undef VT_ENUM_STRING_CASE

}  // namespace videotrust
