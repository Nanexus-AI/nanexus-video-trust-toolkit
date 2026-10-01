#!/usr/bin/env python3
"""Validate the live 0.1 event schema and stream-level contract rules."""

import copy
import json
import pathlib
import subprocess
import sys

import jsonschema


SCENARIOS = (
    "healthy",
    "valid_unresolved",
    "invalid",
    "unsigned",
    "unsigned_unresolved",
    "multi_epoch",
)


def rejected(validator, document, message):
    try:
        validator.validate(document)
    except jsonschema.ValidationError:
        return
    raise AssertionError(message)


def parse_stream(text):
    return [json.loads(line) for line in text.splitlines()]


def validate_stream(validator, documents):
    assert documents
    for document in documents:
        validator.validate(document)
    assert [d["sequence"] for d in documents] == list(range(len(documents)))
    summaries = [i for i, d in enumerate(documents) if d["event_type"] == "session_summary"]
    assert summaries == [len(documents) - 1]
    epochs = [d["epoch"] for d in documents if "epoch" in d]
    assert epochs == sorted(epochs)


def must_reject_stream(validator, documents, message):
    try:
        validate_stream(validator, documents)
    except (AssertionError, jsonschema.ValidationError):
        return
    raise AssertionError(message)


def main():
    schema = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    validator_class = jsonschema.validators.validator_for(schema)
    validator_class.check_schema(schema)
    validator = validator_class(schema)
    executable = sys.argv[2]

    streams = {}
    for scenario in SCENARIOS:
        rendered = subprocess.run(
            [executable, "--scenario", scenario], check=True,
            capture_output=True, text=True,
        )
        assert rendered.stderr == ""
        documents = parse_stream(rendered.stdout)
        validate_stream(validator, documents)
        streams[scenario] = documents

    healthy = streams["healthy"]
    assert healthy[2]["closed_evidence"]["outcome"] == "valid"
    assert healthy[2]["tail_state"] == "open_pending"
    assert healthy[-1]["final_tail_state"] == "no_pending_tail"

    unresolved = streams["valid_unresolved"][-1]
    assert unresolved["worst_closed_outcome"] == "valid"
    assert unresolved["final_tail_state"] == "ended_with_unresolved_tail"
    assert unresolved["unresolved_tail"] is True
    assert streams["invalid"][-1]["worst_closed_outcome"] == "invalid"
    assert streams["unsigned"][-1]["worst_closed_outcome"] == "unsigned"
    assert streams["unsigned_unresolved"][-1]["closed_summary"]["unsigned"] == 1
    assert streams["unsigned_unresolved"][-1]["unresolved_tail"] is True
    assert streams["multi_epoch"][-1]["epoch_count"] == 2
    assert streams["multi_epoch"][-1]["cross_epoch_continuity"] == "not_established"
    assert "cross_epoch_continuity_not_established" in streams["multi_epoch"][-1]["limitations"]

    runtime = subprocess.run(
        [executable, "--scenario-runtime", "healthy"], check=True,
        capture_output=True, text=True,
    )
    normalized = parse_stream(runtime.stdout)
    for document in normalized:
        document.pop("runtime", None)
    assert normalized == healthy

    malformed = copy.deepcopy(healthy[0])
    malformed["event_type"] = "nal_received"
    rejected(validator, malformed, "unknown event type validated")

    malformed = copy.deepcopy(healthy[2])
    malformed["tail_state"] = "partial"
    rejected(validator, malformed, "unknown tail state validated")

    malformed = copy.deepcopy(healthy[-1])
    del malformed["closed_summary"]
    rejected(validator, malformed, "incomplete summary validated")

    malformed = copy.deepcopy(healthy[1])
    malformed["sequence"] = "1"
    rejected(validator, malformed, "string sequence validated")

    malformed = copy.deepcopy(healthy[2])
    malformed["official_counts"]["received_nalus"] = -1
    rejected(validator, malformed, "negative counter validated")

    malformed = copy.deepcopy(healthy[1])
    malformed["closed_evidence"] = None
    rejected(validator, malformed, "illegal event field combination validated")

    malformed = copy.deepcopy(healthy[2])
    malformed["AUTHENTIC"] = True
    rejected(validator, malformed, "AUTHENTIC boolean validated")

    malformed = copy.deepcopy(streams["multi_epoch"][-1])
    malformed["cross_epoch_continuity"] = "intact"
    rejected(validator, malformed, "cross-epoch continuity claim validated")

    malformed = copy.deepcopy(healthy[-1])
    malformed["findings"] = [
        {"code": "AUTH_NOT_OK", "severity": "info"} for _ in range(17)
    ]
    rejected(validator, malformed, "oversized findings validated")

    malformed = copy.deepcopy(healthy[2])
    malformed["raw_diagnostic"] = "rtsp://user:password@private-host/path"
    rejected(validator, malformed, "raw diagnostic field validated")

    duplicate = copy.deepcopy(healthy) + [copy.deepcopy(healthy[-1])]
    duplicate[-1]["sequence"] += 1
    must_reject_stream(validator, duplicate, "duplicate summary stream validated")
    must_reject_stream(validator, healthy[:-1], "missing summary stream validated")
    post_summary = copy.deepcopy(healthy) + [copy.deepcopy(healthy[1])]
    post_summary[-1]["sequence"] = len(healthy)
    must_reject_stream(validator, post_summary, "post-summary event validated")
    unordered = copy.deepcopy(healthy)
    unordered[2]["sequence"] = 1
    must_reject_stream(validator, unordered, "duplicate sequence validated")

    print("PASS: live contract JSON Schema (6 positive streams, 14 negative cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
