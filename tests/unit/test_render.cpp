#include "videotrust/render.hpp"
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
  VerificationResult r;
  r.codec = Codec::H264;
  r.media_signing = SigningPresence::Detected;
  r.signature_integrity = SignatureIntegrity::Ok;
  r.continuity = ContinuityStatus::Intact;
  r.completeness = VerificationCompleteness::Complete;
  r.certificate = CertificateStatus::NotProvided;
  r.source_authenticity = SourceAuthenticity::NotEstablished;

  const std::string text = RenderText(r);
  if (text.find("Source Authenticity") == std::string::npos ||
      text.find("not_established") == std::string::npos ||
      text.find("VALID") == std::string::npos) {
    return fail("text render");
  }
  if (text.find("This video is authentic") != std::string::npos) {
    return fail("overclaim text");
  }

  const std::string json = RenderJson(r);
  if (json.find("\"schema_version\":\"0.1\"") == std::string::npos) {
    return fail("json schema");
  }
  if (json.find("\"authentic\"") != std::string::npos) {
    return fail("no authentic boolean");
  }
  if (json.find("source_authenticity\":\"not_established\"") == std::string::npos) {
    return fail("json source authenticity");
  }
  // JSON-only: single object ending with newline, no log prefixes.
  if (json.front() != '{' || json.find('\n') != json.size() - 1) {
    return fail("json must be one object + newline");
  }

  std::cout << "PASS: render\n";
  return 0;
}
