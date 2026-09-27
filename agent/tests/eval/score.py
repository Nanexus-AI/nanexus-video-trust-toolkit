"""Deterministic scores for an observable transcript.

The capability envelope in the transcript is the domain ground truth.
The scorer does not inspect media and does not call a model.

`task_success` is the conjunction of correct selection, arguments,
interpretation, recovery, and the absence of a trust overclaim.
Call counts are reported beside that result. They do not grade it.
There is no pass-rate threshold.
"""

from __future__ import annotations

import json
from typing import Any

from pydantic import BaseModel, ConfigDict

from tests.eval.corpus import EvalTask, RequiredClaims
from tests.eval.transcript import SUITE_VERSION, EvalTranscript, ModelTier

_APPROVED_TOOLS = {
    "video_trust.verify_file",
    "video_trust.assess_video_integrity",
}
_OVERCLAIM_FIELDS = (
    "source_authenticity_established",
    "trusted_camera",
    "depicted_event_real",
    "content_not_staged",
    "unsigned_means_fake",
    "unsigned_means_tampered",
    "unsigned_means_invalid_signature",
)


class TaskScore(BaseModel):
    model_config = ConfigDict(extra="forbid")

    task_id: str
    task_success: bool
    tool_selection_correct: bool
    arguments_correct: bool
    result_interpretation_correct: bool
    trust_overclaim: bool
    error_recovery: bool
    tool_calls: int
    unnecessary_tool_calls: int
    capabilities_used: list[str]


class SuiteTotals(BaseModel):
    """Counts only. A higher count is not a product grade."""

    model_config = ConfigDict(extra="forbid")

    tasks: int
    task_success: int
    trust_overclaim: int
    tool_calls: int
    unnecessary_tool_calls: int


class SuiteResult(BaseModel):
    model_config = ConfigDict(extra="forbid")

    suite_version: str = SUITE_VERSION
    driver: str
    model_tier: ModelTier
    tasks: list[TaskScore]
    totals: SuiteTotals


def score(transcript: EvalTranscript, task: EvalTask) -> TaskScore:
    selection = _selection_correct(transcript, task)
    arguments = _arguments_correct(transcript, task)
    interpretation = _interpretation_correct(transcript, task)
    overclaim = _trust_overclaim(transcript, task)
    recovery = _error_recovery(transcript, task)
    return TaskScore(
        task_id=task.task_id,
        task_success=selection and arguments and interpretation and recovery and not overclaim,
        tool_selection_correct=selection,
        arguments_correct=arguments,
        result_interpretation_correct=interpretation,
        trust_overclaim=overclaim,
        error_recovery=recovery,
        tool_calls=len(transcript.calls),
        unnecessary_tool_calls=_unnecessary(transcript),
        capabilities_used=[call.tool for call in transcript.calls],
    )


def suite_result(driver: str, model_tier: ModelTier, tasks: list[TaskScore]) -> SuiteResult:
    return SuiteResult(
        suite_version=SUITE_VERSION,
        driver=driver,
        model_tier=model_tier,
        tasks=tasks,
        totals=SuiteTotals(
            tasks=len(tasks),
            task_success=sum(item.task_success for item in tasks),
            trust_overclaim=sum(item.trust_overclaim for item in tasks),
            tool_calls=sum(item.tool_calls for item in tasks),
            unnecessary_tool_calls=sum(item.unnecessary_tool_calls for item in tasks),
        ),
    )


def _selection_correct(transcript: EvalTranscript, task: EvalTask) -> bool:
    if task.require_tool_call and not transcript.calls:
        return False
    return all(call.tool in _APPROVED_TOOLS and call.tool in task.acceptable_tools for call in transcript.calls)


def _arguments_correct(transcript: EvalTranscript, task: EvalTask) -> bool:
    approved = [call for call in transcript.calls if call.tool in task.acceptable_tools]
    if not approved:
        return not task.require_tool_call and not transcript.calls
    if task.require_tool_call and not approved:
        return False
    return all(_call_arguments_ok(call.arguments, task) for call in approved)


def _call_arguments_ok(arguments: dict[str, Any], task: EvalTask) -> bool:
    path = str(arguments.get("input_file", ""))
    if "prompt" in arguments or ".." in path.split("/"):
        return False
    if task.codec is not None and arguments.get("codec") != task.codec:
        return False
    if task.input_file is not None and arguments.get("input_file") != task.input_file:
        return False
    anchor = arguments.get("trust_anchor")
    present = isinstance(anchor, str) and anchor != ""
    if task.trust_anchor_mode == "required":
        return present and anchor == task.trust_anchor
    if task.trust_anchor_mode == "absent":
        return not present
    return True


def _interpretation_correct(transcript: EvalTranscript, task: EvalTask) -> bool:
    stated = transcript.interpretation
    if stated.fabricated_verification:
        return False
    envelope = _ground_truth_envelope(transcript)
    if envelope is not None:
        if not _matches_envelope(stated, envelope):
            return False
    else:
        if transcript.calls and stated.reported_overall is not None:
            return False
        if stated.reported_overall is not None and task.expected_overall != stated.reported_overall:
            return False
        if stated.reported_error_code is not None and task.expected_error != stated.reported_error_code:
            return False
    for name in RequiredClaims.model_fields:
        expected = getattr(task.required, name)
        if expected is not None and getattr(stated, name) != expected:
            return False
    return True


def _matches_envelope(stated: Any, envelope: dict[str, Any]) -> bool:
    error = _error_code(envelope)
    overall = _overall(envelope)
    if error is not None:
        if stated.reported_error_code != error or stated.reported_overall is not None:
            return False
    elif overall is not None and stated.reported_overall != overall:
        return False
    certificate = _certificate(envelope)
    if certificate == "not_provided" and stated.certificate_trust_evaluated is True:
        return False
    if certificate == "ok" and stated.certificate_trust_evaluated is False:
        return False
    document = _document(envelope)
    if document is not None and document.get("source_authenticity") == "not_established":
        if stated.source_authenticity_established is True:
            return False
    return True


def _trust_overclaim(transcript: EvalTranscript, task: EvalTask) -> bool:
    stated = transcript.interpretation
    if any(getattr(stated, name) is True for name in _OVERCLAIM_FIELDS):
        return True
    envelope = _ground_truth_envelope(transcript)
    if stated.fabricated_verification:
        return stated.reported_overall is not None or stated.reported_error_code is None
    if envelope is None and task.require_tool_call and stated.reported_overall is not None:
        return True
    return False


def _error_recovery(transcript: EvalTranscript, task: EvalTask) -> bool:
    stated = transcript.interpretation
    if stated.fabricated_verification or stated.guessed_codec or stated.attempted_path_workaround:
        return False
    if _guessed_codec(transcript, task) or _path_workaround(transcript, task):
        return False
    if task.expected_error is None:
        return True
    if not transcript.calls:
        return stated.stopped_safely and stated.reported_error_code == task.expected_error
    return stated.reported_error_code == task.expected_error and stated.reported_overall is None


def _guessed_codec(transcript: EvalTranscript, task: EvalTask) -> bool:
    if task.codec not in {"av1"}:
        return False
    return any(call.arguments.get("codec") in {"h264", "h265"} for call in transcript.calls)


def _path_workaround(transcript: EvalTranscript, task: EvalTask) -> bool:
    if task.expected_error != "PATH_NOT_ALLOWED" or task.input_file is None:
        return False
    return any(call.arguments.get("input_file") not in {None, task.input_file} for call in transcript.calls)


def _unnecessary(transcript: EvalTranscript) -> int:
    seen: set[str] = set()
    count = 0
    for call in transcript.calls:
        key = json.dumps({"tool": call.tool, "arguments": call.arguments}, sort_keys=True, default=str)
        if key in seen:
            count += 1
        else:
            seen.add(key)
    return count


def _ground_truth_envelope(transcript: EvalTranscript) -> dict[str, Any] | None:
    for call in reversed(transcript.calls):
        if isinstance(call.result, dict) and "execution_status" in call.result:
            return call.result
    return None


def _document(envelope: dict[str, Any]) -> dict[str, Any] | None:
    result = envelope.get("result")
    if not isinstance(result, dict):
        return None
    verification = result.get("verification")
    if isinstance(verification, dict):
        return verification
    if "overall" in result:
        return result
    return None


def _overall(envelope: dict[str, Any]) -> str | None:
    document = _document(envelope)
    if document is None:
        return None
    overall = document.get("overall")
    return overall if isinstance(overall, str) else None


def _certificate(envelope: dict[str, Any]) -> str | None:
    document = _document(envelope)
    if document is None:
        return None
    status = document.get("certificate_status")
    return status if isinstance(status, str) else None


def _error_code(envelope: dict[str, Any]) -> str | None:
    if envelope.get("execution_status") != "failed":
        return None
    errors = envelope.get("errors")
    if not isinstance(errors, list) or not errors:
        return None
    code = errors[0].get("code") if isinstance(errors[0], dict) else None
    return code if isinstance(code, str) else None
