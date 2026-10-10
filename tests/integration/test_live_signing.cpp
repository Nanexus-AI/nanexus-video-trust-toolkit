#include "videotrust/live_signing.hpp"
#include "videotrust/nal_classify.hpp"
#include "videotrust/verify.hpp"
#include "../../src/live/ingest_continuity.hpp"

#include <onvif_media_signing_signer.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

// Test-only fault injection into the official API boundary. Production signing
// remains unchanged; verify terminal orchestration on a real upstream error.
static bool inject_signer_failure = false;
extern "C" MediaSigningReturnCode __real_onvif_media_signing_add_nalu_for_signing(
    onvif_media_signing_t*, const uint8_t*, size_t, int64_t);
extern "C" MediaSigningReturnCode __wrap_onvif_media_signing_add_nalu_for_signing(
    onvif_media_signing_t* session, const uint8_t* nal, size_t size, int64_t timestamp) {
  if (inject_signer_failure) {
    inject_signer_failure = false;
    return OMS_EXTERNAL_ERROR;
  }
  return __real_onvif_media_signing_add_nalu_for_signing(session, nal, size, timestamp);
}

using namespace videotrust;
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)
constexpr int64_t kTimestamp = 133000000000000000LL;

struct Output {
  std::vector<uint8_t> bytes;
  unsigned closes{0};
  SigningState state{SigningState::Active};
};
struct Outputs {
  std::vector<std::shared_ptr<Output>> epochs;
  bool fail_write{false}, fail_close{false};
  SigningEpochSinkFactory Factory() {
    return [this](unsigned id) -> Expected<SigningEpochSink> {
      if (id != epochs.size() + 1) return MakeError(ErrorCode::InternalError, "epoch id");
      auto output = std::make_shared<Output>();
      epochs.push_back(output);
      return SigningEpochSink{
        [this, output](std::span<const uint8_t> bytes) {
          if (fail_write) return MakeError(ErrorCode::IoFailure, "SECRET write");
          output->bytes.insert(output->bytes.end(), bytes.begin(), bytes.end());
          return MakeError(ErrorCode::Ok, {});
        },
        [this, output](SigningState state) {
          ++output->closes;
          if (fail_close) throw std::runtime_error("SECRET close");
          output->state = state;
          return MakeError(ErrorCode::Ok, {});
        }};
    };
  }
};

// The tiny synthetic fixtures have one slice per frame. Group their complete
// compressed AU NALs for testing; production receives depay-aligned AUs.
std::vector<std::vector<NalUnit>> AccessUnits(const std::vector<NalUnit>& nalus, Codec codec) {
  std::vector<std::vector<NalUnit>> units;
  std::vector<NalUnit> current;
  for (const auto& nal : nalus) {
    if (IsVclNal(codec, nal)) {
      current.push_back(nal);
      units.push_back(std::move(current));
      current.clear();
    } else current.push_back(nal);
  }
  if (!current.empty() && !units.empty())
    units.back().insert(units.back().end(), current.begin(), current.end());
  return units;
}

bool Valid(const Output& output, Codec codec, const fs::path& ca) {
  auto nalus = AnnexBReader::Parse(output.bytes);
  auto verifier = MediaSigningSession::Create(codec);
  if (!nalus.ok() || !verifier.ok()) return false;
  if (SetTrustedCertificateFromFile(verifier.value(), ca.string()).code != ErrorCode::Ok) return false;
  auto result = VerifyNalUnits(verifier.value(), codec, nalus.value(), true);
  return result.ok() && DeriveOverallState(result.value()) == OverallState::Valid &&
      result.value().source_authenticity == SourceAuthenticity::NotEstablished;
}

int CheckContinuity() {
  using Guard = videotrust::detail::IngestContinuity;
  Guard normal;
  normal.Observe(Guard::Event::StreamStart);
  normal.Observe(Guard::Event::Caps);
  normal.Observe(Guard::Event::Segment);
  normal.SessionReconfigure(); // Initial SETUP/PLAY is startup, not a reset.
  normal.Rtp(65535, 1, true, false);
  normal.Output(true, false);
  normal.Rtp(0, 1, false, false); // Sequence wrap is continuous.
  normal.Output(false, false);
  CHECK(!normal.lost());
  for (unsigned mode = 0; mode < 13; ++mode) {
    Guard guard;
    guard.Observe(Guard::Event::StreamStart);
    guard.Observe(Guard::Event::Caps);
    guard.Observe(Guard::Event::Segment);
    guard.Rtp(100, 1, true, false);
    guard.Output(true, false);
    switch (mode) {
      case 0: guard.Rtp(102, 1, false, false); break;
      case 1: guard.Rtp(101, 2, false, false); break;
      case 2: guard.Rtp(101, 1, true, false); break;
      case 3: guard.Output(true, false); break;
      case 4: guard.Output(false, true); break;
      case 5: guard.Observe(Guard::Event::Segment); break;
      case 6: guard.Observe(Guard::Event::Caps); break;
      case 7: guard.Observe(Guard::Event::StreamStart); break;
      case 8: guard.Observe(Guard::Event::Flush); break;
      case 9: guard.Observe(Guard::Event::Gap); break;
      case 10: guard.Observe(Guard::Event::SourceRemoved); break;
      case 11: guard.SessionReconfigure(); break;
      case 12: guard.InvalidPacket(); break;
    }
    CHECK(guard.lost());
    guard.Rtp(103, 1, false, false);
    CHECK(guard.lost()); // Loss is latched, never reset inside a run.
  }
  std::cout << "PASS continuity: startup/wrap, sequence/SSRC/flags/segment/caps/flush/source loss\n";
  return 0;
}

int CheckCodec(const fs::path& root, Codec codec) {
  const auto name = codec == Codec::H264 ? "h264" : "h265";
  auto parsed = AnnexBReader::ParseFile((root / name / (std::string("unsigned.") + name)).string());
  CHECK(parsed.ok());
  auto units = AccessUnits(parsed.value(), codec);
  CHECK(units.size() > 10 && SigningAuHasRandomAccess(units[0], codec));
  LiveSigningOptions options;
  options.signer = {codec, (root / "pki/signer.key.pem").string(),
      (root / "pki/signer-chain.pem").string()};
  options.signer.max_signing_frames = 5;
  const auto ca = root / "pki/ca.pem";

  for (auto end : {SigningIngestKind::CleanEos, SigningIngestKind::BoundedStop}) {
    Outputs output;
    auto gateway = LiveSigningOrchestrator::Create(options, output.Factory());
    CHECK(gateway.ok());
    for (const auto& au : units)
      CHECK(gateway.value()->Consume({SigningIngestKind::Media, au, kTimestamp}).code == ErrorCode::Ok);
    CHECK(gateway.value()->Consume({end}).code == ErrorCode::Ok);
    CHECK(output.epochs.size() == 1 && output.epochs[0]->closes == 1);
    CHECK(output.epochs[0]->state == SigningState::Finalized);
    CHECK(Valid(*output.epochs[0], codec, ca));
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code != ErrorCode::Ok);
  }

  Outputs gap_output;
  auto gap = LiveSigningOrchestrator::Create(options, gap_output.Factory());
  CHECK(gap.ok());
  CHECK(gap.value()->Consume({SigningIngestKind::Media, units[1], kTimestamp}).code == ErrorCode::Ok);
  CHECK(gap_output.epochs.empty()); // Joining mid-GOP does not start signing.
  for (unsigned i = 0; i < 10; ++i)
    CHECK(gap.value()->Consume({SigningIngestKind::Media, units[i], kTimestamp}).code == ErrorCode::Ok);
  const auto prefix = gap_output.epochs[0]->bytes;
  CHECK(gap.value()->Consume({SigningIngestKind::Discontinuity}).code == ErrorCode::Ok);
  CHECK(gap_output.epochs[0]->bytes == prefix); // No EOS SEI on a gap.
  CHECK(gap_output.epochs[0]->state == SigningState::Aborted);
  CHECK(gap_output.epochs[0]->closes == 1);
  CHECK(gap.value()->Consume({SigningIngestKind::Media, units[1], kTimestamp}).code == ErrorCode::Ok);
  CHECK(gap_output.epochs.size() == 1); // Wait for a new IDR/IRAP.
  for (const auto& au : units)
    CHECK(gap.value()->Consume({SigningIngestKind::Media, au, kTimestamp}).code == ErrorCode::Ok);
  CHECK(gap.value()->Consume({SigningIngestKind::CleanEos}).code == ErrorCode::Ok);
  CHECK(gap_output.epochs.size() == 2);
  CHECK(gap_output.epochs[0]->bytes == prefix); // This fails if media B bridges A.
  CHECK(gap_output.epochs[1]->state == SigningState::Finalized && gap_output.epochs[1]->closes == 1);
  CHECK(Valid(*gap_output.epochs[1], codec, ca)); // Fresh independent signing state.
  CHECK(gap.value()->result().epochs[0].epoch == 1 && gap.value()->result().epochs[1].epoch == 2);

  for (auto loss : {SigningIngestKind::Abort, SigningIngestKind::Failure}) {
    Outputs output;
    auto gateway = LiveSigningOrchestrator::Create(options, output.Factory());
    CHECK(gateway.ok());
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code == ErrorCode::Ok);
    const auto before = output.epochs[0]->bytes;
    gateway.value()->Consume({loss});
    CHECK(output.epochs[0]->state == SigningState::Aborted && output.epochs[0]->bytes == before);
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code != ErrorCode::Ok);
    CHECK(output.epochs[0]->closes == 1);
  }

  for (unsigned mode = 0; mode < 7; ++mode) {
    Outputs output;
    auto changed = options;
    if (mode == 0) changed.signer.key_pem_path = (root / "missing.pem").string();
    if (mode == 1) output.fail_write = true;
    if (mode == 2) output.fail_close = true;
    if (mode == 3) changed.max_output_bytes = 1;
    if (mode == 4) changed.signer.max_output_bytes = 1;
    auto gateway = LiveSigningOrchestrator::Create(changed, output.Factory());
    CHECK(gateway.ok());
    auto unit = units[0];
    if (mode == 5) unit.front().bytes = {0, 0, 1}; // Input failure after epoch setup.
    if (mode == 6) inject_signer_failure = true;
    auto error = gateway.value()->Consume({SigningIngestKind::Media, unit, kTimestamp});
    if (mode == 2) error = gateway.value()->Consume({SigningIngestKind::CleanEos});
    CHECK(error.code != ErrorCode::Ok);
    CHECK(gateway.value()->result().end == GatewayEnd::Failed);
    if (mode == 6) CHECK(gateway.value()->result().failure == GatewayFailure::Signer);
    CHECK(gateway.value()->result().error.message.find("SECRET") == std::string::npos);
    CHECK(gateway.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code != ErrorCode::Ok);
    CHECK(output.epochs.size() == 1 && output.epochs[0]->closes == 1);
  }
  auto bounded = options;
  bounded.max_epochs = 1;
  Outputs bounded_output;
  auto limited = LiveSigningOrchestrator::Create(bounded, bounded_output.Factory());
  CHECK(limited.ok());
  CHECK(limited.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code == ErrorCode::Ok);
  CHECK(limited.value()->Consume({SigningIngestKind::Discontinuity}).code == ErrorCode::Ok);
  CHECK(limited.value()->Consume({SigningIngestKind::Media, units[0], kTimestamp}).code != ErrorCode::Ok);
  CHECK(bounded_output.epochs.size() == 1 && limited.value()->result().failure == GatewayFailure::Resource);
  std::cout << "PASS " << name << ": continuous/EOS/bounded stop VALID; gap abort -> separate VALID restart; "
               "no bridging; abort/identity/input/output/signer/close/epoch bounds\n";
  return 0;
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  if (CheckContinuity() || CheckCodec(argv[1], Codec::H264)) return 1;
  return CheckCodec(argv[1], Codec::H265);
}
