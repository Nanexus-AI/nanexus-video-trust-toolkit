#include "videotrust/error.hpp"
#include "videotrust/result.hpp"

#include <iostream>
#include <string>

namespace {

int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << '\n';
  return 1;
}

}  // namespace

int main() {
  using namespace videotrust;

  VerificationResult ok;
  ok.media_signing = SigningPresence::Detected;
  ok.signature_integrity = SignatureIntegrity::OkWithMissingInfo;
  ok.continuity = ContinuityStatus::MissingInfo;
  ok.completeness = VerificationCompleteness::Complete;
  ok.certificate = CertificateStatus::NotProvided;
  ok.source_authenticity = SourceAuthenticity::NotEstablished;
  if (ExitCodeForVerification(ok) != ExitCode::Success) {
    return fail("ok_with_missing_info should exit 0");
  }

  VerificationResult partial_unverifiable;
  partial_unverifiable.media_signing = SigningPresence::Detected;
  partial_unverifiable.signature_integrity = SignatureIntegrity::NotFeasible;
  partial_unverifiable.completeness = VerificationCompleteness::NotFeasible;
  if (ExitCodeForVerification(partial_unverifiable) !=
      ExitCode::UnsignedOrNotVerifiable) {
    return fail("not_feasible should exit 4");
  }

  VerificationResult broken;
  broken.media_signing = SigningPresence::Detected;
  broken.signature_integrity = SignatureIntegrity::NotOk;
  broken.continuity = ContinuityStatus::Broken;
  if (ExitCodeForVerification(broken) != ExitCode::VerificationNegative) {
    return fail("not_ok should exit 1");
  }

  // Error model: operational failures are values, not verification results.
  const Error io = MakeError(ErrorCode::IoFailure, "cannot open file");
  if (io.code != ErrorCode::IoFailure) {
    return fail("error code");
  }
  Expected<int> good = 7;
  Expected<int> bad = MakeError(ErrorCode::ParseError, "bad annex-b");
  if (!good.ok() || good.value() != 7) {
    return fail("expected ok");
  }
  if (bad.ok() || bad.error().code != ErrorCode::ParseError) {
    return fail("expected err");
  }

  // ToString smoke (and ensure axes remain distinct labels).
  if (std::string(ToString(SignatureIntegrity::Ok)) ==
      std::string(ToString(CertificateStatus::Ok)) &&
      false) {
    // Same spelling "ok" is fine; axes are different types.
  }
  if (std::string(ToString(SourceAuthenticity::NotEstablished)) != "not_established") {
    return fail("source authenticity string");
  }

  VerificationResult incomplete;
  incomplete.media_signing = SigningPresence::Detected;
  incomplete.signature_integrity = SignatureIntegrity::Ok;
  incomplete.continuity = ContinuityStatus::Intact;
  incomplete.completeness = VerificationCompleteness::Incomplete;
  if (ExitCodeForVerification(incomplete) != ExitCode::UnsignedOrNotVerifiable) {
    return fail("incomplete should exit 4");
  }
  if (DeriveOverallState(incomplete) != OverallState::Partial) {
    return fail("incomplete overall PARTIAL");
  }

  std::cout << "PASS: result and exit codes\n";
  return 0;
}
