#!/usr/bin/env python3
"""Validate representative inspection documents against the published schema."""

import copy
import json
import pathlib
import subprocess
import sys

import jsonschema


def main() -> int:
    schema_path = pathlib.Path(sys.argv[1])
    schema = json.loads(schema_path.read_text(encoding="utf-8"))
    validator_class = jsonschema.validators.validator_for(schema)
    validator_class.check_schema(schema)
    validator = validator_class(schema)

    rendered = subprocess.run(
        [sys.argv[2], "--inspection-json"],
        check=True,
        capture_output=True,
        text=True,
    )
    validator.validate(json.loads(rendered.stdout))

    document = {
        "document_type": "media_signing_inspection",
        "schema_version": "0.1",
        "verification": {
            "schema_version": "0.1",
            "codec": "h264",
            "media_signing": {"present": True},
            "signature_integrity": "ok",
            "continuity": "intact",
            "verification_completeness": "complete",
            "certificate_status": "not_provided",
            "source_authenticity": "not_established",
            "public_key_has_changed": False,
            "overall": "VALID",
            "vendor": {"manufacturer": "Example vendor"},
            "versions": {"signing": "1.2.3", "validation": None},
            "findings": [{"code": "EXAMPLE", "message": "Synthetic finding"}],
        },
        "accumulated_validation": {
            "number_of_received_nalus": 7,
            "number_of_validated_nalus": 5,
            "number_of_pending_nalus": 2,
            "number_of_received_frames": 3,
            "number_of_validated_frames": 2,
            "number_of_pending_frames": 1,
            "first_timestamp": 0,
            "last_timestamp": 133485408001234567,
        },
        "latest_validation": {
            "number_of_expected_hashable_nalus": 4,
            "number_of_received_hashable_nalus": 0,
            "number_of_pending_hashable_nalus": None,
            "start_timestamp": 133485408001234500,
            "end_timestamp": 133485408001234567,
        },
        "vendor": {
            "manufacturer": "Example vendor",
            "firmware_version": None,
            "serial_number": "SN-001",
        },
    }
    validator.validate(document)

    malformed = copy.deepcopy(document)
    malformed["document_type"] = "verification"
    try:
        validator.validate(malformed)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("wrong document_type unexpectedly validated")

    malformed = copy.deepcopy(document)
    malformed["latest_validation"]["number_of_received_hashable_nalus"] = -1
    try:
        validator.validate(malformed)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("negative public count unexpectedly validated")

    malformed = copy.deepcopy(document)
    malformed["latest_validation"]["validation_str"] = "..."
    try:
        validator.validate(malformed)
    except jsonschema.ValidationError:
        pass
    else:
        raise AssertionError("unknown diagnostic field unexpectedly validated")

    print("PASS: inspection JSON Schema")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
