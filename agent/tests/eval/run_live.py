"""Run the live baseline against the unchanged T01–T09 corpus.

This module is test infrastructure. It writes evidence only to the directory
given on the command line. It does not choose a pass threshold.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from datetime import date
from pathlib import Path
from typing import Any

from nanexus_video_trust_agent.capabilities import CapabilityService
from nanexus_video_trust_agent.core_client import CoreClient
from nanexus_video_trust_agent.policy import AllowedRoots
from tests.eval.corpus import corpus
from tests.eval.drivers.codex import CodexExecDriver, CodexSettings, runtime_metadata as codex_metadata
from tests.eval.drivers.ollama import OllamaChatDriver, OllamaSettings, runtime_metadata as ollama_metadata
from tests.eval.score import SCORER_VERSION, TaskScore, score, suite_result
from tests.eval.transcript import SUITE_VERSION
from tests.eval.transcript import EvalTranscript
from tests.integration.test_real_capabilities import BINARY, _generate

_VERIFY = "video_trust.verify_file"
_ASSESS = "video_trust.assess_video_integrity"


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Live evaluation baseline")
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--driver", choices=("ollama", "codex", "both"), default="both")
    parser.add_argument("--task", action="append", default=[])
    parser.add_argument("--fixture-root", type=Path, required=True)
    parser.add_argument("--ollama-model", default="qwen3:14b")
    parser.add_argument("--codex-model", default="gpt-5.6-sol")
    args = parser.parse_args(argv)
    if not BINARY.is_file():
        raise SystemExit("video-trust binary is not built")
    layout = prepare_fixtures(args.fixture_root)
    selected = [task for task in corpus() if not args.task or task.task_id in args.task]
    bound = [bind_task(task, layout["paths"]) for task in selected]
    check_domain_fixtures(layout["allowed"], bound)
    args.evidence_dir.mkdir(parents=True, exist_ok=True)
    needles = [str(args.fixture_root), str(BINARY), str(Path(args.fixture_root).resolve())]
    drivers: list[str] = ["ollama", "codex"] if args.driver == "both" else [args.driver]
    for name in drivers:
        document = run_driver(name, bound, layout, args, needles)
        target = args.evidence_dir / f"{name}.json"
        target.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        totals = document["totals"]
        print(
            f"{name} tasks={totals['tasks']} success={totals['task_success']} "
            f"overclaim={totals['trust_overclaim']} calls={totals['tool_calls']}"
        )


def prepare_fixtures(root: Path) -> dict[str, Any]:
    root.mkdir(parents=True, exist_ok=True)
    lab = root / "lab"
    if not (lab / "h264" / "signed.h264").is_file():
        lab.mkdir(parents=True, exist_ok=True)
        _generate(lab)
    allowed = root / "allowed"
    media = allowed / "media"
    media.mkdir(parents=True, exist_ok=True)
    mapping = {
        "media/signed.h264": media / "signed.h264",
        "media/unsigned.h264": media / "unsigned.h264",
        "media/corrupt.h264": media / "corrupt.h264",
        "media/ca.pem": media / "ca.pem",
        "media/clip.av1": media / "clip.av1",
        "media/not-annexb.bin": media / "not-annexb.bin",
        "outside/secret.h264": root / "outside" / "secret.h264",
    }
    shutil.copyfile(lab / "h264" / "signed.h264", mapping["media/signed.h264"])
    shutil.copyfile(lab / "h264" / "unsigned.h264", mapping["media/unsigned.h264"])
    shutil.copyfile(lab / "h264" / "invalid.h264", mapping["media/corrupt.h264"])
    shutil.copyfile(lab / "pki" / "ca.pem", mapping["media/ca.pem"])
    mapping["media/clip.av1"].write_bytes(b"not-a-real-av1")
    mapping["media/not-annexb.bin"].write_bytes(b"this is not annex-b")
    mapping["outside/secret.h264"].parent.mkdir(parents=True, exist_ok=True)
    mapping["outside/secret.h264"].write_bytes(b"outside")
    return {"allowed": allowed, "paths": mapping, "binary": BINARY}


def bind_task(task: Any, paths: dict[str, Path]) -> Any:
    updates: dict[str, str] = {}
    if task.input_file is not None:
        updates["input_file"] = str(paths[task.input_file])
    if task.trust_anchor is not None:
        updates["trust_anchor"] = str(paths[task.trust_anchor])
    return task.model_copy(update=updates)


def check_domain_fixtures(allowed: Path, tasks: list[Any]) -> None:
    service = CapabilityService(CoreClient(BINARY), AllowedRoots([allowed]))
    by_id = {task.task_id: task for task in tasks}
    expectations = {
        "T01": ("success", "VALID"),
        "T02": ("success", "UNSIGNED"),
        "T03": ("success", "INVALID"),
        "T05": ("success", "VALID"),
        "T06": ("failed", "UNSUPPORTED_CODEC"),
        "T07": ("failed", "CORE_INPUT_REJECTED"),
        "T08": ("failed", "PATH_NOT_ALLOWED"),
    }
    for task_id, (status, marker) in expectations.items():
        task = by_id.get(task_id)
        if task is None:
            continue
        request: dict[str, Any] = {"input_file": task.input_file, "codec": task.codec}
        if task.trust_anchor_mode == "required":
            request["trust_anchor"] = task.trust_anchor
        envelope = service.verify_file(request)
        if envelope.execution_status.value != status:
            raise RuntimeError(f"{task_id} fixture status {envelope.execution_status.value}")
        if status == "success" and envelope.result is not None and envelope.result.overall.value != marker:
            raise RuntimeError(f"{task_id} fixture overall {envelope.result.overall.value}")
        if status == "failed" and (not envelope.errors or envelope.errors[0].code.value != marker):
            raise RuntimeError(f"{task_id} fixture error did not match the corpus")


def run_driver(name: str, tasks: list[Any], layout: dict[str, Any], args: argparse.Namespace, needles: list[str]) -> dict[str, Any]:
    scores: list[TaskScore] = []
    rows: list[dict[str, Any]] = []
    if name == "ollama":
        settings = OllamaSettings(model=args.ollama_model)
        metadata = ollama_metadata(settings)
    else:
        settings = CodexSettings(model=args.codex_model)
        metadata = codex_metadata(settings)
    metadata["evaluated_on"] = date.today().isoformat()
    metadata["suite_version"] = SUITE_VERSION
    metadata["scorer_version"] = SCORER_VERSION
    metadata["baseline"] = "v1"
    metadata["tier"] = "small" if name == "ollama" else "frontier"
    for task in tasks:
        driver = _make_driver(name, settings, layout, task.task_id)
        print(f"{name} {task.task_id}", flush=True)
        try:
            transcript = driver.run(task)
        except Exception as exc:
            rows.append({"task_id": task.task_id, "error": type(exc).__name__})
            continue
        scored = score(transcript, task)
        scores.append(scored)
        rows.append(_row(task.task_id, scored, transcript, driver, needles))
    suite = suite_result(f"{name}:{metadata['model']}", metadata["tier"], scores)
    return {
        "metadata": metadata,
        "totals": suite.totals.model_dump(mode="json"),
        "rates": _rates(scores),
        "tasks": rows,
    }


def _make_driver(name: str, settings: Any, layout: dict[str, Any], task_id: str) -> Any:
    allowed = str(layout["allowed"])
    if name == "ollama":
        stderr = layout["allowed"].parent / f"{task_id}-mcp-stderr.txt"
        return OllamaChatDriver(
            settings,
            python=_python(),
            video_trust=str(layout["binary"]),
            allowed_root=allowed,
            stderr_path=str(stderr),
        )
    return CodexExecDriver(
        settings,
        python=_python(),
        video_trust=str(layout["binary"]),
        allowed_root=allowed,
        work_dir=allowed,
    )


def _python() -> str:
    return sys.executable


def _row(
    task_id: str,
    scored: TaskScore,
    transcript: EvalTranscript,
    driver: Any,
    needles: list[str],
) -> dict[str, Any]:
    pattern = _capability_pattern(scored.capabilities_used)
    payload = {
        "task_id": task_id,
        "score": scored.model_dump(mode="json"),
        "capability_pattern": pattern,
        "parse_mode": driver.last_parse_mode,
        "additional_action_required": driver.last_additional_action,
        "answer": driver.last_answer,
        "transcript": transcript.model_dump(mode="json"),
    }
    return _sanitize(payload, needles)


def _capability_pattern(tools: list[str]) -> str:
    used = {tool for tool in tools if tool in {_VERIFY, _ASSESS}}
    if used == {_VERIFY, _ASSESS}:
        return "both"
    if used == {_VERIFY}:
        return "verify_file"
    if used == {_ASSESS}:
        return "assess_video_integrity"
    if not tools:
        return "neither"
    return "other"


def _rates(scores: list[TaskScore]) -> dict[str, float | None]:
    count = len(scores)
    if count == 0:
        return {"task_success": None}
    fields = (
        "task_success",
        "tool_selection_correct",
        "arguments_correct",
        "result_interpretation_correct",
        "required_claims_covered",
        "trust_overclaim",
        "error_recovery",
    )
    rates: dict[str, float | None] = {
        field: sum(getattr(item, field) for item in scores) / count for field in fields
    }
    rates["tool_calls_mean"] = sum(item.tool_calls for item in scores) / count
    rates["unnecessary_tool_calls_mean"] = sum(item.unnecessary_tool_calls for item in scores) / count
    return rates


def _sanitize(value: Any, needles: list[str]) -> Any:
    if isinstance(value, str):
        for needle in needles:
            if needle:
                value = value.replace(needle, "<fixture>")
        return value
    if isinstance(value, list):
        return [_sanitize(item, needles) for item in value]
    if isinstance(value, dict):
        return {key: _sanitize(item, needles) for key, item in value.items()}
    return value


if __name__ == "__main__":
    main()
