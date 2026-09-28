#pragma once

#include "videotrust/inspection.hpp"
#include "videotrust/result.hpp"

#include <onvif_media_signing_validator.h>

namespace videotrust {

/// Map an upstream authenticity report into Nanexus VerificationResult.
///
/// Trust axes are kept separate:
/// signature integrity ≠ certificate status ≠ source authenticity.
/// Upstream provenance maps to CertificateStatus only; SourceAuthenticity stays
/// NotEstablished for M1 reference-lab material.
VerificationResult MapFromUpstream(Codec codec,
                                   const onvif_media_signing_authenticity_t& report,
                                   bool trust_anchor_provided);

/// Map the same authenticity report into InspectionResult.
///
/// Uses MapFromUpstream for the embedded VerificationResult, then adds typed
/// observation groups. Does not change VerificationResult semantics, does not
/// map validation_str/nalu_str, and does not expose authenticity_and_provenance.
InspectionResult MapInspectionFromUpstream(
    Codec codec,
    const onvif_media_signing_authenticity_t& report,
    bool trust_anchor_provided);

SignatureIntegrity MapAuthenticity(MediaSigningAuthenticityResult v) noexcept;
CertificateStatus MapProvenance(MediaSigningProvenanceResult v,
                                bool trust_anchor_provided) noexcept;

}  // namespace videotrust
