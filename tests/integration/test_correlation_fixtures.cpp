#include "videotrust/correlation.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

int CheckCodec(videotrust::Codec codec,
               const fs::path& signed_path,
               const fs::path& stripped_path) {
  videotrust::CorrelationOptions exact_options;
  exact_options.codec = codec;
  exact_options.before_path = signed_path.string();
  exact_options.after_path = signed_path.string();
  auto exact = videotrust::CorrelateAnnexBFiles(exact_options);
  if (!exact.ok()) {
    return Fail("official signed exact scan: " + exact.error().message);
  }
  if (exact.value().before.signing_sei_count == 0 ||
      exact.value().byte_identical != videotrust::EvidenceState::Yes ||
      exact.value().nal.sequence_equivalent != videotrust::EvidenceState::Yes ||
      exact.value().signing_sei.correlation_complete !=
          videotrust::EvidenceState::Yes ||
      exact.value().signing_sei.missing_from_after_count != 0) {
    return Fail("official signed exact evidence");
  }

  videotrust::CorrelationOptions strip_options;
  strip_options.codec = codec;
  strip_options.before_path = signed_path.string();
  strip_options.after_path = stripped_path.string();
  auto strip = videotrust::CorrelateAnnexBFiles(strip_options);
  if (!strip.ok()) {
    return Fail("official signed strip scan: " + strip.error().message);
  }
  if (strip.value().before.signing_sei_count == 0 ||
      strip.value().after.signing_sei_count != 0 ||
      strip.value().signing_sei.missing_from_after_count !=
          strip.value().before.signing_sei_count ||
      strip.value().nal.after_is_ordered_subsequence !=
          videotrust::EvidenceState::Yes ||
      strip.value().nal.unmatched_before_roles.signing_sei !=
          strip.value().before.signing_sei_count) {
    return Fail("official signed strip evidence");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    return Fail("usage: test_correlation_fixtures FIXTURE_ROOT");
  }
  const fs::path root(argv[1]);
  if (const int rc = CheckCodec(videotrust::Codec::H264,
                                root / "h264/signed.h264",
                                root / "h264/correlation-stripped.h264");
      rc != 0) {
    return rc;
  }
  if (const int rc = CheckCodec(videotrust::Codec::H265,
                                root / "h265/signed.h265",
                                root / "h265/correlation-stripped.h265");
      rc != 0) {
    return rc;
  }
  std::cout << "PASS: official signing-SEI correlation fixtures (H.264 + H.265)\n";
  return 0;
}
