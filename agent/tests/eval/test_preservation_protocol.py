"""Observable M3-I evaluation cases; no hidden reasoning is scored."""

from __future__ import annotations

from copy import deepcopy

from nanexus_video_trust_agent.preservation_contracts import PreservationAssessment
from tests.support.fake_video_trust import preservation_document


def _assessment(**changes) -> PreservationAssessment:
    payload = deepcopy(preservation_document())
    for dotted, value in changes.items():
        target = payload
        parts = dotted.split("__")
        for part in parts[:-1]:
            target = target[part]
        target[parts[-1]] = value
    return PreservationAssessment.model_validate(payload)


def test_h264_ordinary_export_partial_is_incomplete_not_corruption() -> None:
    result = _assessment(
        after__verification__overall="PARTIAL",
        after__verification__verification_completeness="incomplete",
        transitions__verification__after_overall="PARTIAL",
        transitions__verification__verification_completeness="degraded",
        preservation__media_signing_evidence="partially_preserved",
    )
    assert result.after.verification.signature_integrity.value == "ok"
    assert result.after.verification.overall.value == "PARTIAL"
    assert result.after.verification.verification_completeness.value == "incomplete"


def test_h264_guarded_export_exposes_complete_valid_after_state() -> None:
    result = _assessment()
    assert result.after.verification.overall.value == "VALID"
    assert result.after.verification.verification_completeness.value == "complete"


def test_h265_structural_change_can_remain_cryptographically_valid() -> None:
    result = _assessment(correlation__stream_relation="structurally_changed")
    assert result.correlation.stream_relation == "structurally_changed"
    assert result.after.verification.signature_integrity.value == "ok"
    assert result.after.verification.overall.value == "VALID"


def test_signing_sei_removal_exposes_degradation() -> None:
    result = _assessment(
        correlation__signing_metadata__relation="missing_after",
        preservation__media_signing_evidence="not_preserved",
        transitions__verification__media_signing="degraded",
    )
    assert result.correlation.signing_metadata.relation == "missing_after"
    assert result.preservation.media_signing_evidence == "not_preserved"


def test_subset_and_unknown_coverage_are_explicit() -> None:
    assert _assessment(coverage__state="subset").coverage.state == "subset"
    assert _assessment(coverage__state="unknown").coverage.state == "unknown"


def test_unsigned_before_is_explicitly_not_applicable_and_indeterminate() -> None:
    result = _assessment(
        before__verification__media_signing="not_detected",
        before__verification__signature_integrity="not_applicable",
        before__verification__continuity="not_applicable",
        before__verification__verification_completeness="incomplete",
        before__verification__overall="UNSIGNED",
        applicability__media_signing_preservation="not_applicable",
        preservation__media_signing_evidence="indeterminate",
    )
    assert result.applicability.media_signing_preservation == "not_applicable"
    assert result.preservation.media_signing_evidence == "indeterminate"
