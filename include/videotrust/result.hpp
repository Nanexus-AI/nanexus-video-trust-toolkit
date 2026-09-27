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
  NotApplicable,      ///< No Media Signing detected (unsigned).
  NotFeasible,        ///< Signing detected but validation not yet feasible.
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

/// Maps upstream MediaSigningProvenanceResult (certificate / trust-anchor axis).
enum class CertificateStatus {
  NotProvided,             ///< Caller supplied no trust anchor to Nanexus.
  NotFeasible,             ///< Upstream could not establish provenance.
  NotOk,
  Ok,
  FeasibleWithoutTrusted,  ///< Upstream allowed provenance without trusted CA (warn).
};

/// Device/source identity is out of M1 lab scope; default remains NotEstablished.
enum class SourceAuthenticity {
  NotEstablished,
  ProvenanceOk,
  ProvenanceNotOk,
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

/// Future CLI exit-code contract (frozen for M1; CLI not implemented in this slice).
enum class ExitCode : int {
  Success = 0,              ///< Operation OK / verification positive.
  VerificationNegative = 1, ///< Completed verify; integrity/authenticity negative.
  UsageOrInputError = 2,    ///< CLI/unsupported/malformed/parse/input error.
  RuntimeFailure = 3,       ///< Internal/upstream/runtime failure.
  UnsignedOrNotVerifiable = 4, ///< Completed verify; unsigned or not verifiable.
};

/// Map a completed VerificationResult to the frozen exit-code contract.
ExitCode ExitCodeForVerification(const VerificationResult& result) noexcept;

const char* ToString(Codec v) noexcept;
const char* ToString(SigningPresence v) noexcept;
const char* ToString(SignatureIntegrity v) noexcept;
const char* ToString(ContinuityStatus v) noexcept;
const char* ToString(VerificationCompleteness v) noexcept;
const char* ToString(CertificateStatus v) noexcept;
const char* ToString(SourceAuthenticity v) noexcept;

}  // namespace videotrust
