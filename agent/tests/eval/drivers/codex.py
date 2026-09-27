"""Codex CLI driver. Evaluation only.

Codex is an external agent runtime. This module does not add a provider
SDK to the capability service or the MCP server, and it does not read or
store credentials.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any

from pydantic import BaseModel, ConfigDict

from tests.eval.corpus import BOTH_TOOLS, EvalTask
from tests.eval.response import evaluation_json_schema, parse_evaluation, to_interpretation, user_message
from tests.eval.transcript import SUITE_VERSION, EvalTranscript, ToolCallRecord

_SKIPPED_ITEM_TYPES = {"reasoning", "reasoning_summary"}


class CodexSettings(BaseModel):
    model_config = ConfigDict(extra="forbid")

    executable: str = "codex"
    model: str
    sandbox: str = "read-only"
    timeout_seconds: float = 240
    tool_calling_mode: str = "MCP stdio tools attached to codex exec"


class CodexExecDriver:
    """Frontier agent path through the same MCP server."""

    tier = "frontier"

    def __init__(
        self,
        settings: CodexSettings,
        *,
        python: str,
        video_trust: str,
        allowed_root: str,
        work_dir: str,
    ) -> None:
        self.settings = settings
        self.name = f"codex:{settings.model}"
        self._python = python
        self._video_trust = video_trust
        self._allowed_root = allowed_root
        self._work_dir = work_dir
        self.last_answer = ""
        self.last_additional_action: bool | None = None
        self.last_parse_mode = "unparsed"

    def run(self, task: EvalTask) -> EvalTranscript:
        work = Path(self._work_dir)
        schema_path = work / f"{task.task_id}-schema.json"
        last_path = work / f"{task.task_id}-last.txt"
        schema_path.write_text(json.dumps(evaluation_json_schema()), encoding="utf-8")
        command = self._command(schema_path, last_path, task)
        try:
            completed = subprocess.run(
                command,
                cwd=self._work_dir,
                stdin=subprocess.DEVNULL,
                capture_output=True,
                text=True,
                timeout=self.settings.timeout_seconds,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError(f"Codex exec timed out on {task.task_id}") from exc
        calls, final_text = calls_from_codex_events(completed.stdout)
        if last_path.is_file():
            written = last_path.read_text(encoding="utf-8").strip()
            if written:
                final_text = written
        if completed.returncode != 0 and not final_text and not calls:
            raise RuntimeError(f"Codex exec failed on {task.task_id}")
        evaluation, mode = parse_evaluation(final_text)
        self.last_answer = evaluation.answer
        self.last_additional_action = evaluation.additional_action_required
        self.last_parse_mode = mode
        return EvalTranscript(
            suite_version=SUITE_VERSION,
            task_id=task.task_id,
            user_task=task.user_task,
            model_tier=self.tier,
            driver=self.name,
            available_tools=[*BOTH_TOOLS, "codex.command"],
            calls=calls,
            interpretation=to_interpretation(evaluation),
        )

    def _command(self, schema_path: Path, last_path: Path, task: EvalTask) -> list[str]:
        return [
            self.settings.executable,
            "exec",
            "--ephemeral",
            "--json",
            "--skip-git-repo-check",
            "--ignore-rules",
            "--color",
            "never",
            "--sandbox",
            self.settings.sandbox,
            "-m",
            self.settings.model,
            "-C",
            self._work_dir,
            "-c",
            f"mcp_servers.video_trust.command={json.dumps(self._python)}",
            "-c",
            'mcp_servers.video_trust.args=["-m","nanexus_video_trust_agent.mcp_server"]',
            "-c",
            f"mcp_servers.video_trust.env.NANEXUS_VIDEO_TRUST={json.dumps(self._video_trust)}",
            "-c",
            f"mcp_servers.video_trust.env.NANEXUS_ALLOWED_ROOTS={json.dumps(self._allowed_root)}",
            "--output-schema",
            str(schema_path),
            "-o",
            str(last_path),
            user_message(task),
        ]


def calls_from_codex_events(stdout: str) -> tuple[list[ToolCallRecord], str]:
    """Observable tool calls and the final answer. Reasoning items are ignored."""

    calls: list[ToolCallRecord] = []
    final = ""
    for line in stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(event, dict) or event.get("type") != "item.completed":
            continue
        item = event.get("item")
        if not isinstance(item, dict):
            continue
        item_type = str(item.get("type") or "")
        if item_type in _SKIPPED_ITEM_TYPES or "reason" in item_type:
            continue
        if item_type == "agent_message":
            text = item.get("text")
            if isinstance(text, str):
                final = text
            continue
        record = _tool_record(item)
        if record is not None:
            calls.append(record)
    return calls, final


def _tool_record(item: dict[str, Any]) -> ToolCallRecord | None:
    item_type = str(item.get("type") or "")
    if item_type == "mcp_tool_call":
        arguments = item.get("arguments")
        if not isinstance(arguments, dict):
            arguments = {}
        envelope = _envelope(item.get("result"))
        status = str(item.get("status") or "")
        failed = status not in {"", "completed", "success"}
        if isinstance(envelope, dict) and envelope.get("execution_status") == "failed":
            failed = True
        return ToolCallRecord(
            tool=str(item.get("tool") or ""),
            arguments=arguments,
            result=envelope,
            is_error=failed or item.get("error") is not None,
        )
    if item_type in {"command_execution", "shell", "local_shell", "exec"}:
        command = item.get("command") or item.get("cmd") or ""
        return ToolCallRecord(
            tool="codex.command",
            arguments={"command": command if isinstance(command, str) else ""},
            result={"status": item.get("status"), "exit_code": item.get("exit_code")},
            is_error=item.get("exit_code") not in {None, 0},
        )
    return None


def _envelope(result: object) -> dict[str, Any] | None:
    if isinstance(result, dict):
        content = result.get("content")
        if isinstance(content, list):
            for block in content:
                if isinstance(block, dict) and isinstance(block.get("text"), str):
                    try:
                        parsed = json.loads(block["text"])
                    except json.JSONDecodeError:
                        continue
                    if isinstance(parsed, dict):
                        return parsed
        if "execution_status" in result:
            return result
    return None


def runtime_metadata(settings: CodexSettings) -> dict[str, Any]:
    completed = subprocess.run(
        [settings.executable, "--version"],
        capture_output=True,
        text=True,
        check=False,
    )
    version = completed.stdout.strip() or None
    return {
        "provider": "OpenAI Codex CLI",
        "runtime": version,
        "model": settings.model,
        "tool_calling_mode": settings.tool_calling_mode,
        "sandbox": settings.sandbox,
        "context_tokens": 272000 if settings.model == "gpt-5.6-sol" else None,
        "auth": "existing local Codex login; credentials are not read by this driver",
        "session": "ephemeral",
    }
