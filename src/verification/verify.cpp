#include "videotrust/verify.hpp"

#include "videotrust/annexb.hpp"
#include "videotrust/upstream_mapping.hpp"

#include <onvif_media_signing_validator.h>

#include <fstream>
#include <iterator>
#include <sstream>

namespace videotrust {
namespace {

template <typename Result, typename Mapper>
Expected<Result> MapNalUnitsFromOneReport(MediaSigningSession& session,
                                          const std::vector<NalUnit>& nalus,
                                          Mapper mapper) {
  if (nalus.empty()) {
    return MakeError(ErrorCode::ParseError, "no NAL units to verify");
  }

  for (const auto& nal : nalus) {
    onvif_media_signing_authenticity_t* report = nullptr;
    const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
        session.get(), nal.bytes.data(), nal.bytes.size(), &report);
    if (report != nullptr) {
      onvif_media_signing_authenticity_report_free(report);
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
  Result mapped = mapper(*final_report);
  onvif_media_signing_authenticity_report_free(final_report);
  return mapped;
}

template <typename Result, typename Processor>
Expected<Result> ProcessAnnexBFile(const VerifyOptions& options, Processor processor) {
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
  return processor(session.value(), nalus.value(), has_ca);
}

}  // namespace

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
  return MapNalUnitsFromOneReport<VerificationResult>(
      session, nalus, [&](const onvif_media_signing_authenticity_t& report) {
        return MapFromUpstream(codec, report, trust_anchor_provided);
      });
}

Expected<VerificationResult> VerifyAnnexBFile(const VerifyOptions& options) {
  return ProcessAnnexBFile<VerificationResult>(
      options,
      [&](MediaSigningSession& session,
          const std::vector<NalUnit>& nalus,
          bool has_ca) {
        return VerifyNalUnits(session, options.codec, nalus, has_ca);
      });
}

Expected<InspectionResult> InspectAnnexBFile(const VerifyOptions& options) {
  return ProcessAnnexBFile<InspectionResult>(
      options,
      [&](MediaSigningSession& session,
          const std::vector<NalUnit>& nalus,
          bool has_ca) {
        return MapNalUnitsFromOneReport<InspectionResult>(
            session, nalus, [&](const onvif_media_signing_authenticity_t& report) {
              return MapInspectionFromUpstream(options.codec, report, has_ca);
            });
      });
}

}  // namespace videotrust
