#!/usr/bin/env python3
"""Validate representative preservation documents against schema 0.1."""

import copy
import json
import pathlib
import subprocess
import sys

import jsonschema


def rejected(validator, document, message):
    try:
        validator.validate(document)
    except jsonschema.ValidationError:
        return
    raise AssertionError(message)


def main() -> int:
    schema = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    validator_class = jsonschema.validators.validator_for(schema)
    validator_class.check_schema(schema)
    validator = validator_class(schema)

    documents = {}
    for name in (
        "exact",
        "subset",
        "partial",
        "not_preserved",
        "ambiguous",
        "bounded",
        "unsigned",
    ):
        rendered = subprocess.run(
            [sys.argv[2], "--json", name],
            check=True,
            capture_output=True,
            text=True,
        )
        assert rendered.stderr == ""
        assert "/home/" not in rendered.stdout
        document = json.loads(rendered.stdout)
        validator.validate(document)
        documents[name] = document

    exact = documents["exact"]
    assert exact["document_type"] == "media_signing_preservation_assessment"
    assert exact["schema_version"] == "0.1"
    assert exact["coverage"]["state"] == "full"
    assert exact["preservation"]["media_signing_evidence"] == "preserved"
    assert exact["artifact_identity"]["byte_relation"] == "identical"
    assert exact["correlation"]["stream_relation"] == "equivalent"
    assert exact["before"]["inspection"]["pending_nalus"] == 0
    assert exact["before"]["inspection"]["pending_hashable_nalus"] is None
    assert exact["findings"] and exact["limitations"]

    subset = documents["subset"]
    assert subset["coverage"]["state"] == "subset"
    assert subset["preservation"]["media_signing_evidence"] == "preserved"
    assert subset["artifact_identity"]["byte_relation"] == "different"
    assert subset["correlation"]["stream_relation"] == "ordered_subset"
    assert subset["correlation"]["signing_metadata"]["relation"] == "retained_for_subset"

    assert documents["partial"]["preservation"]["media_signing_evidence"] == "partially_preserved"
    assert documents["not_preserved"]["preservation"]["media_signing_evidence"] == "not_preserved"
    assert documents["ambiguous"]["coverage"]["state"] == "unknown"
    assert documents["ambiguous"]["correlation"]["quality"] == "ambiguous"
    assert documents["ambiguous"]["preservation"]["media_signing_evidence"] == "indeterminate"
    assert documents["bounded"]["correlation"]["quality"] == "resource_bounded"
    assert documents["bounded"]["correlation"]["diagnostic_detail_bounded"] is True
    assert documents["unsigned"]["applicability"]["media_signing_preservation"] == "not_applicable"
    assert documents["unsigned"]["preservation"]["media_signing_evidence"] == "indeterminate"

    malformed = copy.deepcopy(exact)
    malformed["unexpected"] = True
    rejected(validator, malformed, "top-level extra field unexpectedly validated")

    malformed = copy.deepcopy(exact)
    malformed["correlation"]["unexpected"] = True
    rejected(validator, malformed, "nested extra field unexpectedly validated")

    malformed = copy.deepcopy(exact)
    malformed["coverage"]["state"] = "complete"
    rejected(validator, malformed, "invalid coverage enum unexpectedly validated")

    malformed = copy.deepcopy(exact)
    malformed["preservation"]["media_signing_evidence"] = "not_applicable"
    rejected(validator, malformed, "fifth aggregate state unexpectedly validated")

    malformed = copy.deepcopy(exact)
    malformed["before"]["inspection"]["pending_nalus"] = None
    rejected(validator, malformed, "numeric zero replaced by null unexpectedly validated")

    print("PASS: preservation assessment JSON Schema")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
