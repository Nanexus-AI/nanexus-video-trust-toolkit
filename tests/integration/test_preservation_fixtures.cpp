#include "videotrust/preservation.hpp"
#include "videotrust/verify.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

int CheckCodec(Codec codec, const fs::path& root) {
  const std::string name = codec == Codec::H264 ? "h264" : "h265";
  const std::string ext = "." + name;
  const fs::path signed_path = root / name / ("signed" + ext);
  const fs::path stripped_path =
      root / name / ("correlation-stripped" + ext);
  const fs::path truncated_path =
      root / name / "tamper-work" / ("trunc" + ext);
  const fs::path corrupt_path =
      root / name / "tamper-work" / ("corrupt" + ext);

  auto inspect = [&](const fs::path& path) {
    VerifyOptions options;
    options.codec = codec;
    options.input_path = path.string();
    options.ca_pem_path = (root / "pki/ca.pem").string();
    return InspectAnnexBFile(options);
  };
  auto correlate = [&](const fs::path& after_path) {
    CorrelationOptions options;
    options.codec = codec;
    options.before_path = signed_path.string();
    options.after_path = after_path.string();
    return CorrelateAnnexBFiles(options);
  };

  auto before = inspect(signed_path);
  if (!before.ok()) return Fail(name + " before inspection");

  auto assess = [&](const fs::path& path) {
    auto after = inspect(path);
    auto evidence = correlate(path);
    if (!after.ok() || !evidence.ok()) {
      throw std::runtime_error(name + " fixture assessment input failed");
    }
    return DerivePreservationAssessment(before.value(), after.value(),
                                        evidence.value());
  };

  try {
    const auto exact = assess(signed_path);
    if (exact.source_coverage != SourceCoverage::Full ||
        exact.media_signing_preservation !=
            MediaSigningPreservation::Preserved) {
      return Fail(name + " measured exact signed outcome");
    }

    const auto stripped = assess(stripped_path);
    if (stripped.media_signing_preservation !=
            MediaSigningPreservation::NotPreserved ||
        stripped.signing_metadata_relation !=
            SigningMetadataRelation::MissingAfter) {
      return Fail(name + " measured signing-SEI strip outcome");
    }

    const auto truncated = assess(truncated_path);
    if (truncated.source_coverage != SourceCoverage::Subset ||
        truncated.media_signing_preservation !=
            MediaSigningPreservation::PartiallyPreserved ||
        truncated.after.verification.completeness !=
            VerificationCompleteness::Incomplete) {
      return Fail(name + " measured truncated boundary outcome: coverage=" +
                  ToString(truncated.source_coverage) + " preservation=" +
                  ToString(truncated.media_signing_preservation) + " sequence=" +
                  ToString(truncated.correlation.nal.sequence_equivalent) +
                  " subsequence=" +
                  ToString(truncated.correlation.nal.after_is_ordered_subsequence) +
                  " unique=" +
                  ToString(truncated.correlation.nal.unique_subsequence_alignment) +
                  " signing=" +
                  std::to_string(truncated.correlation.signing_sei.before_count) +
                  "/" +
                  std::to_string(truncated.correlation.signing_sei.after_count) +
                  " matched=" +
                  std::to_string(
                      truncated.correlation.signing_sei.matched_payload_count) +
                  " missing=" +
                  std::to_string(
                      truncated.correlation.signing_sei.missing_from_after_count));
    }

    const auto corrupt = assess(corrupt_path);
    if (corrupt.media_signing_preservation !=
            MediaSigningPreservation::NotPreserved ||
        corrupt.after.verification.signature_integrity !=
            SignatureIntegrity::NotOk) {
      return Fail(name + " measured corruption outcome");
    }
  } catch (const std::exception& error) {
    return Fail(error.what());
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return Fail("usage: test_preservation_fixtures FIXTURE_ROOT");
  const fs::path root(argv[1]);
  if (const int rc = CheckCodec(Codec::H264, root); rc != 0) return rc;
  if (const int rc = CheckCodec(Codec::H265, root); rc != 0) return rc;
  std::cout << "PASS: measured preservation fixtures (H.264 + H.265)\n";
  return 0;
}
