#include "videotrust/live_contract.hpp"

#include <iostream>
#include <string>

namespace {
using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

LiveObservation Start(std::uint64_t sequence, std::uint64_t epoch, Codec codec) {
  LiveObservation value;
  value.sequence = sequence;
  value.epoch = epoch;
  value.kind = LiveObservationKind::EpochStarted;
  value.codec = codec;
  value.epoch_state = LiveEpochState::Active;
  value.tail = LiveTailState::AwaitingEvidence;
  return value;
}

ClosedEvidence Closed(ClosedEvidenceOutcome outcome, Codec codec) {
  ClosedEvidence value;
  value.outcome = outcome;
  value.verification.codec = codec;
  value.verification.certificate = CertificateStatus::NotProvided;
  value.verification.completeness = VerificationCompleteness::Complete;
  if (outcome == ClosedEvidenceOutcome::Valid) {
    value.verification.media_signing = SigningPresence::Detected;
    value.verification.signature_integrity = SignatureIntegrity::Ok;
    value.verification.continuity = ContinuityStatus::Intact;
  } else if (outcome == ClosedEvidenceOutcome::Invalid) {
    value.verification.media_signing = SigningPresence::Detected;
    value.verification.signature_integrity = SignatureIntegrity::NotOk;
    value.verification.continuity = ContinuityStatus::Broken;
    value.verification.findings.push_back(
        {"AUTH_NOT_OK",
         "rtsp://user:password@private-host/stream /home/operator/key.pem "
         "SECRET_ENV=value GStreamer internal debug"});
  } else if (outcome == ClosedEvidenceOutcome::Unsigned) {
    value.verification.media_signing = SigningPresence::NotDetected;
    value.verification.signature_integrity = SignatureIntegrity::NotApplicable;
    value.verification.continuity = ContinuityStatus::NotApplicable;
  } else {
    value.verification.media_signing = SigningPresence::Detected;
    value.verification.signature_integrity = SignatureIntegrity::NotFeasible;
    value.verification.continuity = ContinuityStatus::MissingInfo;
    value.verification.completeness = VerificationCompleteness::NotFeasible;
  }
  value.newly_validated_nalus = 10;
  value.newly_validated_frames = 5;
  value.accumulated_validated_nalus = 10;
  value.accumulated_validated_frames = 5;
  if (outcome == ClosedEvidenceOutcome::Unsigned) {
    value.newly_validated_nalus = 0;
    value.newly_validated_frames = 0;
    value.accumulated_validated_nalus = 0;
    value.accumulated_validated_frames = 0;
  }
  return value;
}

void Increment(ClosedEvidenceSummary& summary, ClosedEvidenceOutcome outcome) {
  if (outcome == ClosedEvidenceOutcome::Valid) ++summary.valid;
  if (outcome == ClosedEvidenceOutcome::Invalid) ++summary.invalid;
  if (outcome == ClosedEvidenceOutcome::Unsigned) ++summary.unsigned_stream;
  if (outcome == ClosedEvidenceOutcome::NotVerifiable) ++summary.not_verifiable;
}

LiveObservation Change(std::uint64_t sequence, std::uint64_t epoch, Codec codec,
                       LiveTailState tail, ClosedEvidenceSummary summary,
                       std::optional<ClosedEvidence> closed = std::nullopt) {
  LiveObservation value;
  value.sequence = sequence;
  value.epoch = epoch;
  value.kind = LiveObservationKind::SemanticChange;
  value.codec = codec;
  value.epoch_state = LiveEpochState::Active;
  value.tail = tail;
  value.newly_closed = std::move(closed);
  value.closed_summary = summary;
  value.accumulated.received_nalus = 12;
  value.accumulated.validated_nalus = 10;
  value.accumulated.pending_nalus = tail == LiveTailState::OpenPending ? 2 : 0;
  value.accumulated.received_frames = 6;
  value.accumulated.validated_frames = 5;
  value.accumulated.pending_frames = tail == LiveTailState::OpenPending ? 1 : 0;
  return value;
}

LiveObservation End(std::uint64_t sequence, std::uint64_t epoch, Codec codec,
                    LiveTailState tail, LiveStopReason reason,
                    ClosedEvidenceSummary summary) {
  auto value = Change(sequence, epoch, codec, tail, summary);
  value.kind = LiveObservationKind::EpochEnded;
  value.epoch_state = LiveEpochState::Ended;
  value.stop_reason = reason;
  if (tail == LiveTailState::EndedWithUnresolvedTail) {
    value.accumulated.pending_nalus = 2;
    value.accumulated.pending_frames = 1;
  }
  return value;
}

std::string Scenario(const std::string& name, bool runtime = false) {
  LiveContractRenderer renderer;
  LiveRuntimeMetadata metadata;
  if (runtime) {
    metadata.session_id = "session-123";
    metadata.observed_at = "2026-10-01T12:00:00Z";
  }
  std::string output = renderer.Start(metadata).value();
  std::uint64_t sequence = 0;
  ClosedEvidenceSummary summary;
  Codec codec = name == "multi_epoch" ? Codec::H265 : Codec::H264;
  output += renderer.RenderObservation(Start(sequence++, 1, codec), metadata).value();

  ClosedEvidenceOutcome outcome = ClosedEvidenceOutcome::Valid;
  if (name == "invalid") outcome = ClosedEvidenceOutcome::Invalid;
  if (name == "unsigned" || name == "unsigned_unresolved")
    outcome = ClosedEvidenceOutcome::Unsigned;
  Increment(summary, outcome);
  output += renderer.RenderObservation(
      Change(sequence++, 1, codec,
             (name == "healthy" || name == "valid_unresolved")
                 ? LiveTailState::OpenPending
                 : LiveTailState::NoPendingTail,
             summary, Closed(outcome, codec)), metadata).value();

  if (name == "healthy") {
    output += renderer.RenderObservation(
        Change(sequence++, 1, codec, LiveTailState::NoPendingTail, summary),
        metadata).value();
  }
  if (name == "unsigned_unresolved") {
    output += renderer.RenderObservation(
        Change(sequence++, 1, codec, LiveTailState::OpenPending, summary),
        metadata).value();
  }

  LiveTailState final_tail =
      (name == "valid_unresolved" || name == "unsigned_unresolved")
          ? LiveTailState::EndedWithUnresolvedTail
          : LiveTailState::NoPendingTail;
  LiveStopReason stop = final_tail == LiveTailState::EndedWithUnresolvedTail
                            ? LiveStopReason::TransportFailure
                            : LiveStopReason::OrderlyEnd;
  output += renderer.RenderObservation(
      End(sequence++, 1, codec, final_tail, stop, summary), metadata).value();

  if (name == "multi_epoch") {
    output += renderer.RenderObservation(Start(sequence++, 2, Codec::H264), metadata).value();
    Increment(summary, ClosedEvidenceOutcome::NotVerifiable);
    output += renderer.RenderObservation(
        Change(sequence++, 2, Codec::H264, LiveTailState::NoPendingTail,
               summary, Closed(ClosedEvidenceOutcome::NotVerifiable, Codec::H264)),
        metadata).value();
    output += renderer.RenderObservation(
        End(sequence++, 2, Codec::H264, LiveTailState::NoPendingTail,
            LiveStopReason::OrderlyEnd, summary), metadata).value();
    stop = LiveStopReason::OrderlyEnd;
  }
  output += renderer.Finalize(stop, metadata).value();
  return output;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--scenario") {
    std::cout << Scenario(argv[2]);
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--scenario-runtime") {
    std::cout << Scenario(argv[2], true);
    return 0;
  }

  const std::string unsigned_unresolved = Scenario("unsigned_unresolved");
  if (unsigned_unresolved.find("\"outcome\":\"unsigned\"") == std::string::npos ||
      unsigned_unresolved.find("\"final_tail_state\":\"ended_with_unresolved_tail\"") == std::string::npos) {
    return Fail("unsigned plus unresolved tail");
  }
  if (Scenario("healthy").find("\"outcome\":\"valid\"") == std::string::npos ||
      Scenario("healthy").find("\"tail_state\":\"open_pending\"") == std::string::npos) {
    return Fail("valid plus open pending");
  }
  const std::string privacy_output = Scenario("invalid");
  if (privacy_output.find("password") != std::string::npos ||
      privacy_output.find("private-host") != std::string::npos ||
      privacy_output.find("/home/operator") != std::string::npos ||
      privacy_output.find("SECRET_ENV") != std::string::npos ||
      privacy_output.find("GStreamer") != std::string::npos) {
    return Fail("free-form finding text leaked");
  }

  LiveContractRenderer metadata_a;
  LiveContractRenderer metadata_b;
  auto a = metadata_a.Start({"run-a", "2026-10-01T12:00:00Z"});
  auto b = metadata_b.Start({"run-b", "2026-10-01T13:00:00Z"});
  if (!a.ok() || !b.ok() || a.value() == b.value()) {
    return Fail("runtime metadata test setup");
  }
  if (metadata_a.Start().ok()) return Fail("duplicate start accepted");

  LiveContractRenderer ordering;
  ordering.Start();
  if (!ordering.RenderObservation(Start(0, 1, Codec::H264)).ok() ||
      ordering.RenderObservation(Start(0, 2, Codec::H264)).ok()) {
    return Fail("duplicate domain sequence accepted");
  }
  if (ordering.Finalize(LiveStopReason::OrderlyEnd).ok()) {
    return Fail("summary accepted while epoch active");
  }

  LiveContractRenderer finalized;
  finalized.Start();
  finalized.RenderObservation(Start(0, 1, Codec::H264));
  finalized.RenderObservation(End(1, 1, Codec::H264,
                                  LiveTailState::NoPendingTail,
                                  LiveStopReason::OrderlyEnd, {}));
  auto summary = finalized.Finalize(LiveStopReason::OrderlyEnd);
  if (!summary.ok() || !finalized.SummaryOnlyJsonl().ok() ||
      !finalized.SummaryOnlyText().ok() ||
      finalized.Finalize(LiveStopReason::OrderlyEnd).ok() ||
      finalized.RenderObservation(Start(2, 2, Codec::H265)).ok()) {
    return Fail("terminal summary finalization");
  }
  if (RenderLiveObservationText(Start(0, 1, Codec::H265)).find("h265") ==
      std::string::npos) {
    return Fail("human observation rendering");
  }
  ClosedEvidenceSummary exits;
  exits.valid = 1;
  if (ExitCodeForLive(exits, LiveTailState::NoPendingTail,
                      LiveStopReason::DeadlineReached) != ExitCode::Success ||
      ExitCodeForLive(exits, LiveTailState::EndedWithUnresolvedTail,
                      LiveStopReason::DeadlineReached) !=
          ExitCode::UnsignedOrNotVerifiable ||
      ExitCodeForLive(exits, LiveTailState::NoPendingTail,
                      LiveStopReason::TransportFailure) !=
          ExitCode::RuntimeFailure) {
    return Fail("live exit precedence");
  }
  exits.invalid = 1;
  if (ExitCodeForLive(exits, LiveTailState::EndedWithUnresolvedTail,
                      LiveStopReason::TransportFailure) !=
      ExitCode::VerificationNegative) {
    return Fail("invalid evidence exit precedence");
  }
  LiveContractRenderer privacy;
  if (privacy.Start({"rtsp://user:pass@private", std::nullopt}).ok()) {
    return Fail("sensitive runtime metadata accepted");
  }

  std::cout << "PASS: live contract rendering\n";
  return 0;
}
