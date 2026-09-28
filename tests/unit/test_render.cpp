#include "videotrust/render.hpp"
#include "videotrust/inspection.hpp"
#include "videotrust/result.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace videotrust;
  VerificationResult r;
  r.codec = Codec::H264;
  r.media_signing = SigningPresence::Detected;
  r.signature_integrity = SignatureIntegrity::Ok;
  r.continuity = ContinuityStatus::Intact;
  r.completeness = VerificationCompleteness::Complete;
  r.certificate = CertificateStatus::NotProvided;
  r.source_authenticity = SourceAuthenticity::NotEstablished;

  const std::string text = RenderText(r);
  if (text.find("Source Authenticity") == std::string::npos ||
      text.find("not_established") == std::string::npos ||
      text.find("VALID") == std::string::npos) {
    return fail("text render");
  }
  if (text.find("This video is authentic") != std::string::npos) {
    return fail("overclaim text");
  }

  const std::string json = RenderJson(r);
  if (json.find("\"schema_version\":\"0.1\"") == std::string::npos) {
    return fail("json schema");
  }
  if (json.find("\"authentic\"") != std::string::npos) {
    return fail("no authentic boolean");
  }
  if (json.find("source_authenticity\":\"not_established\"") == std::string::npos) {
    return fail("json source authenticity");
  }
  // JSON-only: single object ending with newline, no log prefixes.
  if (json.front() != '{' || json.find('\n') != json.size() - 1) {
    return fail("json must be one object + newline");
  }

  InspectionResult inspection;
  inspection.verification = r;
  inspection.accumulated = {7, 5, 2, 3, 2, 1, 0, 133485408001234567};
  inspection.latest.expected_hashable_nalus = 4;
  inspection.latest.received_hashable_nalus = 0;
  inspection.latest.pending_hashable_nalus = std::nullopt;
  inspection.latest.start_timestamp = 133485408001234500;
  inspection.latest.end_timestamp = 133485408001234567;
  inspection.vendor.manufacturer = "Acme \\\"Camera\\\"";
  inspection.vendor.firmware_version = std::nullopt;
  inspection.vendor.serial_number = "SN-001";

  const std::string inspection_json = RenderInspectionJson(inspection);
  const std::string expected_inspection_json =
      "{\"document_type\":\"media_signing_inspection\",\"schema_version\":\"0.1\","
      "\"verification\":" + json.substr(0, json.size() - 1) +
      ",\"accumulated_validation\":{\"number_of_received_nalus\":7,"
      "\"number_of_validated_nalus\":5,\"number_of_pending_nalus\":2,"
      "\"number_of_received_frames\":3,\"number_of_validated_frames\":2,"
      "\"number_of_pending_frames\":1,\"first_timestamp\":0,"
      "\"last_timestamp\":133485408001234567},\"latest_validation\":{"
      "\"number_of_expected_hashable_nalus\":4,"
      "\"number_of_received_hashable_nalus\":0,"
      "\"number_of_pending_hashable_nalus\":null,"
      "\"start_timestamp\":133485408001234500,"
      "\"end_timestamp\":133485408001234567},\"vendor\":{"
      "\"manufacturer\":\"Acme \\\\\\\"Camera\\\\\\\"\","
      "\"firmware_version\":null,\"serial_number\":\"SN-001\"}}\n";
  if (inspection_json != expected_inspection_json) {
    return fail("deterministic inspection json");
  }
  if (inspection_json.find("\"first_timestamp\":0") == std::string::npos ||
      inspection_json.find("\"number_of_received_hashable_nalus\":0") ==
          std::string::npos) {
    return fail("inspection zero must remain zero");
  }
  if (inspection_json.find("\"firmware_version\":null") == std::string::npos ||
      inspection_json.find("\"number_of_pending_hashable_nalus\":null") ==
          std::string::npos) {
    return fail("inspection unavailable values must be null");
  }
  if (inspection_json.find("validation_str") != std::string::npos ||
      inspection_json.find("nalu_str") != std::string::npos ||
      inspection_json.find("authenticity_and_provenance") != std::string::npos ||
      inspection_json.find("\"authentic\"") != std::string::npos) {
    return fail("inspection must exclude unstable or combined trust fields");
  }
  if (inspection_json.find("\"source_authenticity\":\"not_established\"") ==
      std::string::npos) {
    return fail("inspection source authenticity must not be overstated");
  }

  inspection.verification.signing_lib_version = std::nullopt;
  inspection.verification.validation_lib_version = "1.2.3";
  inspection.verification.findings.push_back({"EXAMPLE", "Synthetic finding"});
  const std::string inspection_text = RenderInspectionText(inspection);
  const std::vector<std::string> ordered_sections = {
      "Nanexus Video Trust Inspection\n",
      "Verification\n",
      "Library Versions\n",
      "Accumulated Validation\n",
      "Latest Validation\n",
      "Vendor Observations\n",
      "Findings\n",
      "Limitations\n",
  };
  std::size_t section_position = 0;
  for (const auto& section : ordered_sections) {
    const std::size_t found = inspection_text.find(section, section_position);
    if (found == std::string::npos) {
      return fail("inspection text section order");
    }
    section_position = found + section.size();
  }
  if (inspection_text.find("First timestamp (FILETIME 100 ns ticks): 0") ==
          std::string::npos ||
      inspection_text.find("Received hashable NALUs: 0") == std::string::npos) {
    return fail("inspection text preserves numeric zero");
  }
  if (inspection_text.find("Pending hashable NALUs: unavailable") ==
          std::string::npos ||
      inspection_text.find("Firmware version: unavailable") == std::string::npos ||
      inspection_text.find("Signing: unavailable") == std::string::npos) {
    return fail("inspection text unavailable policy");
  }
  if (inspection_text.find("Certificate status: not_provided") == std::string::npos ||
      inspection_text.find("Source authenticity: not_established") ==
          std::string::npos ||
      inspection_text.find("Vendor observations do not establish device identity or trust.") ==
          std::string::npos) {
    return fail("inspection text trust axes and limitations");
  }
  if (inspection_text.find("validation_str") != std::string::npos ||
      inspection_text.find("nalu_str") != std::string::npos ||
      inspection_text.find("AUTHENTIC=true") != std::string::npos ||
      inspection_text.find("camera/source is authentic") != std::string::npos) {
    return fail("inspection text must exclude overclaims and raw diagnostics");
  }

  if (argc == 2 && std::string(argv[1]) == "--inspection-json") {
    std::cout << inspection_json;
    return 0;
  }

  std::cout << "PASS: render\n";
  return 0;
}
