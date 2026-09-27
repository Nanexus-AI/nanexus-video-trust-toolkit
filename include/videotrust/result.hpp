#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace videotrust {

enum class Codec {
  H264,
  H265,
};

enum class SigningPresence {
  NotDetected,
  Detected,
};

/// Maps upstream MediaSigningAuthenticityResult (media integrity axis).
enum class SignatureIntegrity {
  NotApplicable,  ///< No Media Signing detected (unsigned).
  NotFeasible,    ///< Signing detected but validation not yet feasible.
  Ok,
  OkWithMissingInfo,
  NotOk,
  VersionMismatch,
};

enum class ContinuityStatus {
  NotApplicable,
  Intact,
  MissingInfo,
  Broken,
};

enum class VerificationCompleteness {
  Complete,
  Incomplete,
  NotFeasible,
};

/// Maps upstream MediaSigningProvenanceResult (signing-key certificate axis).
/// This is NOT device/camera source identity.
enum class CertificateStatus {
  NotProvided,             ///< Caller supplied no trust anchor to Nanexus.
  NotFeasible,             ///< Upstream could not establish provenance.
  NotOk,
  Ok,
  FeasibleWithoutTrusted,  ///< Upstream allowed provenance without trusted CA.
};

/// Source / device authenticity for M1 reference-lab material.
/// Upstream provenance proves signing-key certificate trust, not camera identity.
/// Therefore M1 keeps this axis at NotEstablished for all lab scenarios.
enum class SourceAuthenticity {
  NotEstablished,
};

/// Convenience summary for CLI; never replaces individual trust axes.
enum class OverallState {
  Valid,
  Invalid,
  Unsigned,
  NotVerifiable,
  Partial,
};

struct Finding {
  std::string code;
  std::string message;
};

struct VerificationResult {
  std::string schema_version{"0.1"};
  Codec codec{Codec::H264};
  SigningPresence media_signing{SigningPresence::NotDetected};
  SignatureIntegrity signature_integrity{SignatureIntegrity::NotApplicable};
  ContinuityStatus continuity{ContinuityStatus::NotApplicable};
  VerificationCompleteness completeness{VerificationCompleteness::NotFeasible};
  CertificateStatus certificate{CertificateStatus::NotProvided};
  SourceAuthenticity source_authenticity{SourceAuthenticity::NotEstablished};
  bool public_key_has_changed{false};
  std::optional<std::string> vendor_manufacturer;
  std::optional<std::string> signing_lib_version;
  std::optional<std::string> validation_lib_version;
  std::vector<Finding> findings;
};

enum class ExitCode : int {
  Success = 0,
  VerificationNegative = 1,
  UsageOrInputError = 2,
  RuntimeFailure = 3,
  UnsignedOrNotVerifiable = 4,
};

ExitCode ExitCodeForVerification(const VerificationResult& result) noexcept;
OverallState DeriveOverallState(const VerificationResult& result) noexcept;

const char* ToString(Codec v) noexcept;
const char* ToString(SigningPresence v) noexcept;
const char* ToString(SignatureIntegrity v) noexcept;
const char* ToString(ContinuityStatus v) noexcept;
const char* ToString(VerificationCompleteness v) noexcept;
const char* ToString(CertificateStatus v) noexcept;
const char* ToString(SourceAuthenticity v) noexcept;
const char* ToString(OverallState v) noexcept;

}  // namespace videotrust
