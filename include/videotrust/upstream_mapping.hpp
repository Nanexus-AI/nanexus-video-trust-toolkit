#pragma once

#include "videotrust/result.hpp"

#include <onvif_media_signing_validator.h>

namespace videotrust {

/// Map an upstream authenticity report into Nanexus VerificationResult.
///
/// Trust axes are kept separate:
/// signature integrity ≠ certificate status ≠ source authenticity.
/// Source authenticity stays NotEstablished unless provenance is explicitly OK/NotOk.
VerificationResult MapFromUpstream(Codec codec,
                                   const onvif_media_signing_authenticity_t& report,
                                   bool trust_anchor_provided);

SignatureIntegrity MapAuthenticity(MediaSigningAuthenticityResult v) noexcept;
CertificateStatus MapProvenance(MediaSigningProvenanceResult v,
                                bool trust_anchor_provided) noexcept;

}  // namespace videotrust
