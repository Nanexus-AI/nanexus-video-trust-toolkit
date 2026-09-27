#include "videotrust/upstream_mapping.hpp"

#include <cstring>

namespace videotrust {

SignatureIntegrity MapAuthenticity(MediaSigningAuthenticityResult v) noexcept {
  switch (v) {
    case OMS_NOT_SIGNED:
      return SignatureIntegrity::NotApplicable;
    case OMS_AUTHENTICITY_NOT_FEASIBLE:
      return SignatureIntegrity::NotFeasible;
    case OMS_AUTHENTICITY_NOT_OK:
      return SignatureIntegrity::NotOk;
    case OMS_AUTHENTICITY_OK_WITH_MISSING_INFO:
      return SignatureIntegrity::OkWithMissingInfo;
    case OMS_AUTHENTICITY_OK:
      return SignatureIntegrity::Ok;
    case OMS_AUTHENTICITY_VERSION_MISMATCH:
      return SignatureIntegrity::VersionMismatch;
    default:
      return SignatureIntegrity::NotFeasible;
  }
}

CertificateStatus MapProvenance(MediaSigningProvenanceResult v,
                                bool trust_anchor_provided) noexcept {
  if (!trust_anchor_provided) {
    return CertificateStatus::NotProvided;
  }
  switch (v) {
    case OMS_PROVENANCE_NOT_FEASIBLE:
      return CertificateStatus::NotFeasible;
    case OMS_PROVENANCE_NOT_OK:
      return CertificateStatus::NotOk;
    case OMS_PROVENANCE_FEASIBLE_WITHOUT_TRUSTED:
      return CertificateStatus::FeasibleWithoutTrusted;
    case OMS_PROVENANCE_OK:
      return CertificateStatus::Ok;
    default:
      return CertificateStatus::NotFeasible;
  }
}

VerificationResult MapFromUpstream(Codec codec,
                                   const onvif_media_signing_authenticity_t& report,
                                   bool trust_anchor_provided) {
  VerificationResult out;
  out.schema_version = "0.1";
  out.codec = codec;

  const auto& acc = report.accumulated_validation;
  out.signature_integrity = MapAuthenticity(acc.authenticity);
  out.public_key_has_changed = acc.public_key_has_changed;

  if (out.signature_integrity == SignatureIntegrity::NotApplicable) {
    out.media_signing = SigningPresence::NotDetected;
  } else {
    out.media_signing = SigningPresence::Detected;
  }

  switch (out.signature_integrity) {
    case SignatureIntegrity::NotApplicable:
      out.continuity = ContinuityStatus::NotApplicable;
      break;
    case SignatureIntegrity::OkWithMissingInfo:
      out.continuity = ContinuityStatus::MissingInfo;
      break;
    case SignatureIntegrity::NotOk:
      out.continuity = ContinuityStatus::Broken;
      break;
    case SignatureIntegrity::Ok:
      out.continuity = ContinuityStatus::Intact;
      break;
    default:
      out.continuity = ContinuityStatus::NotApplicable;
      break;
  }

  if (acc.number_of_pending_nalus > 0 || acc.number_of_pending_frames > 0) {
    out.completeness = VerificationCompleteness::Incomplete;
  } else if (out.signature_integrity == SignatureIntegrity::NotFeasible) {
    out.completeness = VerificationCompleteness::NotFeasible;
  } else {
    out.completeness = VerificationCompleteness::Complete;
  }

  out.certificate = MapProvenance(acc.provenance, trust_anchor_provided);

  // Source authenticity is not inferred from signature validity alone.
  if (!trust_anchor_provided) {
    out.source_authenticity = SourceAuthenticity::NotEstablished;
  } else if (acc.provenance == OMS_PROVENANCE_OK) {
    out.source_authenticity = SourceAuthenticity::ProvenanceOk;
  } else if (acc.provenance == OMS_PROVENANCE_NOT_OK) {
    out.source_authenticity = SourceAuthenticity::ProvenanceNotOk;
  } else {
    out.source_authenticity = SourceAuthenticity::NotEstablished;
  }

  if (report.vendor_info.manufacturer[0] != '\0') {
    out.vendor_manufacturer = std::string(report.vendor_info.manufacturer);
  }
  if (report.version_on_signing_side != nullptr) {
    out.signing_lib_version = std::string(report.version_on_signing_side);
  }
  if (report.this_version != nullptr) {
    out.validation_lib_version = std::string(report.this_version);
  }

  if (out.signature_integrity == SignatureIntegrity::NotOk) {
    out.findings.push_back({"AUTH_NOT_OK", "Upstream authenticity validation failed"});
  }
  if (out.certificate == CertificateStatus::NotOk) {
    out.findings.push_back({"PROVENANCE_NOT_OK", "Upstream provenance validation failed"});
  }
  if (out.certificate == CertificateStatus::FeasibleWithoutTrusted) {
    out.findings.push_back(
        {"PROVENANCE_WITHOUT_TRUSTED",
         "Upstream reported provenance without a trusted anchor; treat cautiously"});
  }

  return out;
}

}  // namespace videotrust
