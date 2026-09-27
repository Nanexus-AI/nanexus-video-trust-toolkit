"""Unit tests for live-driver parsing. No model is contacted."""

from __future__ import annotations

import json
from pathlib import Path

from tests.eval.corpus import corpus
from tests.eval.drivers.codex import calls_from_codex_events
from tests.eval.drivers.ollama import _visible_message
from tests.eval.response import evaluation_json_schema, parse_evaluation, to_interpretation, user_message
from tests.eval.run_live import _sanitize, bind_task


def test_structured_response_uses_the_last_object_and_drops_reasoning() -> None:
    text = """
The signature check failed.
{"answer":"draft","reported_overall":"VALID","reasoning":"hidden chain"}
{"answer":"Integrity failed.","reported_overall":"INVALID","integrity_positive":false,
 "stopped_safely":true,"additional_action_required":false,"reasoning":"still hidden"}
"""
    item, mode = parse_evaluation(text)
    assert mode == "direct"
    assert item.answer == "Integrity failed."
    assert item.reported_overall == "INVALID"
    assert item.additional_action_required is False
    interpretation = to_interpretation(item)
    dumped = json.dumps(interpretation.model_dump())
    assert "reasoning" not in dumped
    assert "hidden" not in dumped
    assert interpretation.reported_overall == "INVALID"


def test_user_message_keeps_the_corpus_task_text() -> None:
    task = corpus()[0]
    message = user_message(task)
    assert message.startswith(task.user_task)
    assert "video_trust" in message
    schema = evaluation_json_schema()
    assert schema["additionalProperties"] is False
    assert "trusted_camera" in schema["properties"]
    assert "depicted_event_real" in schema["properties"]


def test_codex_events_keep_tool_results_and_skip_reasoning() -> None:
    envelope = {"execution_status": "success", "result": {"overall": "VALID", "source_authenticity": "not_established"}}
    events = "\n".join(
        [
            json.dumps({"type": "item.completed", "item": {"type": "reasoning", "text": "private trace"}}),
            json.dumps(
                {
                    "type": "item.completed",
                    "item": {
                        "type": "mcp_tool_call",
                        "tool": "video_trust.verify_file",
                        "arguments": {"input_file": "clip", "codec": "h264"},
                        "status": "completed",
                        "error": None,
                        "result": {"content": [{"type": "text", "text": json.dumps(envelope)}]},
                    },
                }
            ),
            json.dumps({"type": "item.completed", "item": {"type": "agent_message", "text": '{"answer":"ok"}'}}),
        ]
    )
    calls, final = calls_from_codex_events(events)
    assert len(calls) == 1
    assert calls[0].tool == "video_trust.verify_file"
    assert calls[0].result == envelope
    assert calls[0].is_error is False
    assert "private trace" not in final
    assert "private trace" not in json.dumps([call.model_dump() for call in calls])


def test_ollama_loop_drops_thinking() -> None:
    visible = _visible_message(
        {"role": "assistant", "content": "done", "thinking": "hidden", "tool_calls": []}
    )
    assert "thinking" not in visible
    assert visible["content"] == "done"


def test_fixture_binding_and_sanitizer_replace_host_paths(tmp_path: Path) -> None:
    task = next(item for item in corpus() if item.task_id == "T01")
    media = tmp_path / "media"
    paths = {
        "media/signed.h264": media / "signed.h264",
        "media/ca.pem": media / "ca.pem",
    }
    bound = bind_task(task, paths)
    assert bound.user_task == task.user_task
    assert bound.input_file == str(paths["media/signed.h264"])
    sanitized = _sanitize({"path": str(tmp_path / "media" / "signed.h264"), "ok": True}, [str(tmp_path)])
    assert sanitized["path"] == "<fixture>/media/signed.h264"
    assert str(tmp_path) not in json.dumps(sanitized)
