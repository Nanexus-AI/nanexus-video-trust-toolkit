#include "videotrust/result.hpp"

namespace videotrust {
namespace {

bool IntegrityNegative(SignatureIntegrity v) noexcept {
  return v == SignatureIntegrity::NotOk || v == SignatureIntegrity::VersionMismatch;
}

bool IntegrityPositive(SignatureIntegrity v) noexcept {
  return v == SignatureIntegrity::Ok || v == SignatureIntegrity::OkWithMissingInfo;
}

}  // namespace

ExitCode ExitCodeForVerification(const VerificationResult& result) noexcept {
  if (IntegrityNegative(result.signature_integrity) ||
      result.continuity == ContinuityStatus::Broken) {
    return ExitCode::VerificationNegative;
  }
  if (result.media_signing == SigningPresence::NotDetected ||
      result.signature_integrity == SignatureIntegrity::NotApplicable ||
      result.signature_integrity == SignatureIntegrity::NotFeasible ||
      result.completeness == VerificationCompleteness::NotFeasible) {
    return ExitCode::UnsignedOrNotVerifiable;
  }
  if (IntegrityPositive(result.signature_integrity)) {
    return ExitCode::Success;
  }
  return ExitCode::UnsignedOrNotVerifiable;
}

const char* ToString(Codec v) noexcept {
  switch (v) {
    case Codec::H264:
      return "h264";
    case Codec::H265:
      return "h265";
  }
  return "unknown";
}

const char* ToString(SigningPresence v) noexcept {
  switch (v) {
    case SigningPresence::NotDetected:
      return "not_detected";
    case SigningPresence::Detected:
      return "detected";
  }
  return "unknown";
}

const char* ToString(SignatureIntegrity v) noexcept {
  switch (v) {
    case SignatureIntegrity::NotApplicable:
      return "not_applicable";
    case SignatureIntegrity::NotFeasible:
      return "not_feasible";
    case SignatureIntegrity::Ok:
      return "ok";
    case SignatureIntegrity::OkWithMissingInfo:
      return "ok_with_missing_info";
    case SignatureIntegrity::NotOk:
      return "not_ok";
    case SignatureIntegrity::VersionMismatch:
      return "version_mismatch";
  }
  return "unknown";
}

const char* ToString(ContinuityStatus v) noexcept {
  switch (v) {
    case ContinuityStatus::NotApplicable:
      return "not_applicable";
    case ContinuityStatus::Intact:
      return "intact";
    case ContinuityStatus::MissingInfo:
      return "missing_info";
    case ContinuityStatus::Broken:
      return "broken";
  }
  return "unknown";
}

const char* ToString(VerificationCompleteness v) noexcept {
  switch (v) {
    case VerificationCompleteness::Complete:
      return "complete";
    case VerificationCompleteness::Incomplete:
      return "incomplete";
    case VerificationCompleteness::NotFeasible:
      return "not_feasible";
  }
  return "unknown";
}

const char* ToString(CertificateStatus v) noexcept {
  switch (v) {
    case CertificateStatus::NotProvided:
      return "not_provided";
    case CertificateStatus::NotFeasible:
      return "not_feasible";
    case CertificateStatus::NotOk:
      return "not_ok";
    case CertificateStatus::Ok:
      return "ok";
    case CertificateStatus::FeasibleWithoutTrusted:
      return "feasible_without_trusted";
  }
  return "unknown";
}

const char* ToString(SourceAuthenticity v) noexcept {
  switch (v) {
    case SourceAuthenticity::NotEstablished:
      return "not_established";
    case SourceAuthenticity::ProvenanceOk:
      return "provenance_ok";
    case SourceAuthenticity::ProvenanceNotOk:
      return "provenance_not_ok";
  }
  return "unknown";
}

}  // namespace videotrust
