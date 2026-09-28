#pragma once

#include "videotrust/annexb.hpp"
#include "videotrust/error.hpp"
#include "videotrust/inspection.hpp"
#include "videotrust/result.hpp"
#include "videotrust/session.hpp"

#include <string>
#include <vector>

namespace videotrust {

struct VerifyOptions {
  Codec codec{Codec::H264};
  std::string input_path;
  /// Empty means no trust anchor configured.
  std::string ca_pem_path;
};

/// Verify an Annex-B file using the official ONVIF validator C API.
Expected<VerificationResult> VerifyAnnexBFile(const VerifyOptions& options);

/// Inspect an Annex-B file using one official ONVIF validation session/report.
/// The returned InspectionResult embeds the M1 verification mapping from that
/// same report and adds the typed M2 observations.
Expected<InspectionResult> InspectAnnexBFile(const VerifyOptions& options);

/// Configure trust anchor PEM on an existing session (before first NAL).
Error SetTrustedCertificateFromFile(MediaSigningSession& session,
                                    const std::string& ca_pem_path);

/// Feed Annex-B NALs (with start codes) and return mapped VerificationResult.
Expected<VerificationResult> VerifyNalUnits(MediaSigningSession& session,
                                            Codec codec,
                                            const std::vector<NalUnit>& nalus,
                                            bool trust_anchor_provided);

}  // namespace videotrust
