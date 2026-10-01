#!/usr/bin/env python3
"""Run the bounded, manifest-driven synthetic RTSP/TCP live matrix.

MediaMTX is deliberately external to this script so callers can pin and
isolate it with Docker or another controlled process supervisor. The harness
owns every publisher/client child and always applies subprocess timeouts.
"""

import argparse
import hashlib
import json
from pathlib import Path
import signal
import socket
import subprocess
import sys
import time
from urllib.parse import urlsplit

import jsonschema


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def normalize(documents):
    normalized = []
    for document in documents:
        copy = dict(document)
        copy.pop("runtime", None)
        normalized.append(copy)
    return normalized


def validate_stream(documents, validator):
    if not documents:
        raise AssertionError("empty JSONL stream")
    for document in documents:
        validator.validate(document)
    sequences = [document["sequence"] for document in documents]
    if sequences != list(range(len(documents))):
        raise AssertionError(f"non-contiguous sequences: {sequences}")
    summaries = [index for index, document in enumerate(documents)
                 if document["event_type"] == "session_summary"]
    if summaries != [len(documents) - 1]:
        raise AssertionError(f"terminal summary positions: {summaries}")


def wait_for_publisher(process, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"publisher exited early: {process.returncode}")
        time.sleep(0.05)


def wait_for_rtsp(endpoint, process, timeout_seconds=10):
    parsed = urlsplit(endpoint)
    port = parsed.port or 554
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"publisher exited before RTSP readiness: {process.returncode}")
        try:
            with socket.create_connection((parsed.hostname, port), timeout=1) as stream:
                request = (f"DESCRIBE {endpoint} RTSP/1.0\r\nCSeq: 1\r\n"
                           "Accept: application/sdp\r\n\r\n")
                stream.sendall(request.encode("ascii"))
                response = stream.recv(256)
            if response.startswith(b"RTSP/1.0 200"):
                return
        except (OSError, TimeoutError):
            continue
        time.sleep(0.05)
    raise AssertionError("RTSP path did not become ready")


def assert_result(case, process, documents):
    expected = case["expected"]
    summary = documents[-1]
    if process.returncode != expected["exit"]:
        raise AssertionError(f"exit {process.returncode}, expected {expected['exit']}")
    for field in ("stop_reason", "worst_closed_outcome", "final_tail_state"):
        if field in expected and summary.get(field) != expected[field]:
            raise AssertionError(
                f"{field}={summary.get(field)!r}, expected {expected[field]!r}")
    if summary["closed_observation_count"] < expected.get("min_closed", 0):
        raise AssertionError("too few closed observations")
    if summary["epoch_count"] > expected.get("max_epochs", summary["epoch_count"]):
        raise AssertionError("unexpected epoch reset")
    if expected.get("require_pending_then_closed"):
        observations = [item for item in documents
                        if item["event_type"] == "verification_observation"]
        pending = [index for index, item in enumerate(observations)
                   if item["tail_state"] == "open_pending"]
        closed = [index for index, item in enumerate(observations)
                  if item.get("closed_evidence") is not None]
        if not pending or not closed or not any(a < b for a in pending for b in closed):
            raise AssertionError("pending-to-closed transition not observed")


def terminate(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def run_case(case, repetition, args, validator, output_dir):
    raw_rtp = case["mode"] == "raw_rtp"
    endpoint = (f"{args.endpoint_base.rstrip('/')}/oversized" if raw_rtp else
                f"{args.endpoint_base.rstrip('/')}/{case['id']}-{repetition}")
    publisher = None
    client = None
    action = None
    run_dir = output_dir / case["id"] / str(repetition)
    run_dir.mkdir(parents=True, exist_ok=True)
    try:
        if case["mode"] != "connection_failure":
            source = args.fixtures / case["fixture"]
            if not source.is_file():
                raise AssertionError(f"missing fixture: {case['fixture']}")
            if raw_rtp:
                publisher_command = [
                    args.gst_launch, "-q", "multifilesrc", "loop=true",
                    f"location={source}", "!",
                    "video/x-h264,stream-format=byte-stream,alignment=au,framerate=1/1",
                    "!", "rtph264pay", "pt=96", "mtu=1400", "!", "identity",
                    "sleep-time=500", "!", "udpsink", "host=127.0.0.1",
                    "port=19000", "sync=false", "async=false",
                ]
            else:
                publisher_command = [
                    args.ffmpeg, "-hide_banner", "-loglevel", "error", "-re",
                    "-stream_loop", "-1", "-fflags", "+genpts", "-i", str(source),
                    "-c", "copy", "-f", "rtsp", "-rtsp_transport", "tcp", endpoint,
                ]
            publisher_stderr = (run_dir / "publisher.stderr").open(
                "w", encoding="utf-8")
            try:
                publisher = subprocess.Popen(
                    publisher_command, stdout=subprocess.DEVNULL,
                    stderr=publisher_stderr, text=True,
                )
            finally:
                publisher_stderr.close()
            if raw_rtp:
                wait_for_rtsp(endpoint, publisher)
            else:
                wait_for_publisher(publisher, case.get("join_delay", 0.20))

        command = [
            str(args.video_trust), "verify-live", "--codec", case["codec"],
            "--duration", str(case["duration"]), "--ca", str(args.ca),
            "--jsonl", endpoint,
        ]
        client = subprocess.Popen(command, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        action_after = case.get("publisher_action_after")
        if action_after is not None:
            time.sleep(action_after)
            if case["mode"] == "stall":
                publisher.send_signal(signal.SIGSTOP)
                action = "sigstop"
            else:
                terminate(publisher)
                action = "terminate"
        stdout, stderr = client.communicate(timeout=case["duration"] + 10)
        documents = [json.loads(line) for line in stdout.splitlines()]
        validate_stream(documents, validator)
        (run_dir / "events.jsonl").write_text(stdout, encoding="utf-8")
        (run_dir / "stderr.txt").write_text(stderr, encoding="utf-8")
        normalized = normalize(documents)
        (run_dir / "normalized.json").write_text(
            json.dumps(normalized, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        assert_result(case, client, documents)
        return {
            "case": case["id"], "repetition": repetition, "result": "PASS",
            "exit": client.returncode, "events": len(documents),
            "summary": documents[-1], "normalized": normalized,
            "fixture_sha256": None if case["fixture"] is None else
                sha256(args.fixtures / case["fixture"]),
        }
    finally:
        if action == "sigstop" and publisher is not None and publisher.poll() is None:
            publisher.send_signal(signal.SIGCONT)
        terminate(client)
        terminate(publisher)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--video-trust", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, required=True)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--endpoint-base", default="rtsp://127.0.0.1:18554")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--gst-launch", default="gst-launch-1.0")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    schema = json.loads(args.schema.read_text(encoding="utf-8"))
    validator_class = jsonschema.validators.validator_for(schema)
    validator_class.check_schema(schema)
    validator = validator_class(schema)
    selected = [case for case in manifest["cases"]
                if not args.case or case["id"] in args.case]
    if not selected:
        raise SystemExit("no matrix cases selected")
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    failed = False
    for case in selected:
        traces = []
        for repetition in range(1, case["repetitions"] + 1):
            try:
                result = run_case(case, repetition, args, validator, args.output)
                traces.append(result.pop("normalized"))
                results.append(result)
                print(f"PASS {case['id']} repetition={repetition}")
            except Exception as error:  # matrix runner must record and continue
                failed = True
                results.append({"case": case["id"], "repetition": repetition,
                                "result": "FAIL", "error": str(error)})
                print(f"FAIL {case['id']} repetition={repetition}: {error}",
                      file=sys.stderr)
        if traces and any(trace != traces[0] for trace in traces[1:]):
            failed = True
            results.append({"case": case["id"], "result": "FAIL",
                            "error": "normalized semantic traces differ"})
    (args.output / "results.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    passed = sum(item["result"] == "PASS" for item in results)
    print(f"matrix repetitions: {passed}/{len(results)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
