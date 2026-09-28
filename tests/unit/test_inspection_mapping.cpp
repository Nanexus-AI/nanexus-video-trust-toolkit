#include "videotrust/upstream_mapping.hpp"

#include <cstring>
#include <iostream>
#include <string>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

bool SameOptionalString(const std::optional<std::string>& a,
                        const std::optional<std::string>& b) {
  return a == b;
}

bool SameVerification(const videotrust::VerificationResult& a,
                      const videotrust::VerificationResult& b) {
  return a.schema_version == b.schema_version && a.codec == b.codec &&
         a.media_signing == b.media_signing &&
         a.signature_integrity == b.signature_integrity &&
         a.continuity == b.continuity && a.completeness == b.completeness &&
         a.certificate == b.certificate &&
         a.source_authenticity == b.source_authenticity &&
         a.public_key_has_changed == b.public_key_has_changed &&
         SameOptionalString(a.vendor_manufacturer, b.vendor_manufacturer) &&
         SameOptionalString(a.signing_lib_version, b.signing_lib_version) &&
         SameOptionalString(a.validation_lib_version, b.validation_lib_version) &&
         a.findings.size() == b.findings.size() &&
         videotrust::DeriveOverallState(a) == videotrust::DeriveOverallState(b) &&
         videotrust::ExitCodeForVerification(a) ==
             videotrust::ExitCodeForVerification(b);
}

onvif_media_signing_authenticity_t MakeBaseReport() {
  onvif_media_signing_authenticity_t report{};
  report.accumulated_validation.authenticity = OMS_AUTHENTICITY_OK;
  report.accumulated_validation.provenance = OMS_PROVENANCE_OK;
  // Combined enum must never become a Nanexus AUTHENTIC shortcut.
  report.accumulated_validation.authenticity_and_provenance =
      OMS_AUTHENTICITY_AND_PROVENANCE_OK;
  report.latest_validation.authenticity = OMS_AUTHENTICITY_OK;
  report.latest_validation.provenance = OMS_PROVENANCE_OK;
  report.latest_validation.authenticity_and_provenance =
      OMS_AUTHENTICITY_AND_PROVENANCE_OK;
  return report;
}

}  // namespace

int main() {
  using namespace videotrust;

  {
    auto report = MakeBaseReport();
    report.accumulated_validation.number_of_received_nalus = 10;
    report.accumulated_validation.number_of_validated_nalus = 8;
    report.accumulated_validation.number_of_pending_nalus = 2;
    report.accumulated_validation.number_of_received_frames = 5;
    report.accumulated_validation.number_of_validated_frames = 4;
    report.accumulated_validation.number_of_pending_frames = 1;
    report.accumulated_validation.first_timestamp = 100;
    report.accumulated_validation.last_timestamp = 200;

    const auto insp = MapInspectionFromUpstream(Codec::H264, report, true);
    if (insp.accumulated.received_nalus != 10 ||
        insp.accumulated.validated_nalus != 8 ||
        insp.accumulated.pending_nalus != 2 ||
        insp.accumulated.received_frames != 5 ||
        insp.accumulated.validated_frames != 4 ||
        insp.accumulated.pending_frames != 1 ||
        insp.accumulated.first_timestamp != 100 ||
        insp.accumulated.last_timestamp != 200) {
      return fail("accumulated fields");
    }
  }

  {
    auto report = MakeBaseReport();
    report.latest_validation.number_of_expected_hashable_nalus = 7;
    report.latest_validation.number_of_received_hashable_nalus = 6;
    report.latest_validation.number_of_pending_hashable_nalus = 1;
    report.latest_validation.start_timestamp = 1000;
    report.latest_validation.end_timestamp = 2000;

    const auto insp = MapInspectionFromUpstream(Codec::H265, report, true);
    if (!insp.latest.expected_hashable_nalus ||
        *insp.latest.expected_hashable_nalus != 7 ||
        !insp.latest.received_hashable_nalus ||
        *insp.latest.received_hashable_nalus != 6 ||
        !insp.latest.pending_hashable_nalus ||
        *insp.latest.pending_hashable_nalus != 1 ||
        insp.latest.start_timestamp != 1000 ||
        insp.latest.end_timestamp != 2000) {
      return fail("latest hashable/timestamps");
    }
  }

  {
    auto report = MakeBaseReport();
    // Upstream negative sentinels → unavailable (nullopt), not magic negatives.
    report.latest_validation.number_of_expected_hashable_nalus = -1;
    report.latest_validation.number_of_received_hashable_nalus = -2;
    report.latest_validation.number_of_pending_hashable_nalus = -3;

    const auto insp = MapInspectionFromUpstream(Codec::H264, report, false);
    if (insp.latest.expected_hashable_nalus || insp.latest.received_hashable_nalus ||
        insp.latest.pending_hashable_nalus) {
      return fail("negative hashable counts must map to nullopt");
    }
  }

  {
    auto report = MakeBaseReport();
    std::strncpy(report.vendor_info.manufacturer, "AcmeCam",
                 sizeof(report.vendor_info.manufacturer) - 1);
    std::strncpy(report.vendor_info.firmware_version, "1.2.3",
                 sizeof(report.vendor_info.firmware_version) - 1);
    std::strncpy(report.vendor_info.serial_number, "SN-42",
                 sizeof(report.vendor_info.serial_number) - 1);

    const auto insp = MapInspectionFromUpstream(Codec::H264, report, true);
    if (!insp.vendor.manufacturer || *insp.vendor.manufacturer != "AcmeCam") {
      return fail("manufacturer");
    }
    if (!insp.vendor.firmware_version || *insp.vendor.firmware_version != "1.2.3") {
      return fail("firmware");
    }
    if (!insp.vendor.serial_number || *insp.vendor.serial_number != "SN-42") {
      return fail("serial");
    }
    if (insp.verification.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("vendor metadata must not establish source authenticity");
    }
    if (DeriveOverallState(insp.verification) != OverallState::Valid) {
      return fail("vendor metadata must not alter overall");
    }
  }

  {
    auto report = MakeBaseReport();
    // Empty C strings → nullopt.
    report.vendor_info.manufacturer[0] = '\0';
    report.vendor_info.firmware_version[0] = '\0';
    report.vendor_info.serial_number[0] = '\0';

    const auto insp = MapInspectionFromUpstream(Codec::H264, report, true);
    if (insp.vendor.manufacturer || insp.vendor.firmware_version ||
        insp.vendor.serial_number) {
      return fail("empty vendor strings must be nullopt");
    }
  }

  {
    auto report = MakeBaseReport();
    report.accumulated_validation.number_of_received_nalus = 3;
    std::strncpy(report.vendor_info.manufacturer, "x",
                 sizeof(report.vendor_info.manufacturer) - 1);
    // Opaque diagnostic pointers must not be required by the inspection model.
    char validation_chars[] = "....N";
    char nalu_chars[] = "IPPPS";
    report.latest_validation.validation_str = validation_chars;
    report.latest_validation.nalu_str = nalu_chars;

    const auto verify_only = MapFromUpstream(Codec::H264, report, true);
    const auto insp = MapInspectionFromUpstream(Codec::H264, report, true);
    if (!SameVerification(verify_only, insp.verification)) {
      return fail("inspection must not change VerificationResult vs MapFromUpstream");
    }
    if (DeriveOverallState(verify_only) != DeriveOverallState(insp.verification) ||
        ExitCodeForVerification(verify_only) !=
            ExitCodeForVerification(insp.verification)) {
      return fail("inspection must not change overall/exit derivation");
    }
    // Combined AUTHENTICITY_AND_PROVENANCE_OK is present on the report but is not a
    // Nanexus field; overall still comes only from M1 axis rules.
    if (DeriveOverallState(insp.verification) != OverallState::Valid) {
      return fail("combined authenticity_and_provenance must not invent overall");
    }
    if (insp.verification.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("source_authenticity must remain not_established");
    }
  }

  {
    // Zero accumulated counts are zero, not "unknown".
    auto report = MakeBaseReport();
    report.accumulated_validation.authenticity = OMS_NOT_SIGNED;
    report.accumulated_validation.provenance = OMS_PROVENANCE_NOT_FEASIBLE;
    report.accumulated_validation.authenticity_and_provenance =
        OMS_AUTHENTICITY_AND_PROVENANCE_NOT_FEASIBLE;

    const auto insp = MapInspectionFromUpstream(Codec::H264, report, false);
    if (insp.accumulated.received_nalus != 0 || insp.accumulated.pending_nalus != 0) {
      return fail("zero counts");
    }
    if (insp.verification.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("unsigned source");
    }
    if (ExitCodeForVerification(insp.verification) != ExitCode::UnsignedOrNotVerifiable) {
      return fail("unsigned exit unchanged");
    }
  }

  std::cout << "PASS: inspection mapping\n";
  return 0;
}
