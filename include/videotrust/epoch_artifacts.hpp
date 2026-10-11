#pragma once
#include "videotrust/live_signing.hpp"

namespace videotrust {
// Internal Linux/POSIX artifact boundary. A new absolute root, existing real
// parents, no symlinks/dot components. Never overwrites or deletes old output.
// Synchronous ordinary filesystem I/O: byte bounded, not hard realtime.
class EpochArtifacts {
 public:
  static Expected<EpochArtifacts> Create(const std::string& root, Codec codec);
  SigningEpochSinkFactory Factory() const;
  // After orchestration has closed every sink, persist bounded run/epoch causes.
  // Success is not a verification verdict. Unsealed evidence remains explicit.
  Error Finish(const LiveSigningResult& result);
 private:
  struct Impl;
  explicit EpochArtifacts(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
  std::shared_ptr<Impl> impl_;
};
} // namespace videotrust
