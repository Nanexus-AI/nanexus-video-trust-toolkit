#pragma once

#include "videotrust/live_ingest.hpp"
#include "videotrust/stream_signer.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace videotrust {

// Internal source-level boundary; no installed SDK, CLI, JSON or MCP contract.
enum class SigningIngestKind { Media, Discontinuity, CleanEos, BoundedStop, Abort, Failure };
struct SigningIngestEvent {
  SigningIngestKind kind;
  std::span<const NalUnit> nalus{};  // One complete compressed AU; borrowed.
  int64_t timestamp_100ns{0};        // Gateway observation, not capture time.
  ErrorCode error_code{ErrorCode::UpstreamFailure}; // Failure only; no raw diagnostic.
};
using SigningIngestConsumer = std::function<Error(const SigningIngestEvent&)>;

enum class GatewayFailure { None, Input, Identity, Signer, Output, Transport, Resource, Continuity };
enum class GatewayEnd { Running, CleanEos, BoundedStop, Aborted, Failed, ContinuityLost };

struct SigningEpochSink {
  SignedNalSink write;
  // Must return promptly; acknowledge flush/close. Always called once per open.
  std::function<Error(SigningState)> close;
};
using SigningEpochSinkFactory = std::function<Expected<SigningEpochSink>(unsigned epoch)>;

struct LiveSigningOptions {
  StreamSigningOptions signer;
  unsigned max_epochs{8};  // 1..8, including aborted epochs.
  std::size_t max_output_bytes{kSigningMaxOutputBytes}; // Across all epochs.
};
struct SigningEpochSummary {
  unsigned epoch;
  SigningState state;
  GatewayFailure failure;
  std::size_t acknowledged_bytes;
};
struct LiveSigningResult {
  GatewayEnd end{GatewayEnd::Running};
  GatewayFailure failure{GatewayFailure::None};
  Error error{ErrorCode::Ok, {}};
  std::vector<SigningEpochSummary> epochs; // Fixed maximum eight.
};

/// Single-threaded, non-reentrant, cooperative callbacks. Discontinuity closes
/// the old sink as Aborted, then waits for a new random-access AU. Terminal
/// failures cannot resume. Destruction aborts any open epoch, never finalizes.
class LiveSigningOrchestrator {
 public:
  static Expected<std::unique_ptr<LiveSigningOrchestrator>> Create(
      const LiveSigningOptions& options, SigningEpochSinkFactory factory);
  ~LiveSigningOrchestrator();
  Error Consume(const SigningIngestEvent& event);
  const LiveSigningResult& result() const noexcept { return result_; }

 private:
  LiveSigningOrchestrator(LiveSigningOptions options, SigningEpochSinkFactory factory);
  Error StartEpoch();
  Error EndEpoch(bool finalize);
  Error Fail(GatewayFailure stage, ErrorCode code, const char* message);
  LiveSigningOptions options_;
  SigningEpochSinkFactory factory_;
  std::optional<SigningEpochSink> sink_;
  std::optional<StreamSigner> signer_;
  LiveSigningResult result_;
  std::size_t acknowledged_bytes_{0};
};

bool SigningAuHasRandomAccess(std::span<const NalUnit> nalus, Codec codec) noexcept;

// Shared fixed RTSP graph with opt-in guards. Stops at first continuity loss;
// no in-pipeline reconnect. Callback errors are terminal. Endpoint/credentials
// and duration are bounded and diagnostics contain no endpoint or key material.
LiveIngestResult RunGStreamerSigningIngest(const LiveIngestOptions& options,
    const SigningIngestConsumer& consume, const LiveCancelRequested& cancelled);
LiveSigningResult RunGStreamerLiveSigning(const LiveIngestOptions& ingest,
    const LiveSigningOptions& signing, SigningEpochSinkFactory factory,
    const LiveCancelRequested& cancelled);

}  // namespace videotrust
