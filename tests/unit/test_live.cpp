#include "videotrust/live.hpp"
#include "videotrust/live_ingest.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace videotrust;

int Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

InspectionResult Snapshot(Codec codec, SignatureIntegrity integrity,
                          unsigned received, unsigned validated,
                          unsigned pending) {
  InspectionResult out;
  out.verification.codec = codec;
  out.verification.media_signing =
      integrity == SignatureIntegrity::NotApplicable
          ? SigningPresence::NotDetected
          : SigningPresence::Detected;
  out.verification.signature_integrity = integrity;
  out.verification.continuity = integrity == SignatureIntegrity::Ok
                                    ? ContinuityStatus::Intact
                                : integrity == SignatureIntegrity::NotOk
                                    ? ContinuityStatus::Broken
                                    : ContinuityStatus::NotApplicable;
  out.verification.completeness = pending ? VerificationCompleteness::Incomplete
                                          : VerificationCompleteness::Complete;
  out.accumulated.received_nalus = received;
  out.accumulated.received_frames = received;
  out.accumulated.validated_nalus = validated;
  out.accumulated.validated_frames = validated;
  out.accumulated.pending_nalus = pending;
  out.accumulated.pending_frames = pending;
  out.latest.pending_hashable_nalus = static_cast<int>(pending);
  return out;
}

std::vector<std::string> DeterministicTrace() {
  LiveSemanticModel model;
  std::vector<std::string> trace;
  auto started = model.BeginEpoch(Codec::H264);
  trace.push_back(std::string(ToString(started.value().kind)) + ":" +
                  ToString(started.value().tail));
  const std::vector<InspectionResult> snapshots{
      Snapshot(Codec::H264, SignatureIntegrity::NotFeasible, 4, 0, 4),
      Snapshot(Codec::H264, SignatureIntegrity::Ok, 24, 20, 4),
      Snapshot(Codec::H264, SignatureIntegrity::Ok, 24, 20, 4),
      Snapshot(Codec::H264, SignatureIntegrity::Ok, 24, 24, 0),
  };
  for (const auto& snapshot : snapshots) {
    auto event = model.Observe(snapshot);
    if (event.value().has_value()) {
      const auto& value = *event.value();
      trace.push_back(std::string(ToString(value.kind)) + ":" +
                      ToString(value.tail) + ":" +
                      (value.newly_closed
                           ? ToString(value.newly_closed->outcome)
                           : "none"));
    }
  }
  auto ended = model.EndEpoch(LiveStopReason::OrderlyEnd);
  trace.push_back(std::string(ToString(ended.value().kind)) + ":" +
                  ToString(ended.value().tail) + ":" +
                  ToString(ended.value().stop_reason));
  return trace;
}

}  // namespace

int main() {
  using namespace videotrust;

  if (!LiveSampleSizeWithinBound(kLiveMaxSampleBytes - 1) ||
      !LiveSampleSizeWithinBound(kLiveMaxSampleBytes) ||
      LiveSampleSizeWithinBound(kLiveMaxSampleBytes + 1) ||
      LiveSampleSizeWithinBound(kLiveMaxSampleBytes + 1024 * 1024) ||
      LiveSampleSizeWithinBound(0)) {
    return Fail("live sample size boundary");
  }

  LiveSemanticModel model;
  auto started = model.BeginEpoch(Codec::H264);
  if (!started.ok() || started.value().tail != LiveTailState::AwaitingEvidence ||
      started.value().epoch != 1) {
    return Fail("epoch start");
  }

  auto pending = model.Observe(
      Snapshot(Codec::H264, SignatureIntegrity::NotFeasible, 5, 0, 5));
  if (!pending.ok() || !pending.value() ||
      pending.value()->tail != LiveTailState::OpenPending ||
      pending.value()->newly_closed.has_value()) {
    return Fail("awaiting to open pending");
  }

  auto valid_pending = model.Observe(
      Snapshot(Codec::H264, SignatureIntegrity::Ok, 25, 20, 5));
  if (!valid_pending.ok() || !valid_pending.value() ||
      !valid_pending.value()->newly_closed ||
      valid_pending.value()->newly_closed->outcome !=
          ClosedEvidenceOutcome::Valid ||
      valid_pending.value()->tail != LiveTailState::OpenPending) {
    return Fail("closed valid plus pending");
  }

  auto duplicate = model.Observe(
      Snapshot(Codec::H264, SignatureIntegrity::Ok, 25, 20, 5));
  if (!duplicate.ok() || duplicate.value().has_value()) {
    return Fail("duplicate report suppression");
  }

  auto ended = model.EndEpoch(LiveStopReason::TransportFailure);
  if (!ended.ok() ||
      ended.value().tail != LiveTailState::EndedWithUnresolvedTail ||
      ended.value().stop_reason != LiveStopReason::TransportFailure ||
      ended.value().closed_summary.valid != 1 ||
      model.latest_closed()->outcome != ClosedEvidenceOutcome::Valid) {
    return Fail("final unresolved preserves valid and transport axis");
  }

  auto next = model.BeginEpoch(Codec::H265);
  if (!next.ok() || next.value().epoch != 2 ||
      next.value().tail != LiveTailState::AwaitingEvidence) {
    return Fail("new epoch boundary");
  }
  auto invalid = model.Observe(
      Snapshot(Codec::H265, SignatureIntegrity::NotOk, 10, 10, 0));
  if (!invalid.ok() || !invalid.value() ||
      invalid.value()->newly_closed->outcome !=
          ClosedEvidenceOutcome::Invalid) {
    return Fail("definitive invalid");
  }
  auto deadline = model.EndEpoch(LiveStopReason::DeadlineReached);
  if (!deadline.ok() || deadline.value().closed_summary.valid != 1 ||
      deadline.value().closed_summary.invalid != 1 ||
      deadline.value().stop_reason != LiveStopReason::DeadlineReached) {
    return Fail("invalid survives deadline");
  }

  LiveSemanticModel unsigned_model;
  unsigned_model.BeginEpoch(Codec::H264);
  auto unsigned_active = unsigned_model.Observe(
      Snapshot(Codec::H264, SignatureIntegrity::NotApplicable, 20, 0, 0));
  if (!unsigned_active.ok() || unsigned_active.value().has_value()) {
    return Fail("unsigned not asserted while active");
  }
  auto unsigned_end = unsigned_model.EndEpoch(LiveStopReason::OrderlyEnd);
  if (!unsigned_end.ok() || !unsigned_end.value().newly_closed ||
      unsigned_end.value().newly_closed->outcome !=
          ClosedEvidenceOutcome::Unsigned ||
      unsigned_end.value().tail != LiveTailState::NoPendingTail) {
    return Fail("unsigned asserted from final official evidence");
  }

  LiveSemanticModel nv_model;
  nv_model.BeginEpoch(Codec::H265);
  nv_model.Observe(
      Snapshot(Codec::H265, SignatureIntegrity::NotFeasible, 20, 0, 0));
  auto nv_end = nv_model.EndEpoch(LiveStopReason::CallerCancelled);
  if (!nv_end.ok() || !nv_end.value().newly_closed ||
      nv_end.value().newly_closed->outcome !=
          ClosedEvidenceOutcome::NotVerifiable ||
      nv_end.value().stop_reason != LiveStopReason::CallerCancelled) {
    return Fail("not verifiable remains separate from cancellation");
  }

  LiveSemanticModel repetition;
  repetition.BeginEpoch(Codec::H264);
  auto parameter_only = repetition.Observe(
      Snapshot(Codec::H264, SignatureIntegrity::NotFeasible, 0, 0, 0));
  if (!parameter_only.ok() || parameter_only.value().has_value() ||
      repetition.epoch_index() != 1) {
    return Fail("parameter repetition does not reset or emit");
  }

  LiveSemanticModel bounded;
  bounded.BeginEpoch(Codec::H264);
  auto many_findings = Snapshot(Codec::H264, SignatureIntegrity::Ok, 20, 20, 0);
  for (int i = 0; i < 32; ++i) {
    many_findings.verification.findings.push_back(
        {"BOUNDED", "synthetic bounded finding"});
  }
  auto bounded_event = bounded.Observe(many_findings);
  if (!bounded_event.ok() || !bounded_event.value() ||
      !bounded_event.value()->newly_closed ||
      bounded_event.value()->newly_closed->verification.findings.size() != 16) {
    return Fail("bounded live findings");
  }

  if (DeterministicTrace() != DeterministicTrace()) {
    return Fail("semantic trace determinism");
  }

  std::cout << "PASS: live semantic model\n";
  return 0;
}
