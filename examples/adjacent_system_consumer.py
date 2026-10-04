#!/usr/bin/env python3
"""Minimal adjacent-system consumer for Nanexus integration contract 0.1."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

REQUEST_TYPE = "nanexus_video_trust_integration_request"
RESPONSE_TYPE = "nanexus_video_trust_integration_response"
CONTRACT_VERSION = "0.1"
MAX_RESPONSE_BYTES = 2 * 1024 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Invoke one finite Nanexus integration operation and print a bounded summary."
    )
    parser.add_argument("--adapter", required=True, type=Path)
    parser.add_argument("--allowed-root", required=True, action="append", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--request-id", default="reference-1")
    parser.add_argument(
        "--operation",
        required=True,
        choices=("verify_file", "inspect_file", "compare_preservation"),
    )
    parser.add_argument("--codec", required=True, choices=("h264", "h265"))
    parser.add_argument("--input-file", type=Path)
    parser.add_argument("--trust-anchor", type=Path)
    parser.add_argument("--before-file", type=Path)
    parser.add_argument("--after-file", type=Path)
    parser.add_argument("--before-trust-anchor", type=Path)
    parser.add_argument("--after-trust-anchor", type=Path)
    args = parser.parse_args()
    if not 0 < args.timeout <= 300:
        parser.error("--timeout must be greater than 0 and at most 300 seconds")
    if args.operation in ("verify_file", "inspect_file"):
        if args.input_file is None or args.before_file is not None or args.after_file is not None:
            parser.error("single-file operations require --input-file only")
    elif args.before_file is None or args.after_file is None or args.input_file is not None:
        parser.error("compare_preservation requires --before-file and --after-file")
    return args


def make_request(args: argparse.Namespace) -> dict[str, Any]:
    body: dict[str, Any] = {"codec": args.codec}
    if args.operation in ("verify_file", "inspect_file"):
        body["input_file"] = str(args.input_file)
        if args.trust_anchor is not None:
            body["trust_anchor"] = str(args.trust_anchor)
    else:
        body["before_file"] = str(args.before_file)
        body["after_file"] = str(args.after_file)
        if args.before_trust_anchor is not None:
            body["before_trust_anchor"] = str(args.before_trust_anchor)
        if args.after_trust_anchor is not None:
            body["after_trust_anchor"] = str(args.after_trust_anchor)
    return {
        "document_type": REQUEST_TYPE,
        "schema_version": CONTRACT_VERSION,
        "request_id": args.request_id,
        "operation": args.operation,
        "input": body,
    }


def invoke(args: argparse.Namespace, request: dict[str, Any]) -> tuple[int, dict[str, Any]]:
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "LANG": "C.UTF-8",
        "LC_ALL": "C.UTF-8",
        "NANEXUS_INTEGRATION_ALLOWED_ROOTS": os.pathsep.join(
            str(root.resolve()) for root in args.allowed_root
        ),
    }
    try:
        process = subprocess.run(
            [args.adapter],
            input=json.dumps(request, separators=(",", ":")),
            text=True,
            capture_output=True,
            timeout=args.timeout,
            env=environment,
            check=False,
        )
    except FileNotFoundError:
        return 2, consumer_error("adapter_unavailable", "The integration adapter is unavailable.")
    except subprocess.TimeoutExpired:
        return 2, consumer_error("adapter_timeout", "The integration adapter exceeded the deadline.")
    if process.stderr:
        return 2, consumer_error("adapter_protocol_error", "The integration adapter wrote to stderr.")
    if len(process.stdout.encode("utf-8")) > MAX_RESPONSE_BYTES:
        return 2, consumer_error("adapter_protocol_error", "The integration response is too large.")
    try:
        response = json.loads(process.stdout)
    except (UnicodeError, json.JSONDecodeError):
        return 2, consumer_error("adapter_protocol_error", "The integration response is not valid JSON.")
    error = validate_response(response, request)
    if error is not None:
        return 2, consumer_error("adapter_protocol_error", error)
    expected_exit = 0 if response["execution"]["status"] == "completed" else 1
    if process.returncode != expected_exit:
        return 2, consumer_error(
            "adapter_protocol_error", "The integration adapter exit does not match its response."
        )
    return (0 if response["execution"]["status"] == "completed" else 1), summarize(response)


def validate_response(response: Any, request: dict[str, Any]) -> str | None:
    if not isinstance(response, dict):
        return "The integration response is not an object."
    if response.get("document_type") != RESPONSE_TYPE or response.get("schema_version") != CONTRACT_VERSION:
        return "The integration response identity is unsupported."
    if response.get("request_id") != request["request_id"] or response.get("operation") != request["operation"]:
        return "The integration response does not match the request."
    execution = response.get("execution")
    if not isinstance(execution, dict) or execution.get("status") not in ("completed", "failed"):
        return "The integration execution status is invalid."
    if not isinstance(response.get("limitations"), list):
        return "The integration limitations are invalid."
    if execution["status"] == "failed":
        if not isinstance(execution.get("error"), dict):
            return "A failed execution has no typed error."
        if response.get("result") is not None or response.get("evidence") is not None:
            return "A failed execution contains a domain result or evidence."
        return None
    result = response.get("result")
    evidence = response.get("evidence")
    if execution.get("error") is not None or not isinstance(result, dict) or not isinstance(evidence, dict):
        return "A completed execution is missing its result or evidence."
    expected_type = {
        "verify_file": "media_signing_verification",
        "inspect_file": "media_signing_inspection",
        "compare_preservation": "media_signing_preservation_assessment",
    }[request["operation"]]
    if result.get("document_type") != expected_type or result.get("schema_version") != "0.1":
        return "The embedded domain document identity is unsupported."
    if not isinstance(result.get("document"), dict) or not isinstance(evidence.get("inputs"), list):
        return "The embedded domain document or evidence is invalid."
    for item in evidence["inputs"]:
        if not isinstance(item, dict) or not isinstance(item.get("reference"), str):
            return "An evidence reference is invalid."
        reference = item["reference"]
        if reference.startswith("/") or ".." in reference.split("/"):
            return "An evidence reference exposes a disallowed path."
    return None


def summarize(response: dict[str, Any]) -> dict[str, Any]:
    execution = response["execution"]
    summary: dict[str, Any] = {
        "document_type": "nanexus_video_trust_reference_summary",
        "schema_version": "0.1",
        "operation": response["operation"],
        "execution_status": execution["status"],
        "error": execution["error"],
        "domain": None,
        "evidence_inputs": [],
        "limitations": response["limitations"],
    }
    if execution["status"] == "failed":
        return summary
    result = response["result"]
    document = result["document"]
    domain: dict[str, Any] = {
        "document_type": result["document_type"],
        "schema_version": result["schema_version"],
    }
    if response["operation"] == "verify_file":
        domain["overall"] = document.get("overall")
    elif response["operation"] == "inspect_file":
        verification = document.get("verification", {})
        domain["overall"] = verification.get("overall")
    else:
        domain["preservation"] = document.get("preservation", {}).get("media_signing_evidence")
        domain["applicability"] = document.get("applicability", {}).get(
            "media_signing_preservation"
        )
        domain["source_coverage"] = document.get("coverage", {}).get("state")
    summary["domain"] = domain
    summary["evidence_inputs"] = response["evidence"]["inputs"]
    return summary


def consumer_error(code: str, message: str) -> dict[str, Any]:
    return {
        "document_type": "nanexus_video_trust_reference_summary",
        "schema_version": "0.1",
        "operation": None,
        "execution_status": "consumer_failed",
        "error": {"code": code, "message": message},
        "domain": None,
        "evidence_inputs": [],
        "limitations": [],
    }


def main() -> int:
    args = parse_args()
    code, summary = invoke(args, make_request(args))
    print(json.dumps(summary, sort_keys=True, separators=(",", ":")))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
