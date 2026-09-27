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

  {
    const auto report = MakeReport(OMS_NOT_SIGNED, OMS_PROVENANCE_NOT_FEASIBLE);
    const auto r = MapFromUpstream(Codec::H264, report, false);
    if (r.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("unsigned source");
    }
    if (ExitCodeForVerification(r) != ExitCode::UnsignedOrNotVerifiable) {
      return fail("unsigned exit");
    }
  }

  {
    const auto report = MakeReport(OMS_AUTHENTICITY_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H265, report, false);
    if (r.signature_integrity != SignatureIntegrity::Ok) {
      return fail("ok integrity");
    }
    if (r.certificate != CertificateStatus::NotProvided) {
      return fail("no-ca cert");
    }
    // Even if upstream provenance enum is OK, without CA Nanexus keeps cert NotProvided
    // and source authenticity remains not established.
    if (r.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("source must stay not_established");
    }
  }

  {
    const auto report = MakeReport(OMS_AUTHENTICITY_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H264, report, true);
    if (r.certificate != CertificateStatus::Ok) {
      return fail("ca maps to certificate ok");
    }
    if (r.source_authenticity != SourceAuthenticity::NotEstablished) {
      return fail("certificate ok must not become source authenticity");
    }
  }

  {
    const auto report = MakeReport(OMS_AUTHENTICITY_NOT_OK, OMS_PROVENANCE_OK);
    const auto r = MapFromUpstream(Codec::H264, report, true);
    if (r.signature_integrity != SignatureIntegrity::NotOk ||
        ExitCodeForVerification(r) != ExitCode::VerificationNegative) {
      return fail("negative");
    }
  }

  std::cout << "PASS: upstream mapping\n";
  return 0;
}
