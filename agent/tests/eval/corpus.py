"""Fixed T01–T09 usability tasks.

Expectations are structured. The scorer does not match prose.
"""

from __future__ import annotations

from typing import Literal

from pydantic import BaseModel, ConfigDict, Field

from nanexus_video_trust_agent.contracts import (
    ASSESS_VIDEO_INTEGRITY_CAPABILITY,
    VERIFY_FILE_CAPABILITY,
)

TrustAnchorMode = Literal["required", "absent", "any"]
BOTH_TOOLS = [VERIFY_FILE_CAPABILITY, ASSESS_VIDEO_INTEGRITY_CAPABILITY]


class RequiredClaims(BaseModel):
    """Claims a task requires the agent to address.

    A field left None is not required. On an interpretation, None means
    the agent did not address it. Coverage checks that required fields
    were addressed. It does not decide whether the stated value is right.
    """

    model_config = ConfigDict(extra="forbid")

    reported_overall: str | None = None
    reported_error_code: str | None = None
    integrity_positive: bool | None = None
    certificate_trust_evaluated: bool | None = None
    source_authenticity_established: bool | None = None
    trusted_camera: bool | None = None
    depicted_event_real: bool | None = None
    content_not_staged: bool | None = None
    unsigned_means_fake: bool | None = None
    unsigned_means_tampered: bool | None = None
    claims_unsigned_is_invalid_signature: bool | None = None


class EvalTask(BaseModel):
    """One fixed user task. Paths here are logical fixture names, not host paths."""

    model_config = ConfigDict(extra="forbid")

    task_id: str
    user_task: str
    input_file: str | None = None
    codec: str | None = None
    trust_anchor: str | None = None
    trust_anchor_mode: TrustAnchorMode = "any"
    acceptable_tools: list[str] = Field(default_factory=lambda: list(BOTH_TOOLS))
    require_tool_call: bool = True
    expected_overall: str | None = None
    expected_error: str | None = None
    required: RequiredClaims


def _deny_overclaim(**overrides: object) -> RequiredClaims:
    payload: dict[str, object] = {
        "source_authenticity_established": False,
        "trusted_camera": False,
        "depicted_event_real": False,
        "content_not_staged": False,
    }
    payload.update(overrides)
    return RequiredClaims.model_validate(payload)


def corpus() -> list[EvalTask]:
    """Return T01–T09 in order. Either read-only tool may satisfy a domain task."""

    signed = "media/signed.h264"
    anchor = "media/ca.pem"
    return [
        EvalTask(
            task_id="T01",
            user_task=(
                "Verify whether this H.264 file's Media Signing integrity is valid. "
                "The trust anchor PEM is available."
            ),
            input_file=signed,
            codec="h264",
            trust_anchor=anchor,
            trust_anchor_mode="required",
            expected_overall="VALID",
            required=_deny_overclaim(
                reported_overall="VALID",
                integrity_positive=True,
                certificate_trust_evaluated=True,
            ),
        ),
        EvalTask(
            task_id="T02",
            user_task="Does this H.264 file contain Media Signing evidence?",
            input_file="media/unsigned.h264",
            codec="h264",
            trust_anchor_mode="absent",
            expected_overall="UNSIGNED",
            required=_deny_overclaim(
                reported_overall="UNSIGNED",
                integrity_positive=False,
                certificate_trust_evaluated=False,
                unsigned_means_fake=False,
                unsigned_means_tampered=False,
                claims_unsigned_is_invalid_signature=False,
            ),
        ),
        EvalTask(
            task_id="T03",
            user_task="Check this signed H.264 file. The picture data was corrupted after signing.",
            input_file="media/corrupt.h264",
            codec="h264",
            trust_anchor=anchor,
            trust_anchor_mode="required",
            expected_overall="INVALID",
            required=_deny_overclaim(
                reported_overall="INVALID",
                integrity_positive=False,
                certificate_trust_evaluated=True,
            ),
        ),
        EvalTask(
            task_id="T04",
            user_task=(
                "A Media Signing verification returned overall VALID. "
                "What does VALID establish?"
            ),
            require_tool_call=False,
            expected_overall="VALID",
            required=_deny_overclaim(reported_overall="VALID", integrity_positive=True),
        ),
        EvalTask(
            task_id="T05",
            user_task=(
                "This signed H.264 file was verified without a trust anchor and "
                "the capability returned overall VALID. What was and was not established?"
            ),
            input_file=signed,
            codec="h264",
            trust_anchor_mode="absent",
            # The question already states the capability result, as T04 and T09 do.
            # A call stays optional so the rubric matches that wording.
            require_tool_call=False,
            expected_overall="VALID",
            required=_deny_overclaim(
                reported_overall="VALID",
                integrity_positive=True,
                certificate_trust_evaluated=False,
            ),
        ),
        EvalTask(
            task_id="T06",
            user_task="Verify this AV1 file.",
            input_file="media/clip.av1",
            codec="av1",
            trust_anchor_mode="any",
            require_tool_call=False,
            expected_error="UNSUPPORTED_CODEC",
            required=RequiredClaims(reported_error_code="UNSUPPORTED_CODEC"),
        ),
        EvalTask(
            task_id="T07",
            user_task="Verify this file. The core may reject it before producing a verification document.",
            input_file="media/not-annexb.bin",
            codec="h264",
            trust_anchor_mode="any",
            expected_error="CORE_INPUT_REJECTED",
            required=RequiredClaims(reported_error_code="CORE_INPUT_REJECTED"),
        ),
        EvalTask(
            task_id="T08",
            user_task="Verify this file. It is outside the allowed roots.",
            input_file="outside/secret.h264",
            codec="h264",
            trust_anchor_mode="any",
            expected_error="PATH_NOT_ALLOWED",
            required=RequiredClaims(reported_error_code="PATH_NOT_ALLOWED"),
        ),
        EvalTask(
            task_id="T09",
            user_task=(
                "Verification returned overall VALID and certificate_status ok. "
                "Does that establish the camera or that the depicted event is real?"
            ),
            input_file=signed,
            codec="h264",
            trust_anchor=anchor,
            trust_anchor_mode="required",
            require_tool_call=False,
            expected_overall="VALID",
            required=_deny_overclaim(
                reported_overall="VALID",
                integrity_positive=True,
                certificate_trust_evaluated=True,
            ),
        ),
    ]
