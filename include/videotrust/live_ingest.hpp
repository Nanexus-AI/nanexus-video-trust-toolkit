#pragma once

#include "videotrust/annexb.hpp"
#include "videotrust/live.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>

namespace videotrust {

inline constexpr unsigned kLiveMinDurationSeconds = 1;
inline constexpr unsigned kLiveMaxDurationSeconds = 300;
inline constexpr unsigned kLiveConnectTimeoutSeconds = 5;
inline constexpr unsigned kLiveReadStallTimeoutSeconds = 5;
inline constexpr unsigned kLiveShutdownGraceSeconds = 2;
inline constexpr std::size_t kLiveMaxSampleBytes = 8 * 1024 * 1024;
inline constexpr std::size_t kLiveAppSinkBuffers = 2;
inline constexpr std::size_t kLiveMaxEvents = 1024;
inline constexpr std::size_t kLiveMaxDocumentBytes = 64 * 1024;
inline constexpr std::size_t kLiveMaxDiagnosticBytes = 160;

struct LiveIngestOptions {
  Codec codec{Codec::H264};
  std::string endpoint;
  std::string username;
  std::string password;
  unsigned duration_seconds{0};
};

struct LiveIngestResult {
  LiveStopReason stop_reason{LiveStopReason::TransportFailure};
  Error error{ErrorCode::Ok, {}};
  std::size_t samples{0};
  std::size_t nalus{0};
};

using LiveNalConsumer = std::function<Error(const NalUnit&)>;
using LiveCancelRequested = std::function<bool()>;

bool LiveIngestDependenciesAvailable() noexcept;
LiveIngestResult RunGStreamerLiveIngest(const LiveIngestOptions& options,
                                        const LiveNalConsumer& consume,
                                        const LiveCancelRequested& cancelled);

}  // namespace videotrust
