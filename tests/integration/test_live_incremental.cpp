#include "videotrust/annexb.hpp"
#include "videotrust/live.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

void PrintObservation(const char* label, const LiveObservation& value) {
  std::cout << label << " sequence=" << value.sequence
            << " epoch=" << value.epoch
            << " kind=" << ToString(value.kind)
            << " tail=" << ToString(value.tail)
            << " stop=" << ToString(value.stop_reason)
            << " received=" << value.accumulated.received_nalus
            << " validated=" << value.accumulated.validated_nalus
            << " pending=" << value.accumulated.pending_nalus
            << " closed="
            << (value.newly_closed ? ToString(value.newly_closed->outcome)
                                   : "none")
            << '\n';
}

int CheckSigned(const fs::path& path, Codec codec, const char* label) {
  auto nalus = AnnexBReader::ParseFile(path.string());
  if (!nalus.ok()) return Fail(std::string(label) + " parse");
  auto validator = IncrementalLiveValidator::Create(codec);
  if (!validator.ok()) return Fail(std::string(label) + " create");
  const auto trust = validator.value().SetTrustedCertificateFile(
      (path.parent_path().parent_path() / "pki/ca.pem").string());
  if (trust.code != ErrorCode::Ok) {
    return Fail(std::string(label) + " trust anchor");
  }

  std::size_t observations = 1;
  PrintObservation(label, validator.value().epoch_started());
  bool saw_open_pending = false;
  bool saw_valid_with_pending = false;
  bool saw_pending_resolve = false;
  for (const auto& nal : nalus.value()) {
    auto event = validator.value().AddNal(nal);
    if (!event.ok()) return Fail(std::string(label) + " add NAL");
    if (!event.value()) continue;
    ++observations;
    PrintObservation(label, *event.value());
    saw_open_pending |= event.value()->tail == LiveTailState::OpenPending;
    saw_valid_with_pending |=
        event.value()->tail == LiveTailState::OpenPending &&
        event.value()->newly_closed.has_value() &&
        event.value()->newly_closed->outcome == ClosedEvidenceOutcome::Valid;
    saw_pending_resolve |= event.value()->tail == LiveTailState::NoPendingTail;
  }
  auto final = validator.value().EndEpoch(LiveStopReason::OrderlyEnd);
  if (!final.ok()) return Fail(std::string(label) + " finalize");
  observations += final.value().size();
  for (const auto& event : final.value()) PrintObservation(label, event);
  const auto& semantic = validator.value().semantic();
  if (!saw_open_pending || !saw_valid_with_pending || !saw_pending_resolve ||
      semantic.closed_summary().valid == 0 ||
      !semantic.latest_closed().has_value() ||
      semantic.latest_closed()->verification.certificate !=
          CertificateStatus::Ok ||
      semantic.tail_state() != LiveTailState::NoPendingTail ||
      semantic.stop_reason() != LiveStopReason::OrderlyEnd) {
    return Fail(std::string(label) + " incremental transitions");
  }
  if (observations >= nalus.value().size()) {
    return Fail(std::string(label) + " observations not material-only");
  }
  std::cout << label << " nalus=" << nalus.value().size()
            << " observations=" << observations
            << " closed_valid=" << semantic.closed_summary().valid << '\n';
  return 0;
}

int CheckUnresolved(const fs::path& path, Codec codec, const char* label) {
  auto nalus = AnnexBReader::ParseFile(path.string());
  if (!nalus.ok()) return Fail(std::string(label) + " parse");
  auto validator = IncrementalLiveValidator::Create(codec);
  if (!validator.ok()) return Fail(std::string(label) + " create");
  bool saw_valid = false;
  bool saw_open_after_valid = false;
  for (const auto& nal : nalus.value()) {
    auto event = validator.value().AddNal(nal);
    if (!event.ok()) return Fail(std::string(label) + " add NAL");
    saw_valid |= validator.value().semantic().closed_summary().valid > 0;
    if (saw_valid && validator.value().semantic().tail_state() ==
                         LiveTailState::OpenPending) {
      saw_open_after_valid = true;
      break;
    }
  }
  if (!saw_open_after_valid) {
    return Fail(std::string(label) + " no valid-plus-pending point");
  }
  auto final = validator.value().EndEpoch(LiveStopReason::TransportFailure);
  if (!final.ok() ||
      validator.value().semantic().tail_state() !=
          LiveTailState::EndedWithUnresolvedTail ||
      validator.value().semantic().closed_summary().valid == 0 ||
      validator.value().semantic().stop_reason() !=
          LiveStopReason::TransportFailure) {
    return Fail(std::string(label) + " final unresolved semantics");
  }
  return 0;
}

int CheckUnsigned(const fs::path& path, Codec codec, const char* label) {
  auto nalus = AnnexBReader::ParseFile(path.string());
  if (!nalus.ok()) return Fail(std::string(label) + " parse");
  auto validator = IncrementalLiveValidator::Create(codec);
  if (!validator.ok()) return Fail(std::string(label) + " create");
  for (const auto& nal : nalus.value()) {
    auto event = validator.value().AddNal(nal);
    if (!event.ok()) return Fail(std::string(label) + " add NAL");
    if (validator.value().semantic().closed_summary().unsigned_stream != 0) {
      return Fail(std::string(label) + " unsigned asserted before finalization");
    }
  }
  auto final = validator.value().EndEpoch(LiveStopReason::OrderlyEnd);
  if (!final.ok() ||
      validator.value().semantic().closed_summary().unsigned_stream != 1 ||
      validator.value().semantic().tail_state() !=
          LiveTailState::EndedWithUnresolvedTail) {
    return Fail(std::string(label) + " final unsigned");
  }
  return 0;
}

int CheckInvalid(const fs::path& path, Codec codec, const char* label) {
  auto nalus = AnnexBReader::ParseFile(path.string());
  if (!nalus.ok()) return Fail(std::string(label) + " parse");
  auto validator = IncrementalLiveValidator::Create(codec);
  if (!validator.ok()) return Fail(std::string(label) + " create");
  for (const auto& nal : nalus.value()) {
    auto event = validator.value().AddNal(nal);
    if (!event.ok()) return Fail(std::string(label) + " add NAL");
  }
  auto final = validator.value().EndEpoch(LiveStopReason::DeadlineReached);
  if (!final.ok() || validator.value().semantic().closed_summary().invalid == 0 ||
      validator.value().semantic().stop_reason() !=
          LiveStopReason::DeadlineReached) {
    return Fail(std::string(label) + " definitive invalid semantics");
  }
  return 0;
}

int CheckUndefinedTlvRejected(const fs::path& path) {
  auto nalus = AnnexBReader::ParseFile(path.string());
  if (!nalus.ok()) return Fail("undefined TLV fixture parse");
  auto validator = IncrementalLiveValidator::Create(Codec::H264);
  if (!validator.ok()) return Fail("undefined TLV validator create");
  for (const auto& nal : nalus.value()) {
    auto event = validator.value().AddNal(nal);
    if (!event.ok()) return Fail("undefined TLV signed prefix");
  }
  const NalUnit undefined_tlv_sei{
      {0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0x13,
       0x00, 0x5b, 0xc9, 0x3f, 0x2d, 0x71, 0x5e, 0x95,
       0xad, 0xa4, 0x79, 0x6f, 0x90, 0x87, 0x7a, 0x6f,
       0x00, 0x00, 0x03, 0x00, 0x80},
      4};
  auto malformed = validator.value().AddNal(undefined_tlv_sei);
  const NalUnit next_gop{{0x00, 0x00, 0x00, 0x01, 0x65, 0x80,
                          0xff, 0x00, 0x80},
                         4};
  if (malformed.ok()) malformed = validator.value().AddNal(next_gop);
  if (malformed.ok() || malformed.error().code != ErrorCode::UpstreamFailure) {
    return Fail("undefined TLV tag was not rejected");
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return Fail("usage: test_live_incremental FIXTURE_DIR");
  const fs::path root = argv[1];
  if (CheckSigned(root / "h264/signed.h264", Codec::H264, "h264") ||
      CheckSigned(root / "h265/signed.h265", Codec::H265, "h265") ||
      CheckUnresolved(root / "h264/signed.h264", Codec::H264,
                      "h264-unresolved") ||
      CheckUnresolved(root / "h265/signed.h265", Codec::H265,
                      "h265-unresolved") ||
      CheckUnsigned(root / "h264/unsigned.h264", Codec::H264,
                    "h264-unsigned") ||
      CheckUnsigned(root / "h265/unsigned.h265", Codec::H265,
                    "h265-unsigned") ||
      CheckInvalid(root / "h264/tampered.h264", Codec::H264,
                   "h264-invalid") ||
      CheckUndefinedTlvRejected(root / "h264/signed.h264")) {
    return 1;
  }
  std::cout << "PASS: incremental live validator\n";
  return 0;
}
