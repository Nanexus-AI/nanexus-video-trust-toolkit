#include "videotrust/live_signing.hpp"
#include "videotrust/verify.hpp"
#include <filesystem>
#include <iostream>
#include <chrono>

using namespace videotrust;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)
int main(int argc, char** argv) {
  CHECK(argc == 5);
  const std::filesystem::path root(argv[1]);
  const std::string codec_name(argv[2]), mode(argv[4]);
  const auto codec = codec_name == "h264" ? Codec::H264 : Codec::H265;
  LiveIngestOptions ingest{codec, argv[3], {}, {}, 2};
  if (mode.starts_with("passive-")) {
    unsigned nal_count = 0;
    auto passive = RunGStreamerLiveIngest(ingest, [&](const NalUnit&) {
      ++nal_count; return MakeError(ErrorCode::Ok, {});
    }, [] { return false; });
    CHECK(passive.error.code == ErrorCode::Ok && passive.stop_reason == LiveStopReason::DeadlineReached);
    CHECK(nal_count > 0);
    std::cout << "PASS " << codec_name << " M4 " << mode << ": frozen passive stop behavior retained\n";
    return 0;
  }
  LiveSigningOptions signing;
  signing.signer = {codec, (root / "pki/signer.key.pem").string(),
      (root / "pki/signer-chain.pem").string()};
  signing.signer.max_signing_frames = 5;
  signing.max_output_bytes = 1024 * 1024;
  std::vector<std::vector<uint8_t>> outputs;
  unsigned closes = 0;
  SigningState closed = SigningState::Active;
  auto factory = [&](unsigned epoch) -> Expected<SigningEpochSink> {
    if (epoch != outputs.size() + 1) return MakeError(ErrorCode::InternalError, "epoch mismatch");
    outputs.emplace_back();
    const auto index = outputs.size() - 1;
    return SigningEpochSink{[&, index](std::span<const uint8_t> bytes) {
      outputs[index].insert(outputs[index].end(), bytes.begin(), bytes.end());
      return MakeError(ErrorCode::Ok, {});
    }, [&](SigningState state) {
      ++closes; closed = state; return MakeError(ErrorCode::Ok, {});
    }};
  };
  const auto start = std::chrono::steady_clock::now();
  auto result = RunGStreamerLiveSigning(ingest, signing, factory, [&]() {
    return mode == "cancel" && std::chrono::steady_clock::now() - start > std::chrono::seconds(1);
  });
  CHECK(outputs.size() == 1 && closes == 1 && result.epochs.size() == 1);
  if (mode == "continuous") {
    CHECK(result.end == GatewayEnd::BoundedStop && result.error.code == ErrorCode::Ok);
    CHECK(closed == SigningState::Finalized);
    auto nalus = AnnexBReader::Parse(outputs[0]);
    CHECK(nalus.ok());
    auto verifier = MediaSigningSession::Create(codec);
    CHECK(verifier.ok());
    CHECK(SetTrustedCertificateFromFile(verifier.value(), (root / "pki/ca.pem").string()).code == ErrorCode::Ok);
    auto verification = VerifyNalUnits(verifier.value(), codec, nalus.value(), true);
    CHECK(verification.ok());
    CHECK(DeriveOverallState(verification.value()) == OverallState::Valid);
    CHECK(verification.value().source_authenticity == SourceAuthenticity::NotEstablished);
    std::cout << "PASS " << codec_name << " RTSP/TCP -> depay -> signer -> existing verifier VALID\n";
  } else {
    CHECK(closed == SigningState::Aborted);
    CHECK(result.end == (mode == "cancel" ? GatewayEnd::Aborted :
        mode == "gap" ? GatewayEnd::ContinuityLost : GatewayEnd::Failed));
    if (mode != "cancel") CHECK(result.failure ==
        (mode == "gap" ? GatewayFailure::Continuity : GatewayFailure::Transport));
    std::cout << "PASS " << codec_name << " RTSP " << mode << ": aborted epoch; no clean finalization or bridging\n";
  }
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(5));
  return 0;
}
