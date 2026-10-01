#pragma once

#include "videotrust/error.hpp"
#include "videotrust/live.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace videotrust {

inline constexpr const char* kLiveDocumentType =
    "media_signing_live_session_event";
inline constexpr const char* kLiveSchemaVersion = "0.1";
inline constexpr std::size_t kMaxLiveContractFindings = 16;

/// Optional, non-normative values. They never affect event ordering or meaning.
struct LiveRuntimeMetadata {
  std::optional<std::string> session_id;
  std::optional<std::string> observed_at;
};

/// Stateful public-contract renderer. Public sequence numbers start at zero.
/// Feed each material M4-B observation exactly once, then call Finalize once.
class LiveContractRenderer {
 public:
  Expected<std::string> Start(
      const LiveRuntimeMetadata& runtime = LiveRuntimeMetadata{});
  Expected<std::string> RenderObservation(
      const LiveObservation& observation,
      const LiveRuntimeMetadata& runtime = LiveRuntimeMetadata{});
  Expected<std::string> Finalize(
      LiveStopReason reason,
      const LiveRuntimeMetadata& runtime = LiveRuntimeMetadata{});

  /// Returns only the terminal summary after successful Finalize().
  Expected<std::string> SummaryOnlyJsonl() const;
  Expected<std::string> SummaryOnlyText() const;
  bool finalized() const noexcept { return finalized_; }

 private:
  Expected<std::string> Envelope(const char* event_type,
                                 const LiveRuntimeMetadata& runtime) const;
  Error ValidateRuntime(const LiveRuntimeMetadata& runtime) const;
  void AccumulateEpochCounters(const LiveObservation& observation);
  void AccumulateClosed(const ClosedEvidence& closed);

  bool started_{false};
  bool finalized_{false};
  bool epoch_active_{false};
  std::uint64_t next_sequence_{0};
  std::uint64_t last_domain_sequence_{0};
  bool have_domain_sequence_{false};
  std::uint64_t last_epoch_{0};
  std::uint64_t epoch_count_{0};
  std::uint64_t transport_boundary_count_{0};
  std::uint64_t reset_count_{0};
  std::uint64_t reconfiguration_count_{0};
  std::uint64_t received_nalus_{0};
  std::uint64_t validated_nalus_{0};
  std::uint64_t received_frames_{0};
  std::uint64_t validated_frames_{0};
  ClosedEvidenceSummary closed_summary_{};
  std::optional<ClosedEvidenceOutcome> worst_closed_outcome_;
  std::optional<CertificateStatus> certificate_status_;
  LiveTailState final_tail_{LiveTailState::AwaitingEvidence};
  LiveStopReason final_stop_reason_{LiveStopReason::None};
  std::vector<Codec> codecs_;
  std::vector<Finding> findings_;
  std::string summary_jsonl_;
};

std::string RenderLiveObservationText(const LiveObservation& observation);

}  // namespace videotrust
