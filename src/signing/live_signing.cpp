#include "videotrust/live_signing.hpp"
#include "videotrust/nal_classify.hpp"

#include <utility>

namespace videotrust {

bool SigningAuHasRandomAccess(std::span<const NalUnit> nalus, Codec codec) noexcept {
  for (const auto& nal : nalus) {
    const auto type = RawNalType(codec, nal);
    if ((codec == Codec::H264 && type == 5) ||
        (codec == Codec::H265 && type >= 16 && type <= 23)) return true;
  }
  return false;
}

LiveSigningOrchestrator::LiveSigningOrchestrator(
    LiveSigningOptions options, SigningEpochSinkFactory factory)
    : options_(std::move(options)), factory_(std::move(factory)) {}

Expected<std::unique_ptr<LiveSigningOrchestrator>> LiveSigningOrchestrator::Create(
    const LiveSigningOptions& options, SigningEpochSinkFactory factory) {
  if (!factory || options.max_epochs == 0 || options.max_epochs > 8 ||
      options.max_output_bytes == 0 || options.max_output_bytes > kSigningMaxOutputBytes ||
      (options.signer.codec != Codec::H264 && options.signer.codec != Codec::H265))
    return MakeError(ErrorCode::InvalidArgument, "invalid live signing configuration");
  return std::unique_ptr<LiveSigningOrchestrator>(
      new LiveSigningOrchestrator(options, std::move(factory)));
}

LiveSigningOrchestrator::~LiveSigningOrchestrator() {
  if (signer_) {
    signer_->Abort();
    EndEpoch(false);
  }
}

Error LiveSigningOrchestrator::Fail(GatewayFailure stage, ErrorCode code, const char* message) {
  result_.end = GatewayEnd::Failed;
  result_.failure = stage;
  result_.error = MakeError(code, message);
  if (signer_) EndEpoch(false);
  return result_.error;
}

Error LiveSigningOrchestrator::StartEpoch() {
  if (result_.epochs.size() >= options_.max_epochs)
    return Fail(GatewayFailure::Resource, ErrorCode::InvalidArgument, "signing epoch bound reached");
  const auto id = static_cast<unsigned>(result_.epochs.size()) + 1;
  try {
    auto opened = factory_(id);
    if (!opened.ok())
      return Fail(GatewayFailure::Output, ErrorCode::IoFailure, "epoch output unavailable");
    sink_ = std::move(opened.value());
  } catch (...) {
    return Fail(GatewayFailure::Output, ErrorCode::IoFailure, "epoch output unavailable");
  }
  // The close callback is required even if signer identity setup fails.
  if (!sink_->close || !sink_->write) {
    if (sink_->close) { try { sink_->close(SigningState::Failed); } catch (...) {} }
    sink_.reset();
    return Fail(GatewayFailure::Output, ErrorCode::IoFailure, "invalid epoch output");
  }
  auto created = StreamSigner::Create(options_.signer,
      [this](std::span<const uint8_t> bytes) -> Error {
        if (bytes.size() > options_.max_output_bytes - acknowledged_bytes_)
          return MakeError(ErrorCode::IoFailure, "run output bound reached");
        auto error = sink_->write(bytes);
        if (error.code == ErrorCode::Ok) acknowledged_bytes_ += bytes.size();
        return error;
      });
  if (!created.ok()) {
    try { sink_->close(SigningState::Failed); } catch (...) {}
    sink_.reset();
    return Fail(GatewayFailure::Identity, created.error().code, "signing identity or setup rejected");
  }
  signer_ = std::move(created.value());
  result_.epochs.push_back({id, SigningState::Active, GatewayFailure::None, 0});
  return MakeError(ErrorCode::Ok, {});
}

Error LiveSigningOrchestrator::EndEpoch(bool finalize) {
  if (!signer_) return MakeError(ErrorCode::Ok, {});
  Error error{ErrorCode::Ok, {}};
  if (finalize) error = signer_->Finalize();
  else signer_->Abort();
  if (error.code != ErrorCode::Ok) {
    result_.end = GatewayEnd::Failed;
    result_.failure = signer_->failure() == SigningFailure::Output
        ? GatewayFailure::Output : GatewayFailure::Signer;
    result_.error = MakeError(error.code, "epoch finalization failed");
  }
  auto state = signer_->state();
  try {
    if (sink_->close(state).code != ErrorCode::Ok) {
      result_.end = GatewayEnd::Failed;
      result_.failure = GatewayFailure::Output;
      result_.error = MakeError(ErrorCode::IoFailure, "epoch output closure failed");
      state = SigningState::Failed;
    }
  } catch (...) {
    result_.end = GatewayEnd::Failed;
    result_.failure = GatewayFailure::Output;
    result_.error = MakeError(ErrorCode::IoFailure, "epoch output closure failed");
    state = SigningState::Failed;
  }
  result_.epochs.back() = {result_.epochs.back().epoch, state,
      result_.failure, signer_->output_bytes()};
  signer_.reset();
  sink_.reset();
  return result_.error;
}

Error LiveSigningOrchestrator::Consume(const SigningIngestEvent& event) {
  if (result_.end != GatewayEnd::Running)
    return MakeError(ErrorCode::InvalidArgument, "live signing run is terminal");
  switch (event.kind) {
    case SigningIngestKind::Discontinuity: {
      const bool had_epoch = signer_.has_value();
      auto error = EndEpoch(false); // Wait for fresh random access; no EOS drain.
      if (had_epoch && error.code == ErrorCode::Ok)
        result_.epochs.back().failure = GatewayFailure::Continuity;
      return error;
    }
    case SigningIngestKind::CleanEos:
    case SigningIngestKind::BoundedStop: {
      auto error = EndEpoch(true);
      if (error.code == ErrorCode::Ok)
        result_.end = event.kind == SigningIngestKind::CleanEos
            ? GatewayEnd::CleanEos : GatewayEnd::BoundedStop;
      return error;
    }
    case SigningIngestKind::Abort: {
      auto error = EndEpoch(false);
      if (error.code == ErrorCode::Ok) result_.end = GatewayEnd::Aborted;
      return error;
    }
    case SigningIngestKind::Failure:
      return Fail(event.error_code == ErrorCode::ParseError ||
          event.error_code == ErrorCode::InvalidArgument ? GatewayFailure::Input : GatewayFailure::Transport,
          event.error_code, "live ingest failed");
    case SigningIngestKind::Media:
      break;
  }
  if (event.nalus.empty() || event.nalus.size() > 4096 || event.timestamp_100ns < 0)
    return Fail(GatewayFailure::Input, ErrorCode::InvalidArgument, "invalid signing access unit");
  std::size_t size = 0;
  for (const auto& nal : event.nalus) {
    if (nal.bytes.size() > kLiveMaxSampleBytes - size)
      return Fail(GatewayFailure::Input, ErrorCode::InvalidArgument, "signing access unit exceeds bound");
    size += nal.bytes.size();
  }
  if (!signer_) {
    if (!SigningAuHasRandomAccess(event.nalus, options_.signer.codec))
      return MakeError(ErrorCode::Ok, {});
    auto error = StartEpoch();
    if (error.code != ErrorCode::Ok) return error;
  }
  for (const auto& nal : event.nalus) {
    auto error = signer_->AddNal(nal, event.timestamp_100ns);
    if (error.code != ErrorCode::Ok) {
      const auto failure = signer_->failure();
      return Fail(failure == SigningFailure::Input ? GatewayFailure::Input :
          failure == SigningFailure::Output ? GatewayFailure::Output : GatewayFailure::Signer,
          error.code, "live signing media processing failed");
    }
  }
  return MakeError(ErrorCode::Ok, {});
}

}  // namespace videotrust
