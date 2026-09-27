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

}  // namespace videotrust
