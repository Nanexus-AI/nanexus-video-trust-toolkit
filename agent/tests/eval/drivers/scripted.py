"""Replay a fixed agent script. No model is contacted.

When an executor is supplied, each scripted call is sent to that callable
and the returned envelope replaces the canned result. The interpretation
remains the script, so scoring stays deterministic.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from pydantic import BaseModel, ConfigDict

from tests.eval.corpus import BOTH_TOOLS, EvalTask, RequiredClaims
from tests.eval.transcript import SUITE_VERSION, EvalTranscript, Interpretation, ModelTier, ToolCallRecord

ToolExecutor = Callable[[str, dict[str, Any]], tuple[dict[str, Any], bool]]


class Script(BaseModel):
    model_config = ConfigDict(extra="forbid")

    name: str
    calls: list[ToolCallRecord]
    interpretation: Interpretation


class ScriptedDriver:
    """Deterministic driver. `tier` labels the script, not a model size."""

    def __init__(
        self,
        script: Script,
        *,
        tier: ModelTier = "small",
        executor: ToolExecutor | None = None,
    ) -> None:
        self.name = f"scripted:{script.name}"
        self.tier: ModelTier = tier
        self._script = script
        self._executor = executor

    def run(self, task: EvalTask) -> EvalTranscript:
        calls: list[ToolCallRecord] = []
        for planned in self._script.calls:
            result = planned.result
            is_error = planned.is_error
            if self._executor is not None:
                result, is_error = self._executor(planned.tool, planned.arguments)
            calls.append(
                ToolCallRecord(
                    tool=planned.tool,
                    arguments=planned.arguments,
                    result=result,
                    is_error=is_error,
                )
            )
        return EvalTranscript(
            suite_version=SUITE_VERSION,
            task_id=task.task_id,
            user_task=task.user_task,
            model_tier=self.tier,
            driver=self.name,
            available_tools=list(BOTH_TOOLS),
            calls=calls,
            interpretation=self._script.interpretation,
        )


def perfect(task: EvalTask, *, tool: str | None = None) -> Script:
    """A careful agent: one appropriate call, or none when a call is optional, and the required claims."""

    selected = tool or task.acceptable_tools[0]
    calls: list[ToolCallRecord] = []
    if task.require_tool_call or tool is not None:
        calls.append(_call(task, selected))
    return Script(name=f"perfect-{task.task_id}", calls=calls, interpretation=_perfect_interpretation(task))


def wrong_tool(task: EvalTask) -> Script:
    return Script(
        name=f"wrong-tool-{task.task_id}",
        calls=[
            ToolCallRecord(
                tool="video_trust.sign",
                arguments=_arguments(task),
                result=None,
                is_error=True,
            )
        ],
        interpretation=_perfect_interpretation(task),
    )


def wrong_codec(task: EvalTask) -> Script:
    arguments = _arguments(task)
    arguments["codec"] = "h265" if task.codec != "h265" else "h264"
    return Script(
        name=f"wrong-codec-{task.task_id}",
        calls=[_call(task, task.acceptable_tools[0], arguments=arguments)],
        interpretation=_perfect_interpretation(task),
    )


def overclaim(task: EvalTask) -> Script:
    stated = _perfect_interpretation(task)
    stated.depicted_event_real = True
    stated.trusted_camera = True
    return Script(
        name=f"overclaim-{task.task_id}",
        calls=[_call(task, task.acceptable_tools[0])] if task.input_file else [],
        interpretation=stated,
    )


def unnecessary_retry(task: EvalTask) -> Script:
    call = _call(task, task.acceptable_tools[0])
    return Script(
        name=f"retry-{task.task_id}",
        calls=[call, call],
        interpretation=_perfect_interpretation(task),
    )


def hallucinated(task: EvalTask) -> Script:
    return Script(
        name=f"hallucinated-{task.task_id}",
        calls=[],
        interpretation=Interpretation(
            reported_overall="VALID",
            integrity_positive=True,
            source_authenticity_established=True,
            fabricated_verification=True,
        ),
    )


def _perfect_interpretation(task: EvalTask) -> Interpretation:
    stated = Interpretation(stopped_safely=task.expected_error is not None)
    for name in RequiredClaims.model_fields:
        value = getattr(task.required, name)
        if value is not None:
            setattr(stated, name, value)
    return stated


def _arguments(task: EvalTask) -> dict[str, Any]:
    arguments: dict[str, Any] = {}
    if task.input_file is not None:
        arguments["input_file"] = task.input_file
    if task.codec is not None:
        arguments["codec"] = task.codec
    if task.trust_anchor_mode == "required" and task.trust_anchor is not None:
        arguments["trust_anchor"] = task.trust_anchor
    return arguments


def _call(task: EvalTask, tool: str, arguments: dict[str, Any] | None = None) -> ToolCallRecord:
    payload = dict(_arguments(task) if arguments is None else arguments)
    if task.expected_error is not None:
        result = {
            "execution_status": "failed",
            "capability": tool,
            "result": None,
            "errors": [{"code": task.expected_error, "message": "typed failure", "retryable": False}],
            "limitations": [],
            "evidence": {},
        }
        return ToolCallRecord(tool=tool, arguments=payload, result=result, is_error=True)
    result = {
        "execution_status": "success",
        "capability": tool,
        "result": _domain_result(task, tool),
        "errors": [],
        "limitations": [],
        "evidence": {},
    }
    return ToolCallRecord(tool=tool, arguments=payload, result=result, is_error=False)


def _domain_result(task: EvalTask, tool: str) -> dict[str, Any]:
    certificate = "not_provided"
    if task.required.certificate_trust_evaluated is True:
        certificate = "ok"
    document = {
        "overall": task.expected_overall,
        "certificate_status": certificate,
        "source_authenticity": "not_established",
        "media_signing": {"present": task.expected_overall != "UNSIGNED"},
    }
    if tool == "video_trust.assess_video_integrity":
        return {"verification": document}
    return document
