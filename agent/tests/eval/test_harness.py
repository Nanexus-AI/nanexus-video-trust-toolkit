"""Corpus, scorer, and scripted-driver tests. No live model."""

from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path

import pytest
from mcp import StdioServerParameters
from mcp.client.session import ClientSession
from mcp.client.stdio import stdio_client
from pydantic import ValidationError

from nanexus_video_trust_agent.contracts import ASSESS_VIDEO_INTEGRITY_CAPABILITY, VERIFY_FILE_CAPABILITY
from tests.eval.corpus import corpus
from tests.eval.drivers.base import EvalDriver
from tests.eval.drivers.scripted import (
    ScriptedDriver,
    hallucinated,
    overclaim,
    perfect,
    unnecessary_retry,
    wrong_codec,
    wrong_tool,
)
from tests.eval.score import score, suite_result
from tests.eval.transcript import EvalTranscript

SCRIPT = Path(__file__).resolve().parents[1] / "support" / "fake_video_trust.py"


def _tasks() -> dict:
    return {task.task_id: task for task in corpus()}


def test_corpus_contains_t01_through_t09() -> None:
    tasks = corpus()
    assert [task.task_id for task in tasks] == [f"T0{index}" for index in range(1, 10)]
    assert all(task.acceptable_tools == [VERIFY_FILE_CAPABILITY, ASSESS_VIDEO_INTEGRITY_CAPABILITY] for task in tasks)
    joined = " ".join(task.user_task for task in tasks)
    assert "AV1" in joined
    assert "trust anchor" in joined.lower()
    assert "allowed roots" in joined


def test_transcript_rejects_hidden_reasoning() -> None:
    task = _tasks()["T04"]
    transcript = ScriptedDriver(perfect(task)).run(task)
    payload = transcript.model_dump()
    payload["reasoning"] = "hidden"
    with pytest.raises(ValidationError):
        EvalTranscript.model_validate(payload)
    assert "reasoning" not in transcript.model_dump()


def test_perfect_scripts_succeed_for_every_task_and_either_tool() -> None:
    scores = []
    for task in corpus():
        driver: EvalDriver = ScriptedDriver(perfect(task), tier="small")
        scores.append(score(driver.run(task), task))
        if task.require_tool_call:
            other = ASSESS_VIDEO_INTEGRITY_CAPABILITY
            assessed = ScriptedDriver(perfect(task, tool=other)).run(task)
            assessed_score = score(assessed, task)
            assert assessed_score.task_success is True
            assert assessed_score.capabilities_used == [other]
    assert all(item.task_success for item in scores)
    assert all(item.trust_overclaim is False for item in scores)
    result = suite_result("scripted:perfect", "small", scores)
    assert result.totals.tasks == 9
    assert result.totals.task_success == 9
    assert result.totals.trust_overclaim == 0
    dumped = result.model_dump(mode="json")
    assert dumped["suite_version"] == "0.1"
    assert dumped["model_tier"] == "small"
    assert dumped["tasks"][4]["task_id"] == "T05"


def test_known_bad_scripts_are_distinct() -> None:
    task = _tasks()["T01"]
    assert score(ScriptedDriver(wrong_tool(task)).run(task), task).tool_selection_correct is False
    assert score(ScriptedDriver(wrong_codec(task)).run(task), task).arguments_correct is False
    over = score(ScriptedDriver(overclaim(task)).run(task), task)
    assert over.trust_overclaim is True
    assert over.task_success is False
    invented = score(ScriptedDriver(hallucinated(task)).run(task), task)
    assert invented.result_interpretation_correct is False
    assert invented.trust_overclaim is True
    repeated = score(ScriptedDriver(unnecessary_retry(task)).run(task), task)
    assert repeated.tool_calls == 2
    assert repeated.unnecessary_tool_calls == 1
    assert repeated.task_success is True
    rejected = _tasks()["T07"]
    mislabeled = perfect(rejected)
    mislabeled.interpretation.reported_error_code = "MALFORMED_MEDIA"
    assert score(ScriptedDriver(mislabeled).run(rejected), rejected).result_interpretation_correct is False


def test_error_recovery_distinguishes_a_guess_from_a_stop() -> None:
    task = _tasks()["T06"]
    stopped = score(ScriptedDriver(perfect(task)).run(task), task)
    assert stopped.error_recovery is True
    assert stopped.task_success is True
    called = score(
        ScriptedDriver(perfect(task, tool=VERIFY_FILE_CAPABILITY)).run(task),
        task,
    )
    assert called.task_success is True
    assert called.capabilities_used == [VERIFY_FILE_CAPABILITY]
    guessed = wrong_codec(task)
    guessed.interpretation.guessed_codec = True
    guessed.interpretation.reported_overall = "VALID"
    guessed.interpretation.reported_error_code = None
    failed = score(ScriptedDriver(guessed).run(task), task)
    assert failed.arguments_correct is False
    assert failed.error_recovery is False
    assert failed.task_success is False


def test_path_workaround_fails_recovery() -> None:
    task = _tasks()["T08"]
    script = perfect(task)
    script.calls.append(
        script.calls[0].model_copy(update={"arguments": {"input_file": "root/copied.h264", "codec": "h264"}})
    )
    script.interpretation.attempted_path_workaround = True
    failed = score(ScriptedDriver(script).run(task), task)
    assert failed.error_recovery is False


def test_core_package_does_not_import_the_harness() -> None:
    root = Path(__file__).resolve().parents[2] / "src" / "nanexus_video_trust_agent"
    for path in root.glob("*.py"):
        text = path.read_text(encoding="utf-8")
        assert "tests.eval" not in text
        assert "langgraph" not in text.lower()
        assert "ollama" not in text.lower()
        assert "openai" not in text.lower()
        assert "anthropic" not in text.lower()


def test_scripted_call_can_use_mcp_stdio(tmp_path: Path) -> None:
    root = tmp_path / "root"
    clip = root / "media" / "signed.h264"
    clip.parent.mkdir(parents=True)
    clip.write_bytes(b"annex-b")
    anchor = root / "media" / "ca.pem"
    anchor.write_bytes(b"not-a-real-pem")
    link = tmp_path / "vt-valid"
    SCRIPT.chmod(0o755)
    link.symlink_to(SCRIPT)
    task = _tasks()["T01"].model_copy(update={"input_file": str(clip), "trust_anchor": str(anchor)})
    script = perfect(task, tool=VERIFY_FILE_CAPABILITY)

    def execute(tool: str, arguments: dict) -> tuple[dict, bool]:
        async def once() -> tuple[dict, bool]:
            env = os.environ.copy()
            env["NANEXUS_VIDEO_TRUST"] = str(link)
            env["NANEXUS_ALLOWED_ROOTS"] = str(root)
            params = StdioServerParameters(
                command=sys.executable,
                args=["-m", "nanexus_video_trust_agent.mcp_server"],
                env=env,
            )
            with (tmp_path / "stderr.txt").open("w", encoding="utf-8") as err:
                async with stdio_client(params, errlog=err) as (read, write):
                    async with ClientSession(read, write) as session:
                        await session.initialize()
                        result = await session.call_tool(tool, arguments)
            assert result.structured_content is not None
            return result.structured_content, result.is_error

        return asyncio.run(once())

    transcript = ScriptedDriver(script, executor=execute).run(task)
    assert transcript.calls[0].result is not None
    assert transcript.calls[0].result["result"]["overall"] == "VALID"
    scored = score(transcript, task)
    assert scored.task_success is True
    assert scored.trust_overclaim is False
    assert str(tmp_path) not in json.dumps(transcript.interpretation.model_dump())
