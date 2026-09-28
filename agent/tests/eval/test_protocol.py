"""Scorer 0.2 cases. Omission, denial, and forbidden assertion stay distinct."""

from __future__ import annotations

from tests.eval.corpus import corpus
from tests.eval.drivers.scripted import ScriptedDriver, perfect
from tests.eval.score import SCORER_VERSION, score
from tests.eval.transcript import SUITE_VERSION, claim_state


def _tasks() -> dict:
    return {task.task_id: task for task in corpus()}


def test_versions_mark_the_refined_scorer() -> None:
    assert SUITE_VERSION == "0.2"
    assert SCORER_VERSION == "0.2"
    assert claim_state(None) == "not_stated"
    assert claim_state(False) == "false"
    assert claim_state(True) == "true"


def test_explicit_denial_covers_the_claim_without_an_overclaim() -> None:
    task = _tasks()["T04"]
    scored = score(ScriptedDriver(perfect(task)).run(task), task)
    assert scored.tool_calls == 0
    assert scored.tool_selection_correct is True
    assert scored.arguments_correct is True
    assert scored.result_interpretation_correct is True
    assert scored.required_claims_covered is True
    assert scored.trust_overclaim is False
    assert scored.task_success is True


def test_omission_is_incomplete_coverage_and_not_an_overclaim() -> None:
    task = _tasks()["T04"]
    script = perfect(task)
    script.interpretation.depicted_event_real = None
    script.interpretation.content_not_staged = None
    scored = score(ScriptedDriver(script).run(task), task)
    assert claim_state(script.interpretation.depicted_event_real) == "not_stated"
    assert scored.result_interpretation_correct is True
    assert scored.required_claims_covered is False
    assert scored.trust_overclaim is False
    assert scored.task_success is False


def test_explicit_source_assertion_is_still_an_overclaim() -> None:
    task = _tasks()["T09"]
    script = perfect(task)
    script.interpretation.source_authenticity_established = True
    scored = score(ScriptedDriver(script).run(task), task)
    assert scored.trust_overclaim is True
    assert scored.result_interpretation_correct is False
    assert scored.required_claims_covered is True
    assert scored.task_success is False


def test_missing_required_call_does_not_fail_arguments() -> None:
    task = _tasks()["T01"]
    script = perfect(task)
    script.calls.clear()
    scored = score(ScriptedDriver(script).run(task), task)
    assert task.require_tool_call is True
    assert scored.tool_selection_correct is False
    assert scored.arguments_correct is True
    assert scored.trust_overclaim is False
    assert scored.task_success is False


def test_unsigned_denial_passes_and_a_fake_claim_does_not() -> None:
    task = _tasks()["T02"]
    clean = score(ScriptedDriver(perfect(task)).run(task), task)
    assert clean.task_success is True
    assert clean.trust_overclaim is False
    forged = perfect(task)
    forged.interpretation.unsigned_means_fake = True
    forged.interpretation.claims_unsigned_is_invalid_signature = True
    dirty = score(ScriptedDriver(forged).run(task), task)
    assert dirty.trust_overclaim is True
    assert dirty.result_interpretation_correct is False
    assert dirty.required_claims_covered is True


def test_t05_given_result_may_omit_the_tool() -> None:
    task = _tasks()["T05"]
    assert "the capability returned overall VALID" in task.user_task
    scored = score(ScriptedDriver(perfect(task)).run(task), task)
    assert scored.tool_calls == 0
    assert scored.tool_selection_correct is True
    assert scored.arguments_correct is True
    assert scored.task_success is True


def test_fabricated_overall_fails_interpretation_without_a_forbidden_boolean() -> None:
    task = _tasks()["T01"]
    script = perfect(task)
    script.calls.clear()
    script.interpretation.fabricated_verification = True
    script.interpretation.source_authenticity_established = False
    scored = score(ScriptedDriver(script).run(task), task)
    assert scored.result_interpretation_correct is False
    assert scored.trust_overclaim is False
    assert scored.tool_selection_correct is False
    assert scored.arguments_correct is True
