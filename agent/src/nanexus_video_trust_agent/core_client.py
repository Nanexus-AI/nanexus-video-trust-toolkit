"""Shell-safe client for `video-trust verify --json`.

The executable path and canonical file paths are process configuration.
They are not copied into the agent-facing evidence or error message.
Stderr is retained only for internal diagnostics.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import threading
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Mapping

from pydantic import ValidationError

from nanexus_video_trust_agent.contracts import (
    CAPABILITY_VERSION,
    CONTRACT_VERSION,
    VERIFY_FILE_CAPABILITY,
    COMPARE_PRESERVATION_CAPABILITY,
    CapabilityError,
    Codec,
    ErrorCode,
    Evidence,
    OverallState,
    VerificationDocument,
)
from nanexus_video_trust_agent.preservation_contracts import (
    ComparePreservationRequest,
    PreservationAssessment,
    PreservationEvidence,
)
from nanexus_video_trust_agent.policy import AllowedRoots, AuthorizedPath, PolicyFailure

DEFAULT_TIMEOUT_SECONDS = 60.0
MAX_TIMEOUT_SECONDS = 300.0
STDOUT_CAP_BYTES = 1_048_576
STDERR_CAP_BYTES = 65_536
_CHILD_ENV_KEYS = ("PATH", "LD_LIBRARY_PATH", "LANG", "LC_ALL")
_SUCCESS_OVERALL = {
    0: {OverallState.VALID},
    1: {OverallState.INVALID},
    4: {
        OverallState.UNSIGNED,
        OverallState.PARTIAL,
        OverallState.NOT_VERIFIABLE,
    },
}

_MESSAGES = {
    ErrorCode.INVALID_REQUEST: "The request is not valid.",
    ErrorCode.PATH_NOT_ALLOWED: "Path is outside the allowed roots.",
    ErrorCode.FILE_NOT_FOUND: "File was not found inside an allowed root.",
    ErrorCode.UNSUPPORTED_CODEC: "Codec must be h264 or h265.",
    ErrorCode.MALFORMED_MEDIA: "The media input is malformed.",
    ErrorCode.INVALID_TRUST_ANCHOR: "The trust anchor is not valid.",
    ErrorCode.CORE_INPUT_REJECTED: (
        "The core rejected the input and did not return a verification result."
    ),
    ErrorCode.CORE_UNAVAILABLE: "The video-trust executable is not available.",
    ErrorCode.CORE_EXECUTION_FAILED: "The core failed while verifying the file.",
    ErrorCode.CORE_TIMEOUT: "The core did not finish before the timeout.",
    ErrorCode.CORE_OUTPUT_TOO_LARGE: "Core output exceeded the allowed size.",
    ErrorCode.CONTRACT_MISMATCH: "The core result did not match schema 0.1.",
}


def child_environment(parent: Mapping[str, str]) -> dict[str, str]:
    """Environment passed to video-trust. Request data is not copied."""

    env = {key: parent[key] for key in _CHILD_ENV_KEYS if key in parent and parent[key] != ""}
    env.setdefault("LANG", "C.UTF-8")
    env.setdefault("LC_ALL", "C.UTF-8")
    return env


def build_verify_argv(
    executable: Path,
    codec: Codec,
    input_file: Path,
    trust_anchor: Path | None,
) -> list[str]:
    """Argument vector for one verify invocation. Never a shell command."""

    if not isinstance(codec, Codec):
        raise TypeError("codec must be the Codec enum")
    argv = [str(executable), "verify", "--codec", codec.value, "--json"]
    if trust_anchor is not None:
        argv.extend(["--ca", str(trust_anchor)])
    argv.append(str(input_file))
    if argv[1] != "verify":
        raise RuntimeError("core client can only invoke verify")
    return argv


def build_compare_argv(
    executable: Path,
    request: ComparePreservationRequest,
    before: Path,
    after: Path,
    before_ca: Path | None,
    after_ca: Path | None,
) -> list[str]:
    argv = [str(executable), "compare-preservation", "--codec", request.codec.value, "--json"]
    if before_ca is not None:
        argv.extend(["--before-ca", str(before_ca)])
    if after_ca is not None:
        argv.extend(["--after-ca", str(after_ca)])
    if request.transformation is not None:
        argv.extend(["--transformation", request.transformation.value])
    if request.pipeline_id is not None:
        argv.extend(["--pipeline-id", request.pipeline_id])
    argv.extend([str(before), str(after)])
    return argv


def classify_exit_2() -> ErrorCode:
    """video-trust 0.1.0 has no machine-readable exit-2 subtype.

    Stderr text is not consulted. MALFORMED_MEDIA and INVALID_TRUST_ANCHOR
    stay available for a future stable signal.
    """

    return ErrorCode.CORE_INPUT_REJECTED


@dataclass
class ProcessObservation:
    exit_code: int | None
    stdout: bytes
    stderr: bytes
    timed_out: bool = False
    stdout_overflow: bool = False
    stderr_overflow: bool = False


@dataclass
class CoreInvocation:
    evidence: Evidence
    document: VerificationDocument | None = None
    error: CapabilityError | None = None
    retained_stderr: bytes = field(default=b"", repr=False)

    def public_payload(self) -> dict[str, object]:
        return {
            "evidence": self.evidence.model_dump(mode="json"),
            "document": None if self.document is None else self.document.model_dump(mode="json"),
            "error": None if self.error is None else self.error.model_dump(mode="json"),
        }


@dataclass
class PreservationInvocation:
    evidence: PreservationEvidence
    document: PreservationAssessment | None = None
    error: CapabilityError | None = None
    retained_stderr: bytes = field(default=b"", repr=False)


def interpret_verify_process(observation: ProcessObservation) -> tuple[
    VerificationDocument | None,
    CapabilityError | None,
    str | None,
]:
    """Map a finished process onto a document or one typed error.

    `observation.stderr` is ignored for classification and is not copied
    into the error message.
    """

    if observation.timed_out:
        return None, _error(ErrorCode.CORE_TIMEOUT), None
    if observation.stdout_overflow:
        return None, _error(
            ErrorCode.CORE_EXECUTION_FAILED,
            "Core output exceeded the allowed size.",
        ), None
    exit_code = observation.exit_code
    if exit_code == 2:
        return None, _error(classify_exit_2()), None
    if exit_code == 3:
        return None, _error(ErrorCode.CORE_EXECUTION_FAILED), None
    if exit_code not in _SUCCESS_OVERALL:
        return None, _error(ErrorCode.CORE_EXECUTION_FAILED), None
    try:
        payload = json.loads(observation.stdout.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        return None, _error(ErrorCode.CONTRACT_MISMATCH, "Core stdout was not valid JSON."), None
    observed_schema = payload.get("schema_version") if isinstance(payload, dict) else None
    schema_text = observed_schema if isinstance(observed_schema, str) else None
    try:
        document = VerificationDocument.model_validate(payload)
    except ValidationError:
        return None, _error(ErrorCode.CONTRACT_MISMATCH), schema_text
    if document.overall not in _SUCCESS_OVERALL[exit_code]:
        return None, _error(
            ErrorCode.CONTRACT_MISMATCH,
            "Core exit code does not match the verification overall state.",
        ), document.schema_version
    return document, None, document.schema_version


def interpret_preservation_process(observation: ProcessObservation) -> tuple[
    PreservationAssessment | None, CapabilityError | None, str | None
]:
    if observation.timed_out:
        return None, _error(ErrorCode.CORE_TIMEOUT), None
    if observation.stdout_overflow:
        return None, _error(ErrorCode.CORE_OUTPUT_TOO_LARGE), None
    if observation.exit_code == 2:
        return None, _error(ErrorCode.CORE_INPUT_REJECTED), None
    if observation.exit_code == 3 or observation.exit_code not in (0, 1, 4):
        return None, _error(ErrorCode.CORE_EXECUTION_FAILED), None
    try:
        payload = json.loads(observation.stdout.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        return None, _error(ErrorCode.CONTRACT_MISMATCH, "Core stdout was not valid JSON."), None
    schema = payload.get("schema_version") if isinstance(payload, dict) else None
    try:
        document = PreservationAssessment.model_validate(payload)
    except ValidationError:
        return None, _error(ErrorCode.CONTRACT_MISMATCH), schema if isinstance(schema, str) else None
    expected = {
        "preserved": 0,
        "partially_preserved": 1,
        "not_preserved": 1,
        "indeterminate": 4,
    }[document.preservation.media_signing_evidence]
    if document.applicability.media_signing_preservation != "applicable":
        expected = 4
    if observation.exit_code != expected:
        return None, _error(ErrorCode.CONTRACT_MISMATCH, "Core exit code does not match preservation state."), document.schema_version
    return document, None, document.schema_version


class CoreClient:
    def __init__(self, executable: str | Path, *, timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS) -> None:
        if timeout_seconds <= 0 or timeout_seconds > MAX_TIMEOUT_SECONDS:
            raise ValueError(
                f"timeout_seconds must be greater than 0 and at most {MAX_TIMEOUT_SECONDS:g}"
            )
        self.executable = Path(executable)
        self.timeout_seconds = timeout_seconds
        self._core_version: str | None = None
        self._version_checked = False

    def verify(
        self,
        *,
        codec: Codec,
        input_file: str,
        policy: AllowedRoots,
        trust_anchor: str | None = None,
        capability: str = VERIFY_FILE_CAPABILITY,
    ) -> CoreInvocation:
        try:
            authorized_input = policy.authorize(input_file)
        except PolicyFailure as exc:
            return self._policy_failure(exc, capability=capability, codec=codec)
        authorized_anchor: AuthorizedPath | None = None
        if trust_anchor is not None:
            try:
                authorized_anchor = policy.authorize(trust_anchor)
            except PolicyFailure as exc:
                return self._policy_failure(
                    exc,
                    capability=capability,
                    codec=codec,
                    input_file=authorized_input,
                )
        if not self.executable.is_file() or not os.access(self.executable, os.X_OK):
            return self._finished(
                capability=capability,
                codec=codec,
                input_file=authorized_input,
                trust_anchor=authorized_anchor,
                document=None,
                error=_error(ErrorCode.CORE_UNAVAILABLE),
                exit_code=None,
                schema_version=None,
                stderr=b"",
            )
        input_hash = _sha256(authorized_input.canonical)
        anchor_hash = None if authorized_anchor is None else _sha256(authorized_anchor.canonical)
        observation = self._run(
            build_verify_argv(
                self.executable,
                codec,
                authorized_input.canonical,
                None if authorized_anchor is None else authorized_anchor.canonical,
            )
        )
        document, error, schema_version = interpret_verify_process(observation)
        return self._finished(
            capability=capability,
            codec=codec,
            input_file=authorized_input,
            trust_anchor=authorized_anchor,
            input_hash=input_hash,
            anchor_hash=anchor_hash,
            document=document,
            error=error,
            exit_code=None if observation.timed_out or observation.stdout_overflow else observation.exit_code,
            schema_version=schema_version,
            stderr=observation.stderr,
        )

    def compare_preservation(
        self, *, request: ComparePreservationRequest, policy: AllowedRoots
    ) -> PreservationInvocation:
        authorized: dict[str, AuthorizedPath | None] = {}
        for field_name in ("before_path", "after_path", "before_ca_path", "after_ca_path"):
            raw = getattr(request, field_name)
            if raw is None:
                authorized[field_name] = None
                continue
            try:
                authorized[field_name] = policy.authorize(raw)
            except PolicyFailure as exc:
                return self._preservation_finished(
                    request=request, authorized=authorized, document=None,
                    error=_error(exc.code, exc.message), exit_code=None, schema_version=None, stderr=b"",
                )
        if not self.executable.is_file() or not os.access(self.executable, os.X_OK):
            return self._preservation_finished(
                request=request, authorized=authorized, document=None,
                error=_error(ErrorCode.CORE_UNAVAILABLE), exit_code=None, schema_version=None, stderr=b"",
            )
        observation = self._run(build_compare_argv(
            self.executable, request,
            authorized["before_path"].canonical, authorized["after_path"].canonical,  # type: ignore[union-attr]
            None if authorized["before_ca_path"] is None else authorized["before_ca_path"].canonical,
            None if authorized["after_ca_path"] is None else authorized["after_ca_path"].canonical,
        ))
        document, error, schema_version = interpret_preservation_process(observation)
        return self._preservation_finished(
            request=request, authorized=authorized, document=document, error=error,
            exit_code=None if observation.timed_out or observation.stdout_overflow else observation.exit_code,
            schema_version=schema_version, stderr=observation.stderr,
        )

    def _preservation_finished(
        self, *, request: ComparePreservationRequest, authorized: dict[str, AuthorizedPath | None],
        document: PreservationAssessment | None, error: CapabilityError | None,
        exit_code: int | None, schema_version: str | None, stderr: bytes,
    ) -> PreservationInvocation:
        def ref(name: str) -> str | None:
            item = authorized.get(name)
            return None if item is None else item.reference
        def digest(name: str) -> str | None:
            item = authorized.get(name)
            return None if item is None else _sha256(item.canonical)
        evidence = PreservationEvidence(
            invocation_id=str(uuid.uuid4()), executed_at=datetime.now(timezone.utc), codec=request.codec,
            before_sha256=digest("before_path"), after_sha256=digest("after_path"),
            before_ca_sha256=digest("before_ca_path"), after_ca_sha256=digest("after_ca_path"),
            before_reference=ref("before_path"), after_reference=ref("after_path"),
            before_ca_reference=ref("before_ca_path"), after_ca_reference=ref("after_ca_path"),
            core_version=self._core_version_text(), core_schema_version=schema_version, core_exit_code=exit_code,
        )
        return PreservationInvocation(evidence=evidence, document=document, error=error,
                                      retained_stderr=stderr[:STDERR_CAP_BYTES])

    def _run(self, argv: list[str]) -> ProcessObservation:
        proc = subprocess.Popen(  # noqa: S603 — argv array, shell is false
            argv,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_environment(os.environ),
            shell=False,
        )
        assert proc.stdout is not None
        assert proc.stderr is not None
        stdout = bytearray()
        stderr = bytearray()
        stdout_overflow = threading.Event()
        stderr_overflow = threading.Event()
        threads = [
            threading.Thread(
                target=_drain,
                args=(proc.stdout, STDOUT_CAP_BYTES, stdout, stdout_overflow),
                daemon=True,
            ),
            threading.Thread(
                target=_drain,
                args=(proc.stderr, STDERR_CAP_BYTES, stderr, stderr_overflow),
                daemon=True,
            ),
        ]
        for thread in threads:
            thread.start()
        timed_out = False
        try:
            proc.wait(timeout=self.timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            proc.kill()
            proc.wait(timeout=5)
        if stdout_overflow.is_set() or stderr_overflow.is_set():
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)
        for thread in threads:
            thread.join(timeout=2)
        return ProcessObservation(
            exit_code=proc.returncode,
            stdout=bytes(stdout),
            stderr=bytes(stderr),
            timed_out=timed_out,
            stdout_overflow=stdout_overflow.is_set(),
            stderr_overflow=stderr_overflow.is_set(),
        )

    def _core_version_text(self) -> str | None:
        if self._version_checked:
            return self._core_version
        self._version_checked = True
        if not self.executable.is_file():
            return None
        try:
            completed = subprocess.run(  # noqa: S603
                [str(self.executable), "--version"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=child_environment(os.environ),
                shell=False,
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            return None
        text = completed.stdout.decode("utf-8", errors="replace").strip()
        prefix = "video-trust "
        if completed.returncode == 0 and text.startswith(prefix):
            self._core_version = text[len(prefix) :].split()[0]
        return self._core_version

    def _policy_failure(
        self,
        exc: PolicyFailure,
        *,
        capability: str,
        codec: Codec,
        input_file: AuthorizedPath | None = None,
    ) -> CoreInvocation:
        reference = exc.reference
        input_reference = None if input_file is None else input_file.reference
        anchor_reference = None
        if input_file is not None:
            anchor_reference = reference
        elif exc.code is not ErrorCode.PATH_NOT_ALLOWED:
            input_reference = reference
        input_hash = None
        if input_file is not None:
            input_hash = _sha256(input_file.canonical)
        return self._finished(
            capability=capability,
            codec=codec,
            input_reference=input_reference,
            anchor_reference=anchor_reference,
            input_hash=input_hash,
            document=None,
            error=_error(exc.code, exc.message),
            exit_code=None,
            schema_version=None,
            stderr=b"",
        )

    def _finished(
        self,
        *,
        capability: str,
        codec: Codec,
        document: VerificationDocument | None,
        error: CapabilityError | None,
        exit_code: int | None,
        schema_version: str | None,
        stderr: bytes,
        input_file: AuthorizedPath | None = None,
        trust_anchor: AuthorizedPath | None = None,
        input_reference: str | None = None,
        anchor_reference: str | None = None,
        input_hash: str | None = None,
        anchor_hash: str | None = None,
    ) -> CoreInvocation:
        if input_file is not None:
            input_reference = input_file.reference
        if trust_anchor is not None:
            anchor_reference = trust_anchor.reference
        evidence = Evidence(
            contract_version=CONTRACT_VERSION,
            capability=capability,
            capability_version=CAPABILITY_VERSION,
            invocation_id=str(uuid.uuid4()),
            executed_at=datetime.now(timezone.utc),
            codec=codec,
            input_sha256=input_hash,
            trust_anchor_sha256=anchor_hash,
            input_reference=input_reference,
            trust_anchor_reference=anchor_reference,
            core_version=self._core_version_text(),
            core_schema_version=schema_version,
            core_exit_code=exit_code,
        )
        return CoreInvocation(
            evidence=evidence,
            document=document,
            error=error,
            retained_stderr=stderr[:STDERR_CAP_BYTES],
        )


def _error(code: ErrorCode, message: str | None = None) -> CapabilityError:
    return CapabilityError(code=code, message=message or _MESSAGES[code], retryable=code is ErrorCode.CORE_TIMEOUT)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _drain(pipe, cap: int, buf: bytearray, overflow: threading.Event) -> None:
    total = 0
    try:
        while True:
            chunk = pipe.read(65536)
            if not chunk:
                break
            total += len(chunk)
            if total > cap:
                overflow.set()
                continue
            buf.extend(chunk)
    finally:
        pipe.close()
