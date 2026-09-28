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

}  // namespace videotrust
