#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace videotrust::detail {

// Conservative terminal latch for one pipeline run. Streaming-thread probes
// only record uncertainty; application-thread callbacks own signing state.
class IngestContinuity {
 public:
  enum class Event { StreamStart, Caps, Segment, Flush, Gap, SourceRemoved };
  bool lost() const noexcept { return lost_.load(std::memory_order_acquire); }
  void Rtp(uint16_t sequence, uint32_t ssrc, bool discont, bool corrupted) {
    std::lock_guard lock(mutex_);
    if (corrupted || (have_rtp_ && (discont || ssrc != ssrc_ ||
        sequence != static_cast<uint16_t>(sequence_ + 1)))) Lose();
    have_rtp_ = true;
    sequence_ = sequence;
    ssrc_ = ssrc;
  }
  void Output(bool discont, bool corrupted) {
    std::lock_guard lock(mutex_);
    if (corrupted || (have_output_ && discont)) Lose();
    have_output_ = true;
  }
  void Observe(Event event, bool output = false) {
    std::lock_guard lock(mutex_);
    auto& seen = seen_[output ? 1 : 0][static_cast<unsigned>(event)];
    if (event == Event::Gap ||
        ((have_rtp_ || have_output_) &&
         (seen || event == Event::Flush || event == Event::SourceRemoved))) Lose();
    seen = true;
  }
  void SessionReconfigure() {
    std::lock_guard lock(mutex_);
    if (have_rtp_ || have_output_) Lose();
  }
  void InvalidPacket() noexcept { Lose(); }
 private:
  void Lose() noexcept { lost_.store(true, std::memory_order_release); }
  std::atomic<bool> lost_{false};
  std::mutex mutex_;
  bool have_rtp_{false}, have_output_{false};
  bool seen_[2][6]{};
  uint16_t sequence_{0};
  uint32_t ssrc_{0};
};

}  // namespace videotrust::detail
