#pragma once

#include "videotrust/annexb.hpp"
#include "videotrust/error.hpp"
#include "videotrust/inspection.hpp"
#include "videotrust/session.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace videotrust {

enum class LiveTailState {
  AwaitingEvidence,
  NoPendingTail,
  OpenPending,
  EndedWithUnresolvedTail,
};

enum class LiveEpochState { Idle, Active, Ended };

enum class LiveStopReason {
  None,
  OrderlyEnd,
  CallerCancelled,
  DeadlineReached,
  TransportFailure,
  ResourceLimit,
  Reset,
  Reconfigured,
};

enum class ClosedEvidenceOutcome { Valid, Invalid, Unsigned, NotVerifiable };

enum class LiveObservationKind { EpochStarted, SemanticChange, EpochEnded };

struct ClosedEvidence {
  ClosedEvidenceOutcome outcome{ClosedEvidenceOutcome::NotVerifiable};
  VerificationResult verification{};
  unsigned newly_validated_nalus{0};
  unsigned newly_validated_frames{0};
  unsigned accumulated_validated_nalus{0};
  unsigned accumulated_validated_frames{0};
};

struct ClosedEvidenceSummary {
  std::uint64_t valid{0};
  std::uint64_t invalid{0};
  std::uint64_t unsigned_stream{0};
  std::uint64_t not_verifiable{0};
};

struct LiveObservation {
  std::uint64_t sequence{0};
  std::uint64_t epoch{0};
  LiveObservationKind kind{LiveObservationKind::SemanticChange};
  Codec codec{Codec::H264};
  LiveEpochState epoch_state{LiveEpochState::Idle};
  LiveTailState tail{LiveTailState::AwaitingEvidence};
  LiveStopReason stop_reason{LiveStopReason::None};
  std::optional<ClosedEvidence> newly_closed;
  ClosedEvidenceSummary closed_summary{};
  AccumulatedValidationObservations accumulated{};
  LatestValidationObservations latest{};
};

/// Pure, transport-independent live semantic state machine.
///
/// It consumes official-framework snapshots and explicit lifecycle boundaries.
/// It stores only current state, bounded findings in the latest closed result,
/// and counters; callers may stream returned observations without retaining a
/// complete history.
class LiveSemanticModel {
 public:
  Expected<LiveObservation> BeginEpoch(Codec codec);
  Expected<std::optional<LiveObservation>> Observe(
      const InspectionResult& official_snapshot);
  Expected<LiveObservation> EndEpoch(LiveStopReason reason);

  LiveEpochState epoch_state() const noexcept { return epoch_state_; }
  LiveTailState tail_state() const noexcept { return tail_state_; }
  LiveStopReason stop_reason() const noexcept { return stop_reason_; }
  std::uint64_t epoch_index() const noexcept { return epoch_index_; }
  const ClosedEvidenceSummary& closed_summary() const noexcept {
    return closed_summary_;
  }
  const std::optional<ClosedEvidence>& latest_closed() const noexcept {
    return latest_closed_;
  }

 private:
  LiveObservation MakeObservation(LiveObservationKind kind,
                                  std::optional<ClosedEvidence> closed);
  std::optional<ClosedEvidence> DeriveClosure(
      const InspectionResult& snapshot,
      bool finalizing);

  std::uint64_t next_sequence_{0};
  std::uint64_t epoch_index_{0};
  Codec codec_{Codec::H264};
  LiveEpochState epoch_state_{LiveEpochState::Idle};
  LiveTailState tail_state_{LiveTailState::AwaitingEvidence};
  LiveStopReason stop_reason_{LiveStopReason::None};
  ClosedEvidenceSummary closed_summary_{};
  std::optional<ClosedEvidence> latest_closed_;
  std::optional<InspectionResult> latest_snapshot_;
  unsigned last_validated_nalus_{0};
  unsigned last_validated_frames_{0};
  bool epoch_has_closed_evidence_{false};
  bool definitive_without_validated_count_emitted_{false};
};

/// Official ONVIF incremental validator composed with LiveSemanticModel.
/// No transport or GStreamer type crosses this boundary.
class IncrementalLiveValidator {
 public:
  static Expected<IncrementalLiveValidator> Create(Codec codec);

  IncrementalLiveValidator(const IncrementalLiveValidator&) = delete;
  IncrementalLiveValidator& operator=(const IncrementalLiveValidator&) = delete;
  IncrementalLiveValidator(IncrementalLiveValidator&&) noexcept = default;
  IncrementalLiveValidator& operator=(IncrementalLiveValidator&&) noexcept = default;

  Error SetTrustedCertificateFile(const std::string& path);
  Expected<std::optional<LiveObservation>> AddNal(const NalUnit& nal);
  Expected<std::vector<LiveObservation>> EndEpoch(LiveStopReason reason);

  const LiveSemanticModel& semantic() const noexcept { return semantic_; }
  const LiveObservation& epoch_started() const noexcept { return epoch_started_; }

 private:
  IncrementalLiveValidator(MediaSigningSession session,
                           LiveSemanticModel semantic,
                           LiveObservation epoch_started);

  MediaSigningSession session_;
  LiveSemanticModel semantic_;
  LiveObservation epoch_started_{};
  bool trust_anchor_provided_{false};
};

const char* ToString(LiveTailState value) noexcept;
const char* ToString(LiveEpochState value) noexcept;
const char* ToString(LiveStopReason value) noexcept;
const char* ToString(ClosedEvidenceOutcome value) noexcept;
const char* ToString(LiveObservationKind value) noexcept;

}  // namespace videotrust
