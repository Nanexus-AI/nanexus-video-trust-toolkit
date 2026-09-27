"""Observable evaluation transcript.

Records the user task, tool calls, tool results, and the agent's stated
conclusions. It does not record hidden reasoning, secrets, or the process
environment.
"""

from __future__ import annotations

from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field

SUITE_VERSION = "0.1"
ModelTier = Literal["frontier", "mid", "small"]


class Interpretation(BaseModel):
    """Conclusions visible in the final answer. Omitted means not stated."""

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
    unsigned_means_invalid_signature: bool | None = None
    fabricated_verification: bool = False
    guessed_codec: bool = False
    attempted_path_workaround: bool = False
    stopped_safely: bool = False


class ToolCallRecord(BaseModel):
    """One observable tool call and the capability envelope it returned."""

    model_config = ConfigDict(extra="forbid")

    tool: str
    arguments: dict[str, Any]
    result: dict[str, Any] | None = None
    is_error: bool = False


class EvalTranscript(BaseModel):
    model_config = ConfigDict(extra="forbid")

    suite_version: Literal["0.1"] = SUITE_VERSION
    task_id: str
    user_task: str
    model_tier: ModelTier
    driver: str
    available_tools: list[str]
    calls: list[ToolCallRecord] = Field(default_factory=list)
    interpretation: Interpretation
