"""Local Ollama chat driver. Evaluation only.

The Ollama process is an external runtime. This module is not imported by
the capability service or the MCP server.
"""

from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen

from mcp import StdioServerParameters
from mcp.client.session import ClientSession
from mcp.client.stdio import stdio_client
from pydantic import BaseModel, ConfigDict

from tests.eval.corpus import EvalTask
from tests.eval.response import parse_evaluation, to_interpretation, user_message
from tests.eval.transcript import SUITE_VERSION, EvalTranscript, ToolCallRecord

_VISIBLE_MESSAGE_KEYS = ("role", "content", "tool_calls")


class OllamaSettings(BaseModel):
    """Hardware-independent settings recorded with a live run."""

    model_config = ConfigDict(extra="forbid")

    host: str = "http://127.0.0.1:11434"
    model: str
    quantization: str | None = None
    context_tokens: int = 8192
    temperature: float = 0
    seed: int = 1
    think: bool = False
    tool_calling_mode: str = "ollama chat tools API"
    max_rounds: int = 6
    request_timeout_seconds: float = 180


class OllamaChatDriver:
    """One local model, the shared MCP server, and the shared scorer contract."""

    tier = "small"

    def __init__(
        self,
        settings: OllamaSettings,
        *,
        python: str,
        video_trust: str,
        allowed_root: str,
        stderr_path: str,
    ) -> None:
        self.settings = settings
        self.name = f"ollama:{settings.model}"
        self._python = python
        self._video_trust = video_trust
        self._allowed_root = allowed_root
        self._stderr_path = stderr_path
        self.last_answer = ""
        self.last_additional_action: bool | None = None
        self.last_parse_mode = "unparsed"

    def run(self, task: EvalTask) -> EvalTranscript:
        return asyncio.run(self._run(task))

    async def _run(self, task: EvalTask) -> EvalTranscript:
        env = _server_env(self._video_trust, self._allowed_root)
        params = StdioServerParameters(
            command=self._python,
            args=["-m", "nanexus_video_trust_agent.mcp_server"],
            env=env,
        )
        with Path(self._stderr_path).open("w", encoding="utf-8") as err:
            async with stdio_client(params, errlog=err) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    listed = await session.list_tools()
                    visible = [tool.name for tool in listed.tools]
                    tools = [_ollama_tool(tool.name, tool.description, tool.input_schema) for tool in listed.tools]
                    messages: list[dict[str, Any]] = [{"role": "user", "content": user_message(task)}]
                    calls: list[ToolCallRecord] = []
                    final = ""
                    for _ in range(self.settings.max_rounds):
                        message = _chat(self.settings, messages, tools)
                        tool_calls = message.get("tool_calls") or []
                        content = message.get("content") if isinstance(message.get("content"), str) else ""
                        messages.append(_visible_message(message))
                        if not tool_calls:
                            final = content
                            break
                        for call in tool_calls:
                            name, arguments = _tool_call_parts(call)
                            record = await _invoke(session, name, arguments)
                            calls.append(record)
                            messages.append(
                                {
                                    "role": "tool",
                                    "tool_name": name,
                                    "content": json.dumps(record.result if record.result is not None else {}),
                                }
                            )
                    else:
                        final = content
        evaluation, mode = parse_evaluation(final)
        if mode == "unparsed":
            messages.append({"role": "user", "content": "Reply with only the JSON evaluation object."})
            reminder = _chat(self.settings, messages, [])
            reminded = reminder.get("content")
            final = reminded if isinstance(reminded, str) else ""
            evaluation, mode = parse_evaluation(final)
            if mode == "direct":
                mode = "format_reminder"
        self.last_answer = evaluation.answer
        self.last_additional_action = evaluation.additional_action_required
        self.last_parse_mode = mode
        return EvalTranscript(
            suite_version=SUITE_VERSION,
            task_id=task.task_id,
            user_task=task.user_task,
            model_tier=self.tier,
            driver=self.name,
            available_tools=visible,
            calls=calls,
            interpretation=to_interpretation(evaluation),
        )


def _server_env(video_trust: str, allowed_root: str) -> dict[str, str]:
    env = {
        name: os.environ[name]
        for name in ("PATH", "LD_LIBRARY_PATH", "LANG", "LC_ALL", "HOME")
        if name in os.environ
    }
    env["NANEXUS_VIDEO_TRUST"] = video_trust
    env["NANEXUS_ALLOWED_ROOTS"] = allowed_root
    return env


def _ollama_tool(name: str, description: str | None, parameters: dict[str, Any]) -> dict[str, Any]:
    return {
        "type": "function",
        "function": {
            "name": name,
            "description": description or "",
            "parameters": parameters,
        },
    }


def _visible_message(message: dict[str, Any]) -> dict[str, Any]:
    """Keep the tool loop free of provider thinking traces."""

    visible = {key: message[key] for key in _VISIBLE_MESSAGE_KEYS if key in message}
    if "content" not in visible:
        visible["content"] = ""
    return visible


def _tool_call_parts(call: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    function = call.get("function") if isinstance(call.get("function"), dict) else call
    name = str(function.get("name") or "")
    arguments = function.get("arguments")
    if isinstance(arguments, str):
        try:
            arguments = json.loads(arguments)
        except json.JSONDecodeError:
            arguments = {}
    if not isinstance(arguments, dict):
        arguments = {}
    return name, arguments


def _chat(settings: OllamaSettings, messages: list[dict[str, Any]], tools: list[dict[str, Any]]) -> dict[str, Any]:
    payload = {
        "model": settings.model,
        "stream": False,
        "think": settings.think,
        "messages": messages,
        "tools": tools,
        "keep_alive": "30m",
        "options": {
            "temperature": settings.temperature,
            "num_ctx": settings.context_tokens,
            "seed": settings.seed,
        },
    }
    request = Request(
        f"{settings.host.rstrip('/')}/api/chat",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(request, timeout=settings.request_timeout_seconds) as response:
            body = json.loads(response.read().decode("utf-8"))
    except (URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise RuntimeError("Ollama chat request failed") from exc
    message = body.get("message")
    if not isinstance(message, dict):
        raise RuntimeError("Ollama chat response had no message")
    return message


async def _invoke(session: ClientSession, name: str, arguments: dict[str, Any]) -> ToolCallRecord:
    try:
        result = await session.call_tool(name, arguments)
    except Exception:
        return ToolCallRecord(tool=name, arguments=arguments, result=None, is_error=True)
    envelope = result.structured_content if isinstance(result.structured_content, dict) else _envelope_from_text(result)
    return ToolCallRecord(
        tool=name,
        arguments=arguments,
        result=envelope,
        is_error=bool(result.is_error),
    )


def _envelope_from_text(result: Any) -> dict[str, Any] | None:
    content = getattr(result, "content", None)
    if not content:
        return None
    text = getattr(content[0], "text", None)
    if not isinstance(text, str):
        return None
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return None
    return parsed if isinstance(parsed, dict) else None


def runtime_metadata(settings: OllamaSettings) -> dict[str, Any]:
    version = None
    discovered: dict[str, Any] = {}
    host = settings.host.rstrip("/")
    try:
        with urlopen(f"{host}/api/version", timeout=5) as response:
            payload = json.loads(response.read().decode("utf-8"))
        if isinstance(payload, dict):
            version = payload.get("version")
    except (URLError, TimeoutError, json.JSONDecodeError):
        version = None
    try:
        request = Request(
            f"{host}/api/show",
            data=json.dumps({"name": settings.model}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urlopen(request, timeout=10) as response:
            shown = json.loads(response.read().decode("utf-8"))
        details = shown.get("details") if isinstance(shown, dict) else None
        info = shown.get("model_info") if isinstance(shown, dict) else None
        if isinstance(details, dict):
            discovered["parameter_size"] = details.get("parameter_size")
            discovered["quantization"] = details.get("quantization_level")
            discovered["family"] = details.get("family")
        if isinstance(info, dict):
            for key, value in info.items():
                if str(key).endswith("context_length"):
                    discovered["model_context_tokens"] = value
    except (URLError, TimeoutError, json.JSONDecodeError):
        discovered = {}
    return {
        "provider": "Ollama",
        "runtime": version,
        "model": settings.model,
        "quantization": discovered.get("quantization", settings.quantization),
        "parameter_size": discovered.get("parameter_size"),
        "family": discovered.get("family"),
        "model_context_tokens": discovered.get("model_context_tokens"),
        "context_tokens": settings.context_tokens,
        "tool_calling_mode": settings.tool_calling_mode,
        "think": settings.think,
        "temperature": settings.temperature,
        "seed": settings.seed,
        "python": sys.version.split()[0],
    }
