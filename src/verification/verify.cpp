#include "videotrust/verify.hpp"

#include "videotrust/annexb.hpp"
#include "videotrust/upstream_mapping.hpp"

#include <onvif_media_signing_validator.h>

#include <fstream>
#include <iterator>
#include <sstream>

namespace videotrust {

Error SetTrustedCertificateFromFile(MediaSigningSession& session,
                                    const std::string& ca_pem_path) {
  std::ifstream in(ca_pem_path);
  if (!in) {
    return MakeError(ErrorCode::IoFailure, "cannot open CA file: " + ca_pem_path);
  }
  std::string pem((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (pem.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "CA file is empty: " + ca_pem_path);
  }
  const MediaSigningReturnCode rc = onvif_media_signing_set_trusted_certificate(
      session.get(), pem.data(), pem.size());
  if (rc != OMS_OK) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_set_trusted_certificate failed: " +
                         std::to_string(static_cast<int>(rc)));
  }
  return MakeError(ErrorCode::Ok, {});
}

Expected<VerificationResult> VerifyNalUnits(MediaSigningSession& session,
                                            Codec codec,
                                            const std::vector<NalUnit>& nalus,
                                            bool trust_anchor_provided) {
  if (nalus.empty()) {
    return MakeError(ErrorCode::ParseError, "no NAL units to verify");
  }

  for (const auto& nal : nalus) {
    onvif_media_signing_authenticity_t* report = nullptr;
    const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
        session.get(), nal.bytes.data(), nal.bytes.size(), &report);
    if (report != nullptr) {
      onvif_media_signing_authenticity_report_free(report);
      report = nullptr;
    }
    if (rc != OMS_OK) {
      return MakeError(ErrorCode::UpstreamFailure,
                       "onvif_media_signing_add_nalu_and_authenticate failed: " +
                           std::to_string(static_cast<int>(rc)));
    }
  }

  onvif_media_signing_authenticity_t* final_report =
      onvif_media_signing_get_authenticity_report(session.get());
  if (final_report == nullptr) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "onvif_media_signing_get_authenticity_report returned null");
  }
  VerificationResult mapped =
      MapFromUpstream(codec, *final_report, trust_anchor_provided);
  onvif_media_signing_authenticity_report_free(final_report);
  return mapped;
}

Expected<VerificationResult> VerifyAnnexBFile(const VerifyOptions& options) {
  auto session = MediaSigningSession::Create(options.codec);
  if (!session.ok()) {
    return session.error();
  }

  const bool has_ca = !options.ca_pem_path.empty();
  if (has_ca) {
    const Error ca_err =
        SetTrustedCertificateFromFile(session.value(), options.ca_pem_path);
    if (ca_err.code != ErrorCode::Ok) {
      return ca_err;
    }
  }

  auto nalus = AnnexBReader::ParseFile(options.input_path);
  if (!nalus.ok()) {
    return nalus.error();
  }
  return VerifyNalUnits(session.value(), options.codec, nalus.value(), has_ca);
}

}  // namespace videotrust
