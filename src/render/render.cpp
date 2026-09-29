#include "videotrust/render.hpp"

#include <sstream>

namespace videotrust {
namespace {

std::string JsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

std::string PresenceJson(SigningPresence v) {
  return v == SigningPresence::Detected ? "true" : "false";
}

std::string NullableStringJson(const std::optional<std::string>& value) {
  return value ? ("\"" + JsonEscape(*value) + "\"") : "null";
}

template <typename T>
void RenderNullableInteger(std::ostringstream& o, const std::optional<T>& value) {
  if (value) {
    o << *value;
  } else {
    o << "null";
  }
}

template <typename T>
void RenderOptionalText(std::ostringstream& o, const std::optional<T>& value) {
  if (value) {
    o << *value;
  } else {
    o << "unavailable";
  }
}

void RenderOptionalText(std::ostringstream& o,
                        const std::optional<std::string>& value) {
  if (!value) {
    o << "unavailable";
  } else if (value->empty()) {
    o << "(empty)";
  } else {
    o << *value;
  }
}

void RenderArtifactSnapshot(std::ostringstream& o,
                            const ArtifactEvidence& artifact,
                            const InspectionResult& inspection) {
  o << "{\"codec\":\"" << ToString(inspection.verification.codec) << "\",";
  o << "\"byte_size\":" << artifact.byte_size << ",";
  o << "\"sha256\":{\"algorithm\":\"sha256\",\"value\":\""
    << JsonEscape(artifact.sha256) << "\"},";
  o << "\"nal_count\":" << artifact.nal_count << ",";
  o << "\"signing_sei_count\":" << artifact.signing_sei_count << ",";
  o << "\"verification\":{";
  o << "\"media_signing\":\"" << ToString(inspection.verification.media_signing)
    << "\",";
  o << "\"signature_integrity\":\""
    << ToString(inspection.verification.signature_integrity) << "\",";
  o << "\"continuity\":\"" << ToString(inspection.verification.continuity)
    << "\",";
  o << "\"verification_completeness\":\""
    << ToString(inspection.verification.completeness) << "\",";
  o << "\"certificate_status\":\""
    << ToString(inspection.verification.certificate) << "\",";
  o << "\"source_authenticity\":\""
    << ToString(inspection.verification.source_authenticity) << "\",";
  o << "\"public_key_has_changed\":"
    << (inspection.verification.public_key_has_changed ? "true" : "false") << ",";
  o << "\"overall\":\""
    << ToString(DeriveOverallState(inspection.verification)) << "\"},";
  o << "\"inspection\":{";
  o << "\"pending_nalus\":" << inspection.accumulated.pending_nalus << ",";
  o << "\"pending_frames\":" << inspection.accumulated.pending_frames << ",";
  o << "\"pending_hashable_nalus\":";
  RenderNullableInteger(o, inspection.latest.pending_hashable_nalus);
  o << "}}";
}

void RenderRoleCounts(std::ostringstream& o, const NalRoleCounts& roles) {
  o << "{\"vcl\":" << roles.vcl << ",";
  o << "\"sei\":" << roles.sei << ",";
  o << "\"signing_sei\":" << roles.signing_sei << ",";
  o << "\"parameter_set\":" << roles.parameter_set << ",";
  o << "\"other\":" << roles.other << "}";
}

void RenderUnmatchedSummary(std::ostringstream& o,
                            const IndexSamples& samples,
                            const NalRoleCounts& roles) {
  o << "{\"total_count\":" << samples.total_count << ",";
  o << "\"sample_count\":" << samples.indices.size() << ",";
  o << "\"samples_truncated\":"
    << (samples.samples_truncated ? "true" : "false") << ",";
  o << "\"roles\":";
  RenderRoleCounts(o, roles);
  o << "}";
}

}  // namespace

std::string RenderText(const VerificationResult& result) {
  std::ostringstream o;
  o << "Input\n";
  o << "  Codec: " << (result.codec == Codec::H264 ? "H.264" : "H.265") << "\n";
  o << "Media Signing\n";
  o << "  Present: "
    << (result.media_signing == SigningPresence::Detected ? "yes" : "no") << "\n";
  o << "Signature Integrity\n";
  o << "  Status: " << ToString(result.signature_integrity) << "\n";
  o << "Continuity\n";
  o << "  Status: " << ToString(result.continuity) << "\n";
  o << "Verification\n";
  o << "  Completeness: " << ToString(result.completeness) << "\n";
  o << "Certificate / Signing-key Provenance\n";
  o << "  Status: " << ToString(result.certificate) << "\n";
  o << "Source Authenticity\n";
  o << "  Status: " << ToString(result.source_authenticity) << "\n";
  if (result.vendor_manufacturer) {
    o << "Vendor\n";
    o << "  Manufacturer: " << *result.vendor_manufacturer << "\n";
  }
  if (!result.findings.empty()) {
    o << "Findings\n";
    for (const auto& f : result.findings) {
      o << "  - [" << f.code << "] " << f.message << "\n";
    }
  }
  o << "Overall\n";
  o << "  " << ToString(DeriveOverallState(result)) << "\n";
  return o.str();
}

std::string RenderJson(const VerificationResult& result) {
  std::ostringstream o;
  o << "{";
  o << "\"schema_version\":\"" << JsonEscape(result.schema_version) << "\",";
  o << "\"codec\":\"" << ToString(result.codec) << "\",";
  o << "\"media_signing\":{\"present\":" << PresenceJson(result.media_signing) << "},";
  o << "\"signature_integrity\":\"" << ToString(result.signature_integrity) << "\",";
  o << "\"continuity\":\"" << ToString(result.continuity) << "\",";
  o << "\"verification_completeness\":\"" << ToString(result.completeness) << "\",";
  o << "\"certificate_status\":\"" << ToString(result.certificate) << "\",";
  o << "\"source_authenticity\":\"" << ToString(result.source_authenticity) << "\",";
  o << "\"public_key_has_changed\":" << (result.public_key_has_changed ? "true" : "false")
    << ",";
  o << "\"overall\":\"" << ToString(DeriveOverallState(result)) << "\",";
  if (result.vendor_manufacturer) {
    o << "\"vendor\":{\"manufacturer\":\"" << JsonEscape(*result.vendor_manufacturer)
      << "\"},";
  } else {
    o << "\"vendor\":null,";
  }
  o << "\"versions\":{";
  o << "\"signing\":"
    << (result.signing_lib_version
            ? ("\"" + JsonEscape(*result.signing_lib_version) + "\"")
            : "null")
    << ",";
  o << "\"validation\":"
    << (result.validation_lib_version
            ? ("\"" + JsonEscape(*result.validation_lib_version) + "\"")
            : "null");
  o << "},";
  o << "\"findings\":[";
  for (std::size_t i = 0; i < result.findings.size(); ++i) {
    if (i) {
      o << ",";
    }
    o << "{\"code\":\"" << JsonEscape(result.findings[i].code) << "\",\"message\":\""
      << JsonEscape(result.findings[i].message) << "\"}";
  }
  o << "]";
  o << "}\n";
  return o.str();
}

std::string RenderInspectionText(const InspectionResult& result) {
  const VerificationResult& verification = result.verification;
  std::ostringstream o;
  o << "Nanexus Video Trust Inspection\n";
  o << "Verification\n";
  o << "  Codec: " << (verification.codec == Codec::H264 ? "H.264" : "H.265")
    << "\n";
  o << "  Media Signing present: "
    << (verification.media_signing == SigningPresence::Detected ? "yes" : "no")
    << "\n";
  o << "  Signature integrity: " << ToString(verification.signature_integrity) << "\n";
  o << "  Continuity: " << ToString(verification.continuity) << "\n";
  o << "  Verification completeness: " << ToString(verification.completeness) << "\n";
  o << "  Certificate status: " << ToString(verification.certificate) << "\n";
  o << "  Source authenticity: " << ToString(verification.source_authenticity) << "\n";
  o << "  Public key changed: " << (verification.public_key_has_changed ? "yes" : "no")
    << "\n";
  o << "  Overall: " << ToString(DeriveOverallState(verification)) << "\n";

  o << "Library Versions\n";
  o << "  Signing: ";
  RenderOptionalText(o, verification.signing_lib_version);
  o << "\n  Validation: ";
  RenderOptionalText(o, verification.validation_lib_version);
  o << "\n";

  o << "Accumulated Validation\n";
  o << "  Received NALUs: " << result.accumulated.received_nalus << "\n";
  o << "  Validated NALUs: " << result.accumulated.validated_nalus << "\n";
  o << "  Pending NALUs: " << result.accumulated.pending_nalus << "\n";
  o << "  Received frames: " << result.accumulated.received_frames << "\n";
  o << "  Validated frames: " << result.accumulated.validated_frames << "\n";
  o << "  Pending frames: " << result.accumulated.pending_frames << "\n";
  o << "  First timestamp (FILETIME 100 ns ticks): " << result.accumulated.first_timestamp
    << "\n";
  o << "  Last timestamp (FILETIME 100 ns ticks): " << result.accumulated.last_timestamp
    << "\n";

  o << "Latest Validation\n";
  o << "  Expected hashable NALUs: ";
  RenderOptionalText(o, result.latest.expected_hashable_nalus);
  o << "\n  Received hashable NALUs: ";
  RenderOptionalText(o, result.latest.received_hashable_nalus);
  o << "\n  Pending hashable NALUs: ";
  RenderOptionalText(o, result.latest.pending_hashable_nalus);
  o << "\n  Start timestamp (FILETIME 100 ns ticks): " << result.latest.start_timestamp
    << "\n";
  o << "  End timestamp (FILETIME 100 ns ticks): " << result.latest.end_timestamp << "\n";

  o << "Vendor Observations\n";
  o << "  Manufacturer: ";
  RenderOptionalText(o, result.vendor.manufacturer);
  o << "\n  Firmware version: ";
  RenderOptionalText(o, result.vendor.firmware_version);
  o << "\n  Serial number: ";
  RenderOptionalText(o, result.vendor.serial_number);
  o << "\n";

  if (!verification.findings.empty()) {
    o << "Findings\n";
    for (const auto& finding : verification.findings) {
      o << "  - [" << finding.code << "] " << finding.message << "\n";
    }
  }

  o << "Limitations\n";
  o << "  Certificate status describes signing-key provenance, not source authenticity.\n";
  o << "  Vendor observations do not establish device identity or trust.\n";
  return o.str();
}

std::string RenderInspectionJson(const InspectionResult& result) {
  std::string verification_json = RenderJson(result.verification);
  if (!verification_json.empty() && verification_json.back() == '\n') {
    verification_json.pop_back();
  }

  std::ostringstream o;
  o << "{";
  o << "\"document_type\":\"media_signing_inspection\",";
  o << "\"schema_version\":\"0.1\",";
  o << "\"verification\":" << verification_json << ",";
  o << "\"accumulated_validation\":{";
  o << "\"number_of_received_nalus\":" << result.accumulated.received_nalus << ",";
  o << "\"number_of_validated_nalus\":" << result.accumulated.validated_nalus << ",";
  o << "\"number_of_pending_nalus\":" << result.accumulated.pending_nalus << ",";
  o << "\"number_of_received_frames\":" << result.accumulated.received_frames << ",";
  o << "\"number_of_validated_frames\":" << result.accumulated.validated_frames << ",";
  o << "\"number_of_pending_frames\":" << result.accumulated.pending_frames << ",";
  o << "\"first_timestamp\":" << result.accumulated.first_timestamp << ",";
  o << "\"last_timestamp\":" << result.accumulated.last_timestamp;
  o << "},";
  o << "\"latest_validation\":{";
  o << "\"number_of_expected_hashable_nalus\":";
  RenderNullableInteger(o, result.latest.expected_hashable_nalus);
  o << ",\"number_of_received_hashable_nalus\":";
  RenderNullableInteger(o, result.latest.received_hashable_nalus);
  o << ",\"number_of_pending_hashable_nalus\":";
  RenderNullableInteger(o, result.latest.pending_hashable_nalus);
  o << ",\"start_timestamp\":" << result.latest.start_timestamp << ",";
  o << "\"end_timestamp\":" << result.latest.end_timestamp;
  o << "},";
  o << "\"vendor\":{";
  o << "\"manufacturer\":" << NullableStringJson(result.vendor.manufacturer) << ",";
  o << "\"firmware_version\":" << NullableStringJson(result.vendor.firmware_version)
    << ",";
  o << "\"serial_number\":" << NullableStringJson(result.vendor.serial_number);
  o << "}";
  o << "}\n";
  return o.str();
}

std::string RenderPreservationJson(const PreservationAssessment& assessment) {
  const auto& correlation = assessment.correlation;
  std::ostringstream o;
  o << "{";
  o << "\"document_type\":\"media_signing_preservation_assessment\",";
  o << "\"schema_version\":\"0.1\",";
  o << "\"before\":";
  RenderArtifactSnapshot(o, correlation.before, assessment.before);
  o << ",\"after\":";
  RenderArtifactSnapshot(o, correlation.after, assessment.after);

  o << ",\"transformation\":{";
  o << "\"kind\":\"" << ToString(assessment.transformation.kind) << "\",";
  o << "\"label\":" << NullableStringJson(assessment.transformation.label) << ",";
  o << "\"pipeline_id\":"
    << NullableStringJson(assessment.transformation.pipeline_id) << ",";
  o << "\"tool_version\":"
    << NullableStringJson(assessment.transformation.tool_version) << ",";
  o << "\"log_digest\":"
    << NullableStringJson(assessment.transformation.log_digest) << ",";
  o << "\"trust\":\"caller_declared_untrusted\"}";

  o << ",\"artifact_identity\":{";
  o << "\"byte_relation\":\"" << ToString(assessment.artifact_relation)
    << "\"}";

  o << ",\"correlation\":{";
  o << "\"quality\":\"" << ToString(assessment.correlation_quality) << "\",";
  o << "\"stream_relation\":\"" << ToString(assessment.stream_relation)
    << "\",";
  o << "\"sequence_equivalent\":\""
    << ToString(correlation.nal.sequence_equivalent) << "\",";
  o << "\"after_is_ordered_subsequence\":\""
    << ToString(correlation.nal.after_is_ordered_subsequence) << "\",";
  o << "\"unique_subsequence_alignment\":\""
    << ToString(correlation.nal.unique_subsequence_alignment) << "\",";
  o << "\"reordered\":\"" << ToString(correlation.nal.reordered) << "\",";
  o << "\"duplicated_after\":\""
    << ToString(correlation.nal.duplicated_after) << "\",";
  o << "\"normalized_payload_matches\":"
    << correlation.nal.normalized_payload_matches << ",";
  o << "\"unique_before_range\":";
  if (correlation.nal.unique_before_start && correlation.nal.unique_before_end) {
    o << "{\"start_nal_index\":" << *correlation.nal.unique_before_start << ",";
    o << "\"end_nal_index\":" << *correlation.nal.unique_before_end << "}";
  } else {
    o << "null";
  }
  o << ",\"unmatched_before\":";
  RenderUnmatchedSummary(o, correlation.nal.unmatched_before,
                         correlation.nal.unmatched_before_roles);
  o << ",\"unmatched_after\":";
  RenderUnmatchedSummary(o, correlation.nal.unmatched_after,
                         correlation.nal.unmatched_after_roles);
  o << ",\"diagnostic_detail_bounded\":"
    << (correlation.nal.diagnostic_detail_bounded ? "true" : "false") << ",";
  o << "\"signing_metadata\":{";
  o << "\"relation\":\"" << ToString(assessment.signing_metadata_relation)
    << "\",";
  o << "\"before_count\":" << correlation.signing_sei.before_count << ",";
  o << "\"after_count\":" << correlation.signing_sei.after_count << ",";
  o << "\"matched_payload_count\":"
    << correlation.signing_sei.matched_payload_count << ",";
  o << "\"missing_from_after_count\":"
    << correlation.signing_sei.missing_from_after_count << ",";
  o << "\"unmatched_after_count\":"
    << correlation.signing_sei.unmatched_after_count << ",";
  o << "\"correlation_complete\":\""
    << ToString(correlation.signing_sei.correlation_complete) << "\",";
  o << "\"before_classification_complete\":"
    << (correlation.before.signing_sei_classification_complete ? "true" : "false")
    << ",";
  o << "\"after_classification_complete\":"
    << (correlation.after.signing_sei_classification_complete ? "true" : "false")
    << "}}";

  o << ",\"coverage\":{\"state\":\"" << ToString(assessment.source_coverage)
    << "\"}";
  o << ",\"applicability\":{\"media_signing_preservation\":\""
    << ToString(assessment.applicability) << "\"}";
  o << ",\"preservation\":{\"media_signing_evidence\":\""
    << ToString(assessment.media_signing_preservation) << "\"}";

  const auto& verification = assessment.verification_transitions;
  const auto& inspection = assessment.inspection_transitions;
  o << ",\"transitions\":{";
  o << "\"verification\":{";
  o << "\"before_overall\":\"" << ToString(verification.before_overall) << "\",";
  o << "\"after_overall\":\"" << ToString(verification.after_overall) << "\",";
  o << "\"media_signing\":\"" << ToString(verification.media_signing) << "\",";
  o << "\"signature_integrity\":\""
    << ToString(verification.signature_integrity) << "\",";
  o << "\"continuity\":\"" << ToString(verification.continuity) << "\",";
  o << "\"verification_completeness\":\""
    << ToString(verification.completeness) << "\",";
  o << "\"certificate\":\"" << ToString(verification.certificate) << "\",";
  o << "\"public_key_observation\":\""
    << ToString(verification.public_key_observation) << "\"},";
  o << "\"inspection\":{";
  o << "\"pending_nalus\":\"" << ToString(inspection.pending_nalus) << "\",";
  o << "\"pending_frames\":\"" << ToString(inspection.pending_frames) << "\",";
  o << "\"accumulated_timestamps\":\""
    << ToString(inspection.accumulated_timestamps) << "\",";
  o << "\"latest_timestamps\":\"" << ToString(inspection.latest_timestamps)
    << "\"}}";

  o << ",\"findings\":[";
  for (std::size_t i = 0; i < assessment.findings.size(); ++i) {
    if (i) o << ",";
    o << "{\"code\":\"" << ToString(assessment.findings[i].code)
      << "\",\"message\":\"" << JsonEscape(assessment.findings[i].message)
      << "\"}";
  }
  o << "],\"limitations\":[";
  for (std::size_t i = 0; i < assessment.limitations.size(); ++i) {
    if (i) o << ",";
    o << "{\"code\":\"" << ToString(assessment.limitations[i].code)
      << "\",\"message\":\"" << JsonEscape(assessment.limitations[i].message)
      << "\"}";
  }
  o << "]}\n";
  return o.str();
}

}  // namespace videotrust
