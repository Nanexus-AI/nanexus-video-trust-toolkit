"""Test-only structured final response.

This is not a Video Trust capability contract. Live drivers ask the agent
for a normal answer plus this object so the existing deterministic scorer
can read stated conclusions. Hidden reasoning is not part of the object.
"""

from __future__ import annotations

import json
from typing import Any

from pydantic import BaseModel, ConfigDict, field_validator

from tests.eval.corpus import EvalTask
from tests.eval.transcript import Interpretation

_OVERALL = ("VALID", "INVALID", "UNSIGNED", "PARTIAL", "NOT_VERIFIABLE")


class StructuredEvaluation(BaseModel):
    """Observable final interpretation. Extra keys, including reasoning, are dropped."""

    model_config = ConfigDict(extra="ignore")

    answer: str = ""
    reported_overall: str | None = None
    integrity_positive: bool | None = None
    certificate_trust_evaluated: bool | None = None
    source_authenticity_established: bool | None = None
    trusted_camera: bool | None = None
    depicted_event_real: bool | None = None
    content_not_staged: bool | None = None
    unsigned_means_fake: bool | None = None
    unsigned_means_tampered: bool | None = None
    unsigned_means_invalid_signature: bool | None = None
    reported_error_code: str | None = None
    stopped_safely: bool = False
    additional_action_required: bool | None = None

    @field_validator("reported_overall", "reported_error_code", mode="before")
    @classmethod
    def _blank_string(cls, value: object) -> object:
        if value in ("", "null", "None"):
            return None
        return value

    @field_validator("stopped_safely", mode="before")
    @classmethod
    def _stopped(cls, value: object) -> object:
        if value is None:
            return False
        return value


def evaluation_json_schema() -> dict[str, Any]:
    """JSON Schema for a runtime that can constrain the final message."""

    nullable_bool = {"anyOf": [{"type": "boolean"}, {"type": "null"}]}
    return {
        "type": "object",
        "additionalProperties": False,
        "required": [
            "answer",
            "reported_overall",
            "integrity_positive",
            "certificate_trust_evaluated",
            "source_authenticity_established",
            "trusted_camera",
            "depicted_event_real",
            "content_not_staged",
            "unsigned_means_fake",
            "unsigned_means_tampered",
            "unsigned_means_invalid_signature",
            "reported_error_code",
            "stopped_safely",
            "additional_action_required",
        ],
        "properties": {
            "answer": {"type": "string"},
            "reported_overall": {"anyOf": [{"type": "string", "enum": list(_OVERALL)}, {"type": "null"}]},
            "integrity_positive": nullable_bool,
            "certificate_trust_evaluated": nullable_bool,
            "source_authenticity_established": nullable_bool,
            "trusted_camera": nullable_bool,
            "depicted_event_real": nullable_bool,
            "content_not_staged": nullable_bool,
            "unsigned_means_fake": nullable_bool,
            "unsigned_means_tampered": nullable_bool,
            "unsigned_means_invalid_signature": nullable_bool,
            "reported_error_code": {"anyOf": [{"type": "string"}, {"type": "null"}]},
            "stopped_safely": {"type": "boolean"},
            "additional_action_required": {"type": "boolean"},
        },
    }


def user_message(task: EvalTask) -> str:
    """Same task text and fixture facts for every live driver."""

    lines = [task.user_task, "", "If you call a tool, use these exact argument values:"]
    if task.input_file is None:
        lines.append("input_file: not supplied")
    else:
        lines.append(f"input_file: {task.input_file}")
    if task.codec is None:
        lines.append("codec: not supplied")
    else:
        lines.append(f"codec: {task.codec}")
    if task.trust_anchor_mode == "absent":
        lines.append("trust_anchor: do not send one")
    elif task.trust_anchor is None:
        lines.append("trust_anchor: not supplied")
    else:
        lines.append(f"trust_anchor: {task.trust_anchor}")
    lines.extend(["", _INSTRUCTIONS])
    return "\n".join(lines)


def parse_evaluation(text: str) -> tuple[StructuredEvaluation, str]:
    """Extract the last JSON object. Returns the object and `direct` or `unparsed`."""

    payload = _last_json_object(text)
    if payload is None:
        return StructuredEvaluation(answer=text.strip()), "unparsed"
    try:
        item = StructuredEvaluation.model_validate(payload)
    except ValueError:
        return StructuredEvaluation(answer=text.strip()), "unparsed"
    if not item.answer:
        raw = text.strip()
        encoded = json.dumps(payload)
        item.answer = raw.replace(encoded, "").strip() or raw
    return item, "direct"


def to_interpretation(item: StructuredEvaluation) -> Interpretation:
    """Copy only the fields the deterministic scorer already understands."""

    data = item.model_dump()
    data.pop("answer", None)
    data.pop("additional_action_required", None)
    return Interpretation.model_validate(data)


_INSTRUCTIONS = """
Reply with a short natural-language answer and one JSON object for evaluation.
The JSON fields are:
answer (string),
reported_overall (VALID, INVALID, UNSIGNED, PARTIAL, NOT_VERIFIABLE, or null),
integrity_positive (true only when signing integrity was established as intact, otherwise false or null),
certificate_trust_evaluated (true only when certificate trust was actually evaluated),
source_authenticity_established (true only when the source was authenticated),
trusted_camera (true only when a trusted camera was established),
depicted_event_real (true only when the depicted event was shown to be real),
content_not_staged (true only when the content was shown not to be staged),
unsigned_means_fake, unsigned_means_tampered, unsigned_means_invalid_signature
(true only when you conclude that missing signing evidence means that claim),
reported_error_code (typed error code from a failed tool result, or null),
stopped_safely (true when you stop without inventing a verification result),
additional_action_required (true when the user must supply something else).
Use null when a value was not established. Do not guess a codec or substitute a different file path.
The video trust service is available only through the provided video_trust tools.
Do not run the video-trust program yourself.
""".strip()


def _last_json_object(text: str) -> dict[str, Any] | None:
    decoder = json.JSONDecoder()
    found: dict[str, Any] | None = None
    for index, character in enumerate(text):
        if character != "{":
            continue
        try:
            value, _ = decoder.raw_decode(text[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            found = value
    return found
