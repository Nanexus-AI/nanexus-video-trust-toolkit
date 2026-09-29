#include "videotrust/preservation.hpp"
#include "videotrust/render.hpp"

#include <iostream>
#include <string>

namespace {

using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

InspectionResult Inspection(OverallState state, unsigned pending = 0) {
  InspectionResult out;
  out.verification.codec = Codec::H264;
  out.latest.pending_hashable_nalus = std::nullopt;
  if (state == OverallState::Unsigned) {
    out.verification.media_signing = SigningPresence::NotDetected;
    out.verification.signature_integrity = SignatureIntegrity::NotApplicable;
    out.verification.continuity = ContinuityStatus::NotApplicable;
    out.verification.completeness = VerificationCompleteness::Incomplete;
    out.verification.certificate = CertificateStatus::NotFeasible;
  } else {
    out.verification.media_signing = SigningPresence::Detected;
    out.verification.signature_integrity = SignatureIntegrity::Ok;
    out.verification.continuity = ContinuityStatus::Intact;
    out.verification.completeness = state == OverallState::Partial
                                        ? VerificationCompleteness::Incomplete
                                        : VerificationCompleteness::Complete;
    out.verification.certificate = CertificateStatus::Ok;
  }
  out.accumulated.pending_nalus = pending;
  out.accumulated.pending_frames = pending;
  if (pending != 0) out.latest.pending_hashable_nalus = static_cast<int>(pending);
  return out;
}

MediaCorrelationEvidence Equivalent(std::uint64_t signing_count = 2) {
  MediaCorrelationEvidence out;
  out.before.byte_size = out.after.byte_size = 1024;
  out.before.sha256 = out.after.sha256 = std::string(64, 'a');
  out.before.nal_count = out.after.nal_count = 12;
  out.before.signing_sei_count = out.after.signing_sei_count = signing_count;
  out.byte_identical = EvidenceState::Yes;
  out.nal.sequence_equivalent = EvidenceState::Yes;
  out.nal.after_is_ordered_subsequence = EvidenceState::Yes;
  out.nal.unique_subsequence_alignment = EvidenceState::Yes;
  out.nal.reordered = EvidenceState::No;
  out.nal.duplicated_after = EvidenceState::No;
  out.nal.normalized_payload_matches = 12;
  out.signing_sei.before_count = signing_count;
  out.signing_sei.after_count = signing_count;
  out.signing_sei.matched_payload_count = signing_count;
  out.signing_sei.correlation_complete = EvidenceState::Yes;
  return out;
}

MediaCorrelationEvidence Subset() {
  auto out = Equivalent();
  out.after.byte_size = 700;
  out.after.sha256 = std::string(64, 'b');
  out.after.nal_count = 8;
  out.after.signing_sei_count = 1;
  out.byte_identical = EvidenceState::No;
  out.nal.sequence_equivalent = EvidenceState::No;
  out.nal.normalized_payload_matches = 8;
  out.nal.unique_before_start = 2;
  out.nal.unique_before_end = 9;
  out.nal.unmatched_before.total_count = 4;
  out.nal.unmatched_before.indices = {0, 1, 10, 11};
  out.nal.unmatched_before_roles.vcl = 4;
  out.signing_sei.after_count = 1;
  out.signing_sei.matched_payload_count = 1;
  out.signing_sei.missing_from_after_count = 1;
  return out;
}

TransformationContext Context(TransformationKind kind) {
  TransformationContext out;
  out.kind = kind;
  out.label = "caller observation";
  out.pipeline_id = "pipeline-1";
  return out;
}

PreservationAssessment MakeAssessment(const std::string& name) {
  const auto valid = Inspection(OverallState::Valid);
  if (name == "exact") {
    return DerivePreservationAssessment(valid, valid, Equivalent(),
                                        Context(TransformationKind::Transparent));
  }
  if (name == "subset") {
    return DerivePreservationAssessment(valid, valid, Subset(),
                                        Context(TransformationKind::Clip));
  }
  if (name == "partial") {
    auto evidence = Subset();
    evidence.signing_sei.after_count = 2;
    evidence.signing_sei.unmatched_after_count = 1;
    return DerivePreservationAssessment(
        valid, Inspection(OverallState::Partial, 3), evidence,
        Context(TransformationKind::Clip));
  }
  if (name == "not_preserved") {
    auto evidence = Subset();
    evidence.after.signing_sei_count = 0;
    evidence.signing_sei.after_count = 0;
    evidence.signing_sei.matched_payload_count = 0;
    evidence.signing_sei.missing_from_after_count = 2;
    return DerivePreservationAssessment(
        valid, Inspection(OverallState::Unsigned, 4), evidence,
        Context(TransformationKind::MetadataChange));
  }
  if (name == "ambiguous") {
    auto evidence = Subset();
    evidence.nal.unique_subsequence_alignment = EvidenceState::No;
    evidence.nal.unique_before_start.reset();
    evidence.nal.unique_before_end.reset();
    return DerivePreservationAssessment(
        valid, valid, evidence, Context(TransformationKind::ProprietaryOrUnknown));
  }
  if (name == "bounded") {
    auto evidence = Equivalent();
    evidence.before.sequence_details_complete = false;
    evidence.after.sequence_details_complete = false;
    evidence.nal.sequence_equivalent = EvidenceState::Indeterminate;
    evidence.nal.after_is_ordered_subsequence = EvidenceState::Indeterminate;
    evidence.nal.unique_subsequence_alignment = EvidenceState::Indeterminate;
    evidence.nal.diagnostic_detail_bounded = true;
    return DerivePreservationAssessment(valid, valid, evidence);
  }
  return DerivePreservationAssessment(Inspection(OverallState::Unsigned),
                                      Inspection(OverallState::Unsigned),
                                      Equivalent(0));
}

}  // namespace

int main(int argc, char** argv) {
  const std::string name = argc == 3 && std::string(argv[1]) == "--json"
                               ? argv[2]
                               : "exact";
  const auto assessment = MakeAssessment(name);
  const std::string json = RenderPreservationJson(assessment);

  if (json.empty() || json.front() != '{' || json.back() != '\n' ||
      json.find('\n') != json.size() - 1) {
    return Fail("JSON must be one deterministic object plus newline");
  }
  if (json.find("\"document_type\":\"media_signing_preservation_assessment\"") ==
          std::string::npos ||
      json.find("\"schema_version\":\"0.1\"") == std::string::npos) {
    return Fail("document identity");
  }
  if (json.find("/home/") != std::string::npos ||
      json.find("input_path") != std::string::npos ||
      json.find("validation_str") != std::string::npos ||
      json.find("nalu_str") != std::string::npos) {
    return Fail("host path or internal diagnostic leakage");
  }
  if (json.find("\"pending_nalus\":0") == std::string::npos ||
      json.find("\"pending_hashable_nalus\":null") == std::string::npos) {
    return Fail("zero/null policy");
  }

  const auto label_a = DerivePreservationAssessment(
      Inspection(OverallState::Valid), Inspection(OverallState::Valid),
      Equivalent(), Context(TransformationKind::Transcode));
  const auto label_b = DerivePreservationAssessment(
      Inspection(OverallState::Valid), Inspection(OverallState::Valid),
      Equivalent(), Context(TransformationKind::Transparent));
  if (label_a.media_signing_preservation != label_b.media_signing_preservation ||
      label_a.source_coverage != label_b.source_coverage) {
    return Fail("transformation context changed domain semantics");
  }

  if (argc == 3 && std::string(argv[1]) == "--json") {
    std::cout << json;
    return 0;
  }
  std::cout << "PASS: preservation JSON render\n";
  return 0;
}
