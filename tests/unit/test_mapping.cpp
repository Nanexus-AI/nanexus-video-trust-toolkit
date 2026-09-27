#include "videotrust/upstream_mapping.hpp"

#include <cstring>
#include <iostream>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

onvif_media_signing_authenticity_t MakeReport(MediaSigningAuthenticityResult auth,
                                              MediaSigningProvenanceResult prov) {
  onvif_media_signing_authenticity_t report{};
  report.accumulated_validation.authenticity = auth;
  report.accumulated_validation.provenance = prov;
  report.accumulated_validation.authenticity_and_provenance =
      OMS_AUTHENTICITY_AND_PROVENANCE_NOT_FEASIBLE;
  report.latest_validation.authenticity = auth;
  report.latest_validation.provenance = prov;
  std::strncpy(report.vendor_info.manufacturer, "test-vendor",
               sizeof(report.vendor_info.manufacturer) - 1);
  return report;
}

}  // namespace

int main() {
  using namespace videotrust;

  // Unsigned: integrity not applicable; source still not established.
  {
    const auto report = MakeReport(OMS_NOT_SIGNED, OMS_PROVENANCE_NOT_FEASIBLE);
    const auto r = MapFromUpstream(Codec::H264, report, /*trust_anchor_provided=*/false);
    if (r.media_signing != SigningPresence::NotDetected) {
      return fail("unsigned presence");
    }
    if (r.signature_integrity != SignatureIntegrity::NotApplicable) {
      return fail("unsigned integrity");
    }
    if (r.certificate != CertificateStatus::NotProvided) {
      return fail("unsigned cert axis");
    }
    if (r.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("unsigned must not claim source authenticity");
    }
    if (ExitCodeForVerification(r) != ExitCode::UnsignedOrNotVerifiable) {
      return fail("unsigned exit code");
    }
  }

  // Valid signature WITHOUT trust anchor: must NOT claim source authenticity.
  {
    const auto report = MakeReport(OMS_AUTHENTICITY_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H265, report, /*trust_anchor_provided=*/false);
    if (r.signature_integrity != SignatureIntegrity::Ok) {
      return fail("ok integrity");
    }
    if (r.certificate != CertificateStatus::NotProvided) {
      return fail("no-ca must stay NotProvided");
    }
    if (r.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("valid signature must not imply source authenticity");
    }
    if (ExitCodeForVerification(r) != ExitCode::Success) {
      return fail("ok exit code");
    }
  }

  // Valid signature WITH trust anchor + provenance OK.
  {
    const auto report = MakeReport(OMS_AUTHENTICITY_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H264, report, /*trust_anchor_provided=*/true);
    if (r.certificate != CertificateStatus::Ok) {
      return fail("ca ok");
    }
    if (r.source_authenticity != SourceAuthenticity::ProvenanceOk) {
      return fail("provenance ok mapping");
    }
  }

  // Integrity failure is independent of certificate.
  {
    const auto report = MakeReport(OMS_AUTHENTICITY_NOT_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H264, report, true);
    if (r.signature_integrity != SignatureIntegrity::NotOk) {
      return fail("not_ok integrity");
    }
    if (r.certificate != CertificateStatus::Ok) {
      return fail("axes must not collapse");
    }
    if (ExitCodeForVerification(r) != ExitCode::VerificationNegative) {
      return fail("negative exit code");
    }
  }

  // Enum mappers
  if (MapAuthenticity(OMS_AUTHENTICITY_OK_WITH_MISSING_INFO) !=
      SignatureIntegrity::OkWithMissingInfo) {
    return fail("missing info map");
  }
  if (MapAuthenticity(OMS_AUTHENTICITY_VERSION_MISMATCH) !=
      SignatureIntegrity::VersionMismatch) {
    return fail("version mismatch map");
  }

  std::cout << "PASS: upstream mapping\n";
  return 0;
}
