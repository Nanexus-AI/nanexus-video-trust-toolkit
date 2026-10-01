#include "videotrust/live_contract.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace videotrust {
namespace {

std::string JsonString(const std::string& value) {
  std::string out{"\""};
  for (unsigned char c : value) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c >= 0x20 && c <= 0x7e) out += static_cast<char>(c);
        break;
    }
  }
  out += '"';
  return out;
}

bool ValidSessionId(const std::string& value) {
  if (value.empty() || value.size() > 64) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.';
  });
}

bool ValidTimestamp(const std::string& value) {
  if (value.empty() || value.size() > 40) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isdigit(c) || c == 'T' || c == 'Z' || c == '+' || c == '-' ||
           c == ':' || c == '.';
  });
}

int OutcomeRank(ClosedEvidenceOutcome value) {
  switch (value) {
    case ClosedEvidenceOutcome::Valid: return 0;
    case ClosedEvidenceOutcome::Unsigned: return 1;
    case ClosedEvidenceOutcome::NotVerifiable: return 2;
    case ClosedEvidenceOutcome::Invalid: return 3;
  }
  return 0;
}

int CertificateRank(CertificateStatus value) {
  switch (value) {
    case CertificateStatus::Ok: return 0;
    case CertificateStatus::FeasibleWithoutTrusted: return 1;
    case CertificateStatus::NotProvided: return 2;
    case CertificateStatus::NotFeasible: return 3;
    case CertificateStatus::NotOk: return 4;
  }
  return 0;
}

bool StableFinding(const std::string& code) {
  return code == "AUTH_NOT_OK" || code == "PROVENANCE_NOT_OK" ||
         code == "PROVENANCE_WITHOUT_TRUSTED" ||
         code == "SIGNING_KEY_PROVENANCE_OK";
}

void RenderRuntime(std::ostringstream& out, const LiveRuntimeMetadata& runtime) {
  if (!runtime.session_id && !runtime.observed_at) return;
  out << ",\"runtime\":{";
  bool comma = false;
  if (runtime.session_id) {
    out << "\"session_id\":" << JsonString(*runtime.session_id);
    comma = true;
  }
  if (runtime.observed_at) {
    if (comma) out << ',';
    out << "\"observed_at\":" << JsonString(*runtime.observed_at);
  }
  out << '}';
}

void RenderSummary(std::ostringstream& out, const ClosedEvidenceSummary& value) {
  out << "{\"valid\":" << value.valid << ",\"invalid\":" << value.invalid
      << ",\"unsigned\":" << value.unsigned_stream
      << ",\"not_verifiable\":" << value.not_verifiable << '}';
}

void RenderCounters(std::ostringstream& out,
                    const AccumulatedValidationObservations& value) {
  out << "{\"received_nalus\":" << value.received_nalus
      << ",\"validated_nalus\":" << value.validated_nalus
      << ",\"pending_nalus\":" << value.pending_nalus
      << ",\"received_frames\":" << value.received_frames
      << ",\"validated_frames\":" << value.validated_frames
      << ",\"pending_frames\":" << value.pending_frames << '}';
}

void RenderClosed(std::ostringstream& out, const ClosedEvidence& value) {
  const auto& v = value.verification;
  out << "{\"outcome\":\"" << ToString(value.outcome) << "\""
      << ",\"media_signing\":\"" << ToString(v.media_signing) << "\""
      << ",\"signature_integrity\":\"" << ToString(v.signature_integrity) << "\""
      << ",\"continuity\":\"" << ToString(v.continuity) << "\""
      << ",\"verification_completeness\":\"" << ToString(v.completeness) << "\""
      << ",\"certificate_status\":\"" << ToString(v.certificate) << "\""
      << ",\"source_authenticity\":\"not_established\""
      << ",\"public_key_has_changed\":"
      << (v.public_key_has_changed ? "true" : "false")
      << ",\"newly_validated_nalus\":" << value.newly_validated_nalus
      << ",\"newly_validated_frames\":" << value.newly_validated_frames
      << ",\"accumulated_validated_nalus\":"
      << value.accumulated_validated_nalus
      << ",\"accumulated_validated_frames\":"
      << value.accumulated_validated_frames << ",\"findings\":[";
  bool comma = false;
  std::vector<std::string> emitted;
  for (const auto& finding : v.findings) {
    if (!StableFinding(finding.code) || emitted.size() >= kMaxLiveContractFindings ||
        std::find(emitted.begin(), emitted.end(), finding.code) != emitted.end()) {
      continue;
    }
    if (comma) out << ',';
    out << "{\"code\":" << JsonString(finding.code)
        << ",\"severity\":\"info\"}";
    comma = true;
    emitted.push_back(finding.code);
  }
  out << "]}";
}

}  // namespace

Error LiveContractRenderer::ValidateRuntime(
    const LiveRuntimeMetadata& runtime) const {
  if (runtime.session_id && !ValidSessionId(*runtime.session_id)) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live runtime session_id must be a bounded opaque token");
  }
  if (runtime.observed_at && !ValidTimestamp(*runtime.observed_at)) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live runtime observed_at must be a bounded timestamp");
  }
  return Error{ErrorCode::Ok, {}};
}

Expected<std::string> LiveContractRenderer::Envelope(
    const char* event_type, const LiveRuntimeMetadata& runtime) const {
  Error valid = ValidateRuntime(runtime);
  if (valid.code != ErrorCode::Ok) return valid;
  std::ostringstream out;
  out << "{\"document_type\":\"" << kLiveDocumentType
      << "\",\"schema_version\":\"" << kLiveSchemaVersion
      << "\",\"event_type\":\"" << event_type
      << "\",\"sequence\":" << next_sequence_;
  RenderRuntime(out, runtime);
  return out.str();
}

Expected<std::string> LiveContractRenderer::Start(
    const LiveRuntimeMetadata& runtime) {
  if (started_ || finalized_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live contract session has already started");
  }
  auto base = Envelope("session_started", runtime);
  if (!base.ok()) return base.error();
  started_ = true;
  ++next_sequence_;
  return base.value() +
         ",\"source_authenticity\":\"not_established\","
         "\"limitations\":[\"source_authenticity_not_established\","
         "\"pre_connection_coverage_not_established\","
         "\"preservation_not_assessed\",\"rtsp_tcp_single_stream_scope\"]}\n";
}

void LiveContractRenderer::AccumulateClosed(const ClosedEvidence& closed) {
  if (!worst_closed_outcome_ ||
      OutcomeRank(closed.outcome) > OutcomeRank(*worst_closed_outcome_)) {
    worst_closed_outcome_ = closed.outcome;
  }
  if (!certificate_status_ ||
      CertificateRank(closed.verification.certificate) >
          CertificateRank(*certificate_status_)) {
    certificate_status_ = closed.verification.certificate;
  }
  for (const auto& finding : closed.verification.findings) {
    if (!StableFinding(finding.code)) continue;
    if (std::none_of(findings_.begin(), findings_.end(), [&](const Finding& v) {
          return v.code == finding.code;
        }) && findings_.size() < kMaxLiveContractFindings) {
      findings_.push_back({finding.code, {}});
    }
  }
}

void LiveContractRenderer::AccumulateEpochCounters(
    const LiveObservation& observation) {
  received_nalus_ += observation.accumulated.received_nalus;
  validated_nalus_ += observation.accumulated.validated_nalus;
  received_frames_ += observation.accumulated.received_frames;
  validated_frames_ += observation.accumulated.validated_frames;
}

Expected<std::string> LiveContractRenderer::RenderObservation(
    const LiveObservation& observation, const LiveRuntimeMetadata& runtime) {
  if (!started_ || finalized_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live contract is not open for observations");
  }
  if (have_domain_sequence_ && observation.sequence <= last_domain_sequence_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live observation sequence is not strictly increasing");
  }
  if (observation.epoch == 0 || observation.epoch < last_epoch_ ||
      observation.epoch > last_epoch_ + 1) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live observation epoch is not monotonic");
  }

  const char* type = nullptr;
  if (observation.kind == LiveObservationKind::EpochStarted) {
    if (epoch_active_ || observation.epoch != last_epoch_ + 1 ||
        observation.epoch_state != LiveEpochState::Active ||
        observation.stop_reason != LiveStopReason::None) {
      return MakeError(ErrorCode::InvalidArgument, "illegal epoch start");
    }
    type = "epoch_started";
  } else if (observation.kind == LiveObservationKind::SemanticChange) {
    if (!epoch_active_ || observation.epoch != last_epoch_ ||
        observation.epoch_state != LiveEpochState::Active ||
        observation.stop_reason != LiveStopReason::None) {
      return MakeError(ErrorCode::InvalidArgument,
                       "observation is outside its active epoch");
    }
    type = "verification_observation";
  } else {
    if (!epoch_active_ || observation.epoch != last_epoch_ ||
        observation.epoch_state != LiveEpochState::Ended ||
        observation.stop_reason == LiveStopReason::None ||
        observation.tail == LiveTailState::OpenPending ||
        observation.tail == LiveTailState::AwaitingEvidence) {
      return MakeError(ErrorCode::InvalidArgument, "illegal epoch end");
    }
    type = "epoch_ended";
  }

  auto base = Envelope(type, runtime);
  if (!base.ok()) return base.error();
  std::ostringstream out;
  out << base.value() << ",\"epoch\":" << observation.epoch
      << ",\"codec\":\"" << ToString(observation.codec) << "\""
      << ",\"tail_state\":\"" << ToString(observation.tail) << "\"";

  if (observation.kind != LiveObservationKind::EpochStarted) {
    out << ",\"material_change\":true,\"closed_evidence\":";
    if (observation.newly_closed) {
      RenderClosed(out, *observation.newly_closed);
    } else {
      out << "null";
    }
    out << ",\"closed_summary\":";
    RenderSummary(out, observation.closed_summary);
    out << ",\"official_counts\":";
    RenderCounters(out, observation.accumulated);
  }
  if (observation.kind == LiveObservationKind::EpochEnded) {
    out << ",\"stop_reason\":\"" << ToString(observation.stop_reason) << "\"";
  }
  out << "}\n";

  if (observation.kind == LiveObservationKind::EpochStarted) {
    epoch_active_ = true;
    last_epoch_ = observation.epoch;
    ++epoch_count_;
    if (std::find(codecs_.begin(), codecs_.end(), observation.codec) ==
        codecs_.end()) {
      codecs_.push_back(observation.codec);
    }
  }
  if (observation.newly_closed) AccumulateClosed(*observation.newly_closed);
  closed_summary_ = observation.closed_summary;
  final_tail_ = observation.tail;
  if (observation.kind == LiveObservationKind::EpochEnded) {
    epoch_active_ = false;
    final_stop_reason_ = observation.stop_reason;
    AccumulateEpochCounters(observation);
    if (observation.stop_reason == LiveStopReason::TransportFailure)
      ++transport_boundary_count_;
    if (observation.stop_reason == LiveStopReason::Reset) ++reset_count_;
    if (observation.stop_reason == LiveStopReason::Reconfigured)
      ++reconfiguration_count_;
  }
  last_domain_sequence_ = observation.sequence;
  have_domain_sequence_ = true;
  ++next_sequence_;
  return out.str();
}

Expected<std::string> LiveContractRenderer::Finalize(
    LiveStopReason reason, const LiveRuntimeMetadata& runtime) {
  if (!started_ || finalized_ || epoch_active_ || epoch_count_ == 0) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live contract cannot finalize in its current state");
  }
  if (reason == LiveStopReason::None || reason != final_stop_reason_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "summary stop reason must match the final epoch boundary");
  }
  auto base = Envelope("session_summary", runtime);
  if (!base.ok()) return base.error();
  std::ostringstream out;
  out << base.value() << ",\"stop_reason\":\"" << ToString(reason) << "\""
      << ",\"epoch_count\":" << epoch_count_ << ",\"codecs\":[";
  for (std::size_t i = 0; i < codecs_.size(); ++i) {
    if (i) out << ',';
    out << '"' << ToString(codecs_[i]) << '"';
  }
  out << "],\"totals\":{\"received_nalus\":" << received_nalus_
      << ",\"validated_nalus\":" << validated_nalus_
      << ",\"received_frames\":" << received_frames_
      << ",\"validated_frames\":" << validated_frames_ << '}'
      << ",\"closed_observation_count\":"
      << (closed_summary_.valid + closed_summary_.invalid +
          closed_summary_.unsigned_stream + closed_summary_.not_verifiable)
      << ",\"closed_summary\":";
  RenderSummary(out, closed_summary_);
  out << ",\"worst_closed_outcome\":";
  if (worst_closed_outcome_) out << '"' << ToString(*worst_closed_outcome_) << '"';
  else out << "null";
  out << ",\"final_tail_state\":\"" << ToString(final_tail_) << "\""
      << ",\"unresolved_tail\":"
      << (final_tail_ == LiveTailState::EndedWithUnresolvedTail ? "true" : "false")
      << ",\"boundary_counts\":{\"transport_failures\":"
      << transport_boundary_count_ << ",\"resets\":" << reset_count_
      << ",\"reconfigurations\":" << reconfiguration_count_ << '}'
      << ",\"certificate_status\":\""
      << ToString(certificate_status_.value_or(CertificateStatus::NotProvided))
      << "\""
      << ",\"source_authenticity\":\"not_established\""
      << ",\"cross_epoch_continuity\":\"not_established\""
      << ",\"findings\":[";
  for (std::size_t i = 0; i < findings_.size(); ++i) {
    if (i) out << ',';
    out << "{\"code\":" << JsonString(findings_[i].code)
        << ",\"severity\":\"info\"}";
  }
  out << "],\"limitations\":[\"source_authenticity_not_established\","
         "\"pre_connection_coverage_not_established\","
         "\"preservation_not_assessed\",\"rtsp_tcp_single_stream_scope\"";
  if (epoch_count_ > 1) out << ",\"cross_epoch_continuity_not_established\"";
  out << "]}\n";
  finalized_ = true;
  ++next_sequence_;
  summary_jsonl_ = out.str();
  return summary_jsonl_;
}

Expected<std::string> LiveContractRenderer::SummaryOnlyJsonl() const {
  if (!finalized_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live contract has no terminal summary");
  }
  return summary_jsonl_;
}

Expected<std::string> LiveContractRenderer::SummaryOnlyText() const {
  if (!finalized_) {
    return MakeError(ErrorCode::InvalidArgument,
                     "live contract has no terminal summary");
  }
  std::ostringstream out;
  out << "Nanexus live session summary\n"
      << "  Stop reason: " << ToString(final_stop_reason_) << "\n"
      << "  Epochs: " << epoch_count_ << "\n"
      << "  Closed observations: "
      << (closed_summary_.valid + closed_summary_.invalid +
          closed_summary_.unsigned_stream + closed_summary_.not_verifiable)
      << "\n  Final tail: " << ToString(final_tail_) << "\n"
      << "  Source authenticity: not_established\n";
  return out.str();
}

std::string RenderLiveObservationText(const LiveObservation& observation) {
  std::ostringstream out;
  out << "Nanexus live " << ToString(observation.kind) << "\n"
      << "  Epoch: " << observation.epoch << "\n"
      << "  Codec: " << ToString(observation.codec) << "\n"
      << "  Tail: " << ToString(observation.tail) << "\n";
  if (observation.newly_closed) {
    out << "  Closed outcome: " << ToString(observation.newly_closed->outcome)
        << "\n";
  }
  if (observation.kind == LiveObservationKind::EpochEnded) {
    out << "  Stop reason: " << ToString(observation.stop_reason) << "\n";
  }
  return out.str();
}

ExitCode ExitCodeForLive(const ClosedEvidenceSummary& closed,
                         LiveTailState tail,
                         LiveStopReason stop) noexcept {
  if (closed.invalid > 0) return ExitCode::VerificationNegative;
  if (stop == LiveStopReason::TransportFailure ||
      stop == LiveStopReason::ResourceLimit || stop == LiveStopReason::Reset ||
      stop == LiveStopReason::Reconfigured) return ExitCode::RuntimeFailure;
  if (tail == LiveTailState::EndedWithUnresolvedTail ||
      closed.unsigned_stream > 0 || closed.not_verifiable > 0 ||
      closed.valid == 0) return ExitCode::UnsignedOrNotVerifiable;
  return ExitCode::Success;
}

}  // namespace videotrust
