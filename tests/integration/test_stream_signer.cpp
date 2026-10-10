#include "videotrust/stream_signer.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/verify.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace videotrust;
namespace fs = std::filesystem;

#define REQUIRE(condition) do { if (!(condition)) { \
  std::cerr << "FAIL line " << __LINE__ << ": " << #condition << '\n'; return 1; } } while (false)

int CheckCodec(const fs::path& root, Codec codec) {
  const std::string name = codec == Codec::H264 ? "h264" : "h265";
  const auto input = root / name / ("unsigned." + name);
  auto nalus = AnnexBReader::ParseFile(input.string());
  REQUIRE(nalus.ok());
  StreamSigningOptions options{codec, (root / "pki/signer.key.pem").string(),
      (root / "pki/signer-chain.pem").string()};
  // Force partial signing units even without an intra-frame boundary.
  options.max_signing_frames = 5;
  std::vector<uint8_t> output;
  auto sink = [&output](std::span<const uint8_t> bytes) {
    output.insert(output.end(), bytes.begin(), bytes.end());
    return MakeError(ErrorCode::Ok, {});
  };
  auto signer = StreamSigner::Create(options, sink);
  REQUIRE(signer.ok());
  constexpr int64_t timestamp = 133000000000000000LL;
  for (const auto& nal : nalus.value())
    REQUIRE(signer.value().AddNal(nal, timestamp).code == ErrorCode::Ok);
  REQUIRE(!output.empty()); // Egress occurs before finalization, not file buffering.
  auto incremental_output = AnnexBReader::Parse(output);
  REQUIRE(incremental_output.ok());
  bool saw_incremental_signature = false;
  for (const auto& nal : incremental_output.value())
    saw_incremental_signature |= IsOnvifMediaSigningSei(codec, nal);
  REQUIRE(saw_incremental_signature); // Signing metadata also arrives before EOS.
  REQUIRE(signer.value().Finalize().code == ErrorCode::Ok);
  REQUIRE(signer.value().state() == SigningState::Finalized);
  REQUIRE(signer.value().failure() == SigningFailure::None);
  REQUIRE(signer.value().input_nalus() == nalus.value().size());
  REQUIRE(signer.value().output_bytes() == output.size());
  REQUIRE(signer.value().Finalize().code != ErrorCode::Ok);
  REQUIRE(signer.value().AddNal(nalus.value()[0], timestamp).code != ErrorCode::Ok);

  const auto path = root / name / ("gateway." + name);
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(output.data()), output.size());
    REQUIRE(file.good());
  }
  VerifyOptions verify{codec, path.string(), (root / "pki/ca.pem").string()};
  auto result = VerifyAnnexBFile(verify);
  REQUIRE(result.ok());
  REQUIRE(DeriveOverallState(result.value()) == OverallState::Valid);
  REQUIRE(result.value().certificate == CertificateStatus::Ok);
  REQUIRE(result.value().source_authenticity == SourceAuthenticity::NotEstablished);
  auto signed_nalus = AnnexBReader::Parse(output);
  REQUIRE(signed_nalus.ok());
  std::size_t original = 0, metadata = 0;
  NalUnit signing_sei;
  for (const auto& nal : signed_nalus.value()) {
    if (IsOnvifMediaSigningSei(codec, nal)) { ++metadata; signing_sei = nal; continue; }
    REQUIRE(original < nalus.value().size());
    REQUIRE(nal.bytes == nalus.value()[original++].bytes);
  }
  REQUIRE(metadata > 0 && original == nalus.value().size());
  std::cout << "PASS " << name << " incremental sign -> existing verifier VALID; "
            << "certificate Ok; source not_established; compressed bytes preserved\n";

  // Independently tamper emitted output; existing verifier must reject it.
  auto changed = signed_nalus.value();
  bool mutated = false;
  for (auto& nal : changed) {
    std::size_t offset = 0;
    if (VclMutationOffset(codec, nal, &offset)) {
      nal.bytes[offset] ^= 0x80;
      mutated = true;
      break;
    }
  }
  REQUIRE(mutated);
  auto validator = MediaSigningSession::Create(codec);
  REQUIRE(validator.ok());
  REQUIRE(SetTrustedCertificateFromFile(validator.value(), verify.ca_pem_path).code == ErrorCode::Ok);
  auto negative = VerifyNalUnits(validator.value(), codec, changed, true);
  REQUIRE(negative.ok());
  REQUIRE(DeriveOverallState(negative.value()) == OverallState::Invalid);

  // Missing, malformed, oversized and mismatched identity never emit output.
  auto bad = options;
  unsigned emissions = 0;
  auto counting = [&emissions](std::span<const uint8_t>) {
    ++emissions; return MakeError(ErrorCode::Ok, {});
  };
  bad.key_pem_path = (root / "missing-key.pem").string();
  REQUIRE(!StreamSigner::Create(bad, counting).ok());
  const auto bad_key = root / "invalid-key.pem";
  { std::ofstream file(bad_key); file << "SECRET-MARKER-not-a-key"; }
  bad.key_pem_path = bad_key.string();
  auto malformed = StreamSigner::Create(bad, counting);
  REQUIRE(!malformed.ok());
  REQUIRE(malformed.error().message.find("SECRET-MARKER") == std::string::npos);
  { std::ofstream file(bad_key); file << std::string(kSigningMaxPemBytes + 1, 'x'); }
  REQUIRE(!StreamSigner::Create(bad, counting).ok());
  fs::remove(bad_key);
  bad.key_pem_path = (root / "pki/ca.key.pem").string();
  REQUIRE(!StreamSigner::Create(bad, counting).ok());
  REQUIRE(emissions == 0);
  bad = options;
  bad.max_signing_frames = 0;
  REQUIRE(!StreamSigner::Create(bad, counting).ok());
  bad = options;
  bad.max_output_bytes = kSigningMaxOutputBytes + 1;
  REQUIRE(!StreamSigner::Create(bad, counting).ok());
  REQUIRE(!StreamSigner::Create(options, {}).ok());

  // Explicit abort emits no trailing metadata and requires a fresh epoch.
  auto aborted = StreamSigner::Create(options, counting);
  REQUIRE(aborted.ok());
  REQUIRE(aborted.value().AddNal(nalus.value()[0], timestamp).code == ErrorCode::Ok);
  const auto before_abort = emissions;
  aborted.value().Abort();
  REQUIRE(aborted.value().state() == SigningState::Aborted);
  REQUIRE(aborted.value().Finalize().code != ErrorCode::Ok);
  REQUIRE(aborted.value().AddNal(nalus.value()[0], timestamp).code != ErrorCode::Ok);
  REQUIRE(emissions == before_abort);
  output.clear();
  auto restarted = StreamSigner::Create(options, sink);
  REQUIRE(restarted.ok());
  for (const auto& nal : nalus.value())
    REQUIRE(restarted.value().AddNal(nal, timestamp).code == ErrorCode::Ok);
  REQUIRE(restarted.value().Finalize().code == ErrorCode::Ok);
  auto restart_nalus = AnnexBReader::Parse(output);
  REQUIRE(restart_nalus.ok());
  auto restart_validator = MediaSigningSession::Create(codec);
  REQUIRE(restart_validator.ok());
  REQUIRE(SetTrustedCertificateFromFile(restart_validator.value(), verify.ca_pem_path).code == ErrorCode::Ok);
  auto restart_result = VerifyNalUnits(restart_validator.value(), codec, restart_nalus.value(), true);
  REQUIRE(restart_result.ok());
  REQUIRE(DeriveOverallState(restart_result.value()) == OverallState::Valid);
  // Official signatures/metadata need not be byte-identical between sessions.


  for (unsigned mode = 0; mode < 6; ++mode) {
    auto failed = StreamSigner::Create(options, counting);
    REQUIRE(failed.ok());
    auto nal = nalus.value()[0];
    if (mode == 0) nal.start_code_size = 1;
    if (mode == 1) nal.bytes.assign(kSigningMaxNalBytes + 1, 0);
    if (mode == 2) nal = signing_sei;
    if (mode == 3) nal.bytes = {0, 0, 1};
    if (mode == 5) nal.bytes.resize(nal.start_code_size + NalHeaderLength(codec));
    const auto e = failed.value().AddNal(nal, mode == 4 ? -1 : timestamp);
    REQUIRE(e.code != ErrorCode::Ok);
    REQUIRE(failed.value().state() == SigningState::Failed);
    REQUIRE(failed.value().failure() == SigningFailure::Input);
    REQUIRE(failed.value().Finalize().code != ErrorCode::Ok);
    REQUIRE(failed.value().AddNal(nalus.value()[0], timestamp).code != ErrorCode::Ok);
  }
  auto empty = StreamSigner::Create(options, counting);
  REQUIRE(empty.ok());
  REQUIRE(empty.value().Finalize().code != ErrorCode::Ok);
  REQUIRE(empty.value().failure() == SigningFailure::Input);

  for (bool throws : {false, true}) {
    auto failed = StreamSigner::Create(options, [throws](std::span<const uint8_t>) -> Error {
      if (throws) throw std::runtime_error("SECRET sink diagnostic");
      return MakeError(ErrorCode::IoFailure, "SECRET sink diagnostic");
    });
    REQUIRE(failed.ok());
    auto e = failed.value().AddNal(nalus.value()[0], timestamp);
    REQUIRE(e.code == ErrorCode::IoFailure);
    REQUIRE(e.message.find("SECRET") == std::string::npos);
    REQUIRE(failed.value().failure() == SigningFailure::Output);
    REQUIRE(failed.value().Finalize().code != ErrorCode::Ok);
  }
  bool reject_final = false;
  auto final_failure = StreamSigner::Create(options,
      [&reject_final](std::span<const uint8_t>) {
        return MakeError(reject_final ? ErrorCode::IoFailure : ErrorCode::Ok, {});
      });
  REQUIRE(final_failure.ok());
  for (const auto& nal : nalus.value())
    REQUIRE(final_failure.value().AddNal(nal, timestamp).code == ErrorCode::Ok);
  reject_final = true;
  REQUIRE(final_failure.value().Finalize().code == ErrorCode::IoFailure);
  REQUIRE(final_failure.value().state() == SigningState::Failed);
  REQUIRE(final_failure.value().failure() == SigningFailure::Output);
  REQUIRE(final_failure.value().Finalize().code != ErrorCode::Ok);
  auto bounded = options;
  bounded.max_output_bytes = 1;
  auto overflow = StreamSigner::Create(bounded, counting);
  REQUIRE(overflow.ok());
  REQUIRE(overflow.value().AddNal(nalus.value()[0], timestamp).code != ErrorCode::Ok);
  REQUIRE(overflow.value().failure() == SigningFailure::Output);
  REQUIRE(overflow.value().output_bytes() == 0);
  std::cout << "PASS " << name << " tamper, identity, abort/restart, input and sink failures\n";
  return 0;
}

int main(int argc, char** argv) {
  REQUIRE(argc == 2);
  if (CheckCodec(argv[1], Codec::H264)) return 1;
  return CheckCodec(argv[1], Codec::H265);
}
