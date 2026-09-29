#include "videotrust/preservation.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace {

using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

InspectionResult Inspection(Codec codec,
                            OverallState state,
                            unsigned pending = 0) {
  InspectionResult out;
  out.verification.codec = codec;
  switch (state) {
    case OverallState::Valid:
      out.verification.media_signing = SigningPresence::Detected;
      out.verification.signature_integrity = SignatureIntegrity::Ok;
      out.verification.continuity = ContinuityStatus::Intact;
      out.verification.completeness = VerificationCompleteness::Complete;
      out.verification.certificate = CertificateStatus::Ok;
      break;
    case OverallState::Partial:
      out.verification.media_signing = SigningPresence::Detected;
      out.verification.signature_integrity = SignatureIntegrity::Ok;
      out.verification.continuity = ContinuityStatus::Intact;
      out.verification.completeness = VerificationCompleteness::Incomplete;
      out.verification.certificate = CertificateStatus::Ok;
      break;
    case OverallState::Invalid:
      out.verification.media_signing = SigningPresence::Detected;
      out.verification.signature_integrity = SignatureIntegrity::NotOk;
      out.verification.continuity = ContinuityStatus::Broken;
      out.verification.completeness = VerificationCompleteness::Complete;
      out.verification.certificate = CertificateStatus::Ok;
      break;
    case OverallState::Unsigned:
      out.verification.media_signing = SigningPresence::NotDetected;
      out.verification.signature_integrity = SignatureIntegrity::NotApplicable;
      out.verification.continuity = ContinuityStatus::NotApplicable;
      out.verification.completeness = VerificationCompleteness::Incomplete;
      out.verification.certificate = CertificateStatus::NotFeasible;
      break;
    case OverallState::NotVerifiable:
      out.verification.media_signing = SigningPresence::Detected;
      out.verification.signature_integrity = SignatureIntegrity::NotFeasible;
      out.verification.continuity = ContinuityStatus::MissingInfo;
      out.verification.completeness = VerificationCompleteness::NotFeasible;
      break;
  }
  out.accumulated.pending_nalus = pending;
  out.accumulated.pending_frames = pending;
  out.latest.pending_hashable_nalus = static_cast<int>(pending);
  return out;
}

MediaCorrelationEvidence Equivalent(std::uint64_t signing_count = 2) {
  MediaCorrelationEvidence out;
  out.before.byte_size = out.after.byte_size = 100;
  out.before.sha256 = out.after.sha256 = "same";
  out.before.nal_count = out.after.nal_count = 10;
  out.before.signing_sei_count = out.after.signing_sei_count = signing_count;
  out.byte_identical = EvidenceState::Yes;
  out.nal.sequence_equivalent = EvidenceState::Yes;
  out.nal.after_is_ordered_subsequence = EvidenceState::Yes;
  out.nal.unique_subsequence_alignment = EvidenceState::Yes;
  out.signing_sei.before_count = signing_count;
  out.signing_sei.after_count = signing_count;
  out.signing_sei.matched_payload_count = signing_count;
  out.signing_sei.correlation_complete = EvidenceState::Yes;
  return out;
}

MediaCorrelationEvidence Subset(bool partial_signing = false) {
  auto out = Equivalent(2);
  out.byte_identical = EvidenceState::No;
  out.after.byte_size = 50;
  out.after.sha256 = "after";
  out.after.nal_count = 5;
  out.nal.sequence_equivalent = EvidenceState::No;
  out.nal.unique_before_start = 2;
  out.nal.unique_before_end = 6;
  out.signing_sei.after_count = 1;
  out.signing_sei.matched_payload_count = 1;
  out.signing_sei.missing_from_after_count = 1;
  if (partial_signing) {
    out.signing_sei.after_count = 2;
    out.signing_sei.unmatched_after_count = 1;
  }
  return out;
}

bool HasLimitation(const PreservationAssessment& value,
                   PreservationLimitationCode code) {
  return std::any_of(value.limitations.begin(), value.limitations.end(),
                     [code](const auto& item) { return item.code == code; });
}

TransformationContext Context(TransformationKind kind) {
  TransformationContext out;
  out.kind = kind;
  return out;
}

int RunCodec(Codec codec) {
  const auto valid = Inspection(codec, OverallState::Valid);

  // Exact identity is used with correlation, signing, and consistent M1/M2
  // evidence; it is not sufficient in isolation.
  auto exact = DerivePreservationAssessment(valid, valid, Equivalent());
  if (exact.source_coverage != SourceCoverage::Full ||
      exact.media_signing_preservation != MediaSigningPreservation::Preserved ||
      exact.applicability != PreservationApplicability::Applicable ||
      !HasLimitation(exact,
                     PreservationLimitationCode::SourceAuthenticityNotEstablished) ||
      !HasLimitation(exact,
                     PreservationLimitationCode::ChainOfCustodyNotEstablished)) {
    return Fail("exact signed assessment");
  }

  // Annex-B framing can differ while normalized sequence and signing evidence
  // remain equivalent.
  auto framing_evidence = Equivalent();
  framing_evidence.byte_identical = EvidenceState::No;
  framing_evidence.after.sha256 = "different";
  auto framing =
      DerivePreservationAssessment(valid, valid, framing_evidence,
                                   Context(TransformationKind::Remux));
  if (framing.artifact_relation != ArtifactRelation::Different ||
      framing.source_coverage != SourceCoverage::Full ||
      framing.media_signing_preservation != MediaSigningPreservation::Preserved) {
    return Fail("framing-only assessment");
  }

  // A unique ordered subset may preserve all signing evidence applicable to
  // that subset. Subset coverage alone does not select partial preservation.
  auto subset = DerivePreservationAssessment(valid, valid, Subset(),
                                              Context(TransformationKind::Clip));
  if (subset.source_coverage != SourceCoverage::Subset ||
      subset.media_signing_preservation != MediaSigningPreservation::Preserved ||
      subset.signing_metadata_relation !=
          SigningMetadataRelation::RetainedForSubset ||
      !HasLimitation(
          subset,
          PreservationLimitationCode::SubsetDoesNotEstablishFullExport)) {
    return Fail("preserved subset assessment");
  }

  const auto partial_after = Inspection(codec, OverallState::Partial, 3);
  auto partial = DerivePreservationAssessment(valid, partial_after, Subset(true));
  if (partial.source_coverage != SourceCoverage::Subset ||
      partial.media_signing_preservation !=
          MediaSigningPreservation::PartiallyPreserved) {
    return Fail("partial subset assessment");
  }

  auto stripped_evidence = Subset();
  stripped_evidence.after.signing_sei_count = 0;
  stripped_evidence.signing_sei.after_count = 0;
  stripped_evidence.signing_sei.matched_payload_count = 0;
  stripped_evidence.signing_sei.missing_from_after_count = 2;
  const auto unsigned_after = Inspection(codec, OverallState::Unsigned, 4);
  auto stripped =
      DerivePreservationAssessment(valid, unsigned_after, stripped_evidence);
  if (stripped.media_signing_preservation !=
          MediaSigningPreservation::NotPreserved ||
      stripped.signing_metadata_relation !=
          SigningMetadataRelation::MissingAfter) {
    return Fail("signing-SEI loss assessment");
  }

  // Controlled non-signing metadata removal changes NAL structure but keeps
  // the relevant signing payloads and complete verification.
  auto metadata_evidence = Subset();
  metadata_evidence.signing_sei.before_count = 1;
  metadata_evidence.signing_sei.after_count = 1;
  metadata_evidence.signing_sei.matched_payload_count = 1;
  metadata_evidence.signing_sei.missing_from_after_count = 0;
  auto metadata = DerivePreservationAssessment(
      valid, valid, metadata_evidence,
      Context(TransformationKind::MetadataChange));
  if (metadata.media_signing_preservation !=
      MediaSigningPreservation::Preserved) {
    return Fail("non-signing metadata assessment");
  }

  auto ambiguous_evidence = Subset();
  ambiguous_evidence.nal.unique_subsequence_alignment = EvidenceState::No;
  auto ambiguous =
      DerivePreservationAssessment(valid, valid, ambiguous_evidence);
  if (ambiguous.source_coverage != SourceCoverage::Unknown ||
      ambiguous.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate) {
    return Fail("ambiguous correlation assessment");
  }

  auto bounded_evidence = Equivalent();
  bounded_evidence.before.sequence_details_complete = false;
  bounded_evidence.after.sequence_details_complete = false;
  bounded_evidence.nal.sequence_equivalent = EvidenceState::Indeterminate;
  bounded_evidence.nal.after_is_ordered_subsequence = EvidenceState::Indeterminate;
  bounded_evidence.nal.diagnostic_detail_bounded = true;
  auto bounded = DerivePreservationAssessment(valid, valid, bounded_evidence);
  if (bounded.source_coverage != SourceCoverage::Unknown ||
      bounded.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate) {
    return Fail("resource-bound assessment");
  }

  // Reorder, duplication, and a caller's transcode label are structural facts
  // or declarations, not preservation shortcuts.
  auto structural_evidence = Equivalent();
  structural_evidence.byte_identical = EvidenceState::No;
  structural_evidence.nal.sequence_equivalent = EvidenceState::No;
  structural_evidence.nal.after_is_ordered_subsequence = EvidenceState::No;
  structural_evidence.nal.unique_subsequence_alignment =
      EvidenceState::Indeterminate;
  structural_evidence.nal.reordered = EvidenceState::Yes;
  auto reordered = DerivePreservationAssessment(
      valid, valid, structural_evidence, Context(TransformationKind::Transcode));
  if (reordered.source_coverage != SourceCoverage::Unknown ||
      reordered.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate) {
    return Fail("structural-change assessment");
  }
  structural_evidence.nal.reordered = EvidenceState::No;
  structural_evidence.nal.duplicated_after = EvidenceState::Yes;
  auto duplicated =
      DerivePreservationAssessment(valid, valid, structural_evidence);
  if (duplicated.source_coverage != SourceCoverage::Unknown ||
      duplicated.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate) {
    return Fail("duplicated-NAL assessment");
  }

  auto classifier_limited = Equivalent();
  classifier_limited.after.signing_sei_classification_complete = false;
  auto limited =
      DerivePreservationAssessment(valid, valid, classifier_limited);
  if (limited.applicability != PreservationApplicability::Indeterminate ||
      limited.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate) {
    return Fail("indeterminate signing-SEI classification");
  }

  const auto unsigned_before = Inspection(codec, OverallState::Unsigned);
  auto unsigned_assessment = DerivePreservationAssessment(
      unsigned_before, unsigned_before, Equivalent(0));
  if (unsigned_assessment.applicability !=
          PreservationApplicability::NotApplicable ||
      unsigned_assessment.media_signing_preservation !=
          MediaSigningPreservation::Indeterminate ||
      unsigned_assessment.signing_metadata_relation !=
          SigningMetadataRelation::NotApplicable) {
    return Fail("unsigned-before applicability");
  }

  // Invalid and partial sources are not assumed pristine. Exact, consistent
  // evidence can still preserve their observed signing evidence.
  const auto invalid = Inspection(codec, OverallState::Invalid);
  auto invalid_exact =
      DerivePreservationAssessment(invalid, invalid, Equivalent());
  if (invalid_exact.media_signing_preservation !=
      MediaSigningPreservation::Preserved) {
    return Fail("invalid-before exact preservation");
  }
  auto partial_exact =
      DerivePreservationAssessment(partial_after, partial_after, Equivalent());
  if (partial_exact.media_signing_preservation !=
      MediaSigningPreservation::Preserved) {
    return Fail("partial-before exact preservation");
  }

  // Caller labels do not affect identical factual evidence.
  auto transcode_label = DerivePreservationAssessment(
      valid, valid, Equivalent(), Context(TransformationKind::Transcode));
  if (transcode_label.media_signing_preservation !=
      exact.media_signing_preservation) {
    return Fail("transformation label influenced classification");
  }

  return 0;
}

}  // namespace

int main() {
  if (const int rc = RunCodec(videotrust::Codec::H264); rc != 0) return rc;
  if (const int rc = RunCodec(videotrust::Codec::H265); rc != 0) return rc;
  std::cout << "PASS: preservation derivation (H.264 + H.265)\n";
  return 0;
}
