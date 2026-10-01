#include "videotrust/live.hpp"

#include "videotrust/upstream_mapping.hpp"
#include "videotrust/verify.hpp"

#include <onvif_media_signing_validator.h>

#include <algorithm>
#include <utility>

namespace videotrust {
namespace {

constexpr std::size_t kMaxLiveFindings = 16;

bool HasPending(const InspectionResult& value) {
  return value.accumulated.pending_nalus > 0 ||
         value.accumulated.pending_frames > 0 ||
         (value.latest.pending_hashable_nalus.has_value() &&
          *value.latest.pending_hashable_nalus > 0);
}

std::optional<ClosedEvidenceOutcome> OutcomeForIntegrity(
    SignatureIntegrity integrity) {
  switch (integrity) {
    case SignatureIntegrity::Ok:
      return ClosedEvidenceOutcome::Valid;
    case SignatureIntegrity::OkWithMissingInfo:
      return ClosedEvidenceOutcome::NotVerifiable;
    case SignatureIntegrity::NotOk:
    case SignatureIntegrity::VersionMismatch:
      return ClosedEvidenceOutcome::Invalid;
    case SignatureIntegrity::NotApplicable:
    case SignatureIntegrity::NotFeasible:
      return std::nullopt;
  }
  return std::nullopt;
}

void IncrementSummary(ClosedEvidenceSummary& summary,
                      ClosedEvidenceOutcome outcome) {
  switch (outcome) {
    case ClosedEvidenceOutcome::Valid:
      ++summary.valid;
      break;
    case ClosedEvidenceOutcome::Invalid:
      ++summary.invalid;
      break;
    case ClosedEvidenceOutcome::Unsigned:
      ++summary.unsigned_stream;
      break;
    case ClosedEvidenceOutcome::NotVerifiable:
      ++summary.not_verifiable;
      break;
  }
}

VerificationResult BoundedVerification(VerificationResult value) {
  if (value.findings.size() > kMaxLiveFindings) {
    value.findings.resize(kMaxLiveFindings);
  }
  return value;
}

}  // namespace

Expected<LiveObservation> LiveSemanticModel::BeginEpoch(Codec codec) {
  if (epoch_state_ == LiveEpochState::Active) {
    return MakeError(ErrorCode::InvalidArgument, "live epoch is already active");
  }
  ++epoch_index_;
  codec_ = codec;
  epoch_state_ = LiveEpochState::Active;
  tail_state_ = LiveTailState::AwaitingEvidence;
  stop_reason_ = LiveStopReason::None;
  latest_snapshot_.reset();
  last_validated_nalus_ = 0;
  last_validated_frames_ = 0;
  epoch_has_closed_evidence_ = false;
  definitive_without_validated_count_emitted_ = false;
  return MakeObservation(LiveObservationKind::EpochStarted, std::nullopt);
}

Expected<std::optional<LiveObservation>> LiveSemanticModel::Observe(
    const InspectionResult& snapshot) {
  if (epoch_state_ != LiveEpochState::Active) {
    return MakeError(ErrorCode::InvalidArgument, "live epoch is not active");
  }
  if (snapshot.verification.codec != codec_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "official snapshot codec differs from active epoch");
  }

  const LiveTailState previous_tail = tail_state_;
  std::optional<ClosedEvidence> closed = DeriveClosure(snapshot, false);
  if (HasPending(snapshot)) {
    tail_state_ = LiveTailState::OpenPending;
  } else if (epoch_has_closed_evidence_) {
    tail_state_ = LiveTailState::NoPendingTail;
  } else {
    tail_state_ = LiveTailState::AwaitingEvidence;
  }
  latest_snapshot_ = snapshot;

  if (!closed.has_value() && tail_state_ == previous_tail) {
    return std::optional<LiveObservation>{};
  }
  return std::optional<LiveObservation>{
      MakeObservation(LiveObservationKind::SemanticChange, std::move(closed))};
}

Expected<LiveObservation> LiveSemanticModel::EndEpoch(LiveStopReason reason) {
  if (epoch_state_ != LiveEpochState::Active) {
    return MakeError(ErrorCode::InvalidArgument, "live epoch is not active");
  }
  std::optional<ClosedEvidence> closed;
  if (latest_snapshot_.has_value()) {
    closed = DeriveClosure(*latest_snapshot_, true);
    tail_state_ = HasPending(*latest_snapshot_)
                      ? LiveTailState::EndedWithUnresolvedTail
                      : LiveTailState::NoPendingTail;
  } else {
    tail_state_ = LiveTailState::NoPendingTail;
  }
  epoch_state_ = LiveEpochState::Ended;
  stop_reason_ = reason;
  return MakeObservation(LiveObservationKind::EpochEnded, std::move(closed));
}

std::optional<ClosedEvidence> LiveSemanticModel::DeriveClosure(
    const InspectionResult& snapshot,
    bool finalizing) {
  const auto validated_nalus = snapshot.accumulated.validated_nalus;
  const auto validated_frames = snapshot.accumulated.validated_frames;
  const bool newly_validated = validated_nalus > last_validated_nalus_ ||
                               validated_frames > last_validated_frames_;
  auto outcome = OutcomeForIntegrity(snapshot.verification.signature_integrity);

  if (!newly_validated && outcome == ClosedEvidenceOutcome::Invalid &&
      !definitive_without_validated_count_emitted_ &&
      (!latest_closed_.has_value() ||
       latest_closed_->outcome != ClosedEvidenceOutcome::Invalid)) {
    definitive_without_validated_count_emitted_ = true;
  } else if (!newly_validated) {
    outcome.reset();
  }

  if (finalizing && !outcome.has_value() && !epoch_has_closed_evidence_ &&
      snapshot.accumulated.received_nalus > 0) {
    if (snapshot.verification.signature_integrity ==
        SignatureIntegrity::NotApplicable) {
      outcome = ClosedEvidenceOutcome::Unsigned;
    } else if (snapshot.verification.signature_integrity ==
                   SignatureIntegrity::NotFeasible &&
               !HasPending(snapshot)) {
      outcome = ClosedEvidenceOutcome::NotVerifiable;
    }
  }

  if (!outcome.has_value()) {
    last_validated_nalus_ = std::max(last_validated_nalus_, validated_nalus);
    last_validated_frames_ = std::max(last_validated_frames_, validated_frames);
    return std::nullopt;
  }

  ClosedEvidence closed;
  closed.outcome = *outcome;
  closed.verification = BoundedVerification(snapshot.verification);
  closed.newly_validated_nalus = validated_nalus - last_validated_nalus_;
  closed.newly_validated_frames = validated_frames - last_validated_frames_;
  closed.accumulated_validated_nalus = validated_nalus;
  closed.accumulated_validated_frames = validated_frames;
  last_validated_nalus_ = validated_nalus;
  last_validated_frames_ = validated_frames;
  IncrementSummary(closed_summary_, closed.outcome);
  epoch_has_closed_evidence_ = true;
  if (closed.outcome == ClosedEvidenceOutcome::Invalid) {
    definitive_without_validated_count_emitted_ = true;
  }
  latest_closed_ = closed;
  return closed;
}

LiveObservation LiveSemanticModel::MakeObservation(
    LiveObservationKind kind,
    std::optional<ClosedEvidence> closed) {
  LiveObservation out;
  out.sequence = next_sequence_++;
  out.epoch = epoch_index_;
  out.kind = kind;
  out.codec = codec_;
  out.epoch_state = epoch_state_;
  out.tail = tail_state_;
  out.stop_reason = stop_reason_;
  out.newly_closed = std::move(closed);
  out.closed_summary = closed_summary_;
  if (latest_snapshot_.has_value()) {
    out.accumulated = latest_snapshot_->accumulated;
    out.latest = latest_snapshot_->latest;
  }
  return out;
}

IncrementalLiveValidator::IncrementalLiveValidator(
    MediaSigningSession session,
    LiveSemanticModel semantic,
    LiveObservation epoch_started)
    : session_(std::move(session)),
      semantic_(std::move(semantic)),
      epoch_started_(std::move(epoch_started)) {}

Expected<IncrementalLiveValidator> IncrementalLiveValidator::Create(Codec codec) {
  auto session = MediaSigningSession::Create(codec);
  if (!session.ok()) return session.error();
  LiveSemanticModel semantic;
  auto started = semantic.BeginEpoch(codec);
  if (!started.ok()) return started.error();
  return IncrementalLiveValidator(std::move(session.value()), std::move(semantic),
                                  std::move(started.value()));
}

Error IncrementalLiveValidator::SetTrustedCertificateFile(
    const std::string& path) {
  Error result = SetTrustedCertificateFromFile(session_, path);
  if (result.code == ErrorCode::Ok) trust_anchor_provided_ = true;
  return result;
}

Expected<std::optional<LiveObservation>> IncrementalLiveValidator::AddNal(
    const NalUnit& nal) {
  if (nal.bytes.empty()) {
    return MakeError(ErrorCode::InvalidArgument, "live NAL unit is empty");
  }
  onvif_media_signing_authenticity_t* report = nullptr;
  const MediaSigningReturnCode rc = onvif_media_signing_add_nalu_and_authenticate(
      session_.get(), nal.bytes.data(), nal.bytes.size(), &report);
  if (rc != OMS_OK) {
    if (report) onvif_media_signing_authenticity_report_free(report);
    return MakeError(ErrorCode::UpstreamFailure,
                     "incremental ONVIF validation failed: " +
                         std::to_string(static_cast<int>(rc)));
  }
  if (!report) {
    report = onvif_media_signing_get_authenticity_report(session_.get());
  }
  if (!report) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "incremental ONVIF report was unavailable");
  }
  InspectionResult snapshot = MapInspectionFromUpstream(
      session_.codec(), *report, trust_anchor_provided_);
  onvif_media_signing_authenticity_report_free(report);
  return semantic_.Observe(snapshot);
}

Expected<std::vector<LiveObservation>> IncrementalLiveValidator::EndEpoch(
    LiveStopReason reason) {
  std::vector<LiveObservation> out;
  onvif_media_signing_authenticity_t* report =
      onvif_media_signing_get_authenticity_report(session_.get());
  if (!report) {
    return MakeError(ErrorCode::UpstreamFailure,
                     "final incremental ONVIF report was unavailable");
  }
  InspectionResult snapshot = MapInspectionFromUpstream(
      session_.codec(), *report, trust_anchor_provided_);
  onvif_media_signing_authenticity_report_free(report);
  auto change = semantic_.Observe(snapshot);
  if (!change.ok()) return change.error();
  if (change.value().has_value()) out.push_back(std::move(*change.value()));
  auto ended = semantic_.EndEpoch(reason);
  if (!ended.ok()) return ended.error();
  out.push_back(std::move(ended.value()));
  return out;
}

const char* ToString(LiveTailState value) noexcept {
  switch (value) {
    case LiveTailState::AwaitingEvidence: return "awaiting_evidence";
    case LiveTailState::NoPendingTail: return "no_pending_tail";
    case LiveTailState::OpenPending: return "open_pending";
    case LiveTailState::EndedWithUnresolvedTail:
      return "ended_with_unresolved_tail";
  }
  return "unknown";
}

const char* ToString(LiveEpochState value) noexcept {
  switch (value) {
    case LiveEpochState::Idle: return "idle";
    case LiveEpochState::Active: return "active";
    case LiveEpochState::Ended: return "ended";
  }
  return "unknown";
}

const char* ToString(LiveStopReason value) noexcept {
  switch (value) {
    case LiveStopReason::None: return "none";
    case LiveStopReason::OrderlyEnd: return "orderly_end";
    case LiveStopReason::CallerCancelled: return "caller_cancelled";
    case LiveStopReason::DeadlineReached: return "deadline_reached";
    case LiveStopReason::TransportFailure: return "transport_failure";
    case LiveStopReason::ResourceLimit: return "resource_limit";
    case LiveStopReason::Reset: return "reset";
    case LiveStopReason::Reconfigured: return "reconfigured";
  }
  return "unknown";
}

const char* ToString(ClosedEvidenceOutcome value) noexcept {
  switch (value) {
    case ClosedEvidenceOutcome::Valid: return "valid";
    case ClosedEvidenceOutcome::Invalid: return "invalid";
    case ClosedEvidenceOutcome::Unsigned: return "unsigned";
    case ClosedEvidenceOutcome::NotVerifiable: return "not_verifiable";
  }
  return "unknown";
}

const char* ToString(LiveObservationKind value) noexcept {
  switch (value) {
    case LiveObservationKind::EpochStarted: return "epoch_started";
    case LiveObservationKind::SemanticChange: return "semantic_change";
    case LiveObservationKind::EpochEnded: return "epoch_ended";
  }
  return "unknown";
}

}  // namespace videotrust
