"""Experimental read-only MCP stdio adapter.

This module registers three tools and translates their results. Path
policy, hashing, verification, and trust interpretation stay in the
capability core.

Run:
    NANEXUS_VIDEO_TRUST=/path/to/video-trust \\
    NANEXUS_ALLOWED_ROOTS=/path/to/media \\
    python -m nanexus_video_trust_agent.mcp_server

Optional NANEXUS_VERIFY_TIMEOUT is a number of seconds, at most 300.
The server is experimental and is not a stable agent API.
"""

from __future__ import annotations

import logging
import os
import sys
from collections.abc import Callable, Mapping
from contextvars import ContextVar
from typing import Any

from mcp.server import MCPServer
from mcp.types import CallToolResult, TextContent, ToolAnnotations
from pydantic import ValidationError

from nanexus_video_trust_agent.capabilities import CapabilityService
from nanexus_video_trust_agent.contracts import (
    ASSESS_VIDEO_INTEGRITY_CAPABILITY,
    COMPARE_PRESERVATION_CAPABILITY,
    PRODUCT_VERSION,
    VERIFY_FILE_CAPABILITY,
    CapabilityEnvelope,
    Codec,
    ExecutionStatus,
)
from nanexus_video_trust_agent.core_client import MAX_TIMEOUT_SECONDS, CoreClient
from nanexus_video_trust_agent.policy import AllowedRoots, AllowedRootsNotConfigured
from nanexus_video_trust_agent.preservation_contracts import (
    PreservationCapabilityEnvelope,
    TransformationKind,
)

logger = logging.getLogger("nanexus_video_trust_agent.mcp_server")

SERVER_INSTRUCTIONS = (
    "Experimental read-only Media Signing tools. They verify or compare supplied files and do not "
    "establish source authenticity or that a depicted event is real. They do not "
    "sign, modify, or delete files."
)
VERIFY_FILE_DESCRIPTION = (
    "Use to read the Media Signing verification record for one Annex-B file. "
    "Required: input_file and codec (h264 or h265). Optional: trust_anchor PEM. "
    "Success reports the verification axes and overall, including INVALID, UNSIGNED, "
    "PARTIAL, and NOT_VERIFIABLE. It does not establish camera identity, source "
    "authenticity, or that the depicted event is real. Read-only: it does not sign, "
    "modify, or delete the file."
)
ASSESS_VIDEO_INTEGRITY_DESCRIPTION = (
    "Use to interpret one verification as integrity, certificate trust, limitations, "
    "and follow-up hints. Required: input_file and codec (h264 or h265). Optional: "
    "trust_anchor PEM. It uses the same verification as video_trust.verify_file. "
    "A validated signing key is not a trusted camera, not source authenticity, and "
    "not proof the depicted event is real. Read-only: it does not sign, modify, or "
    "delete the file."
)
COMPARE_PRESERVATION_DESCRIPTION = (
    "Compare verifier-observable Media Signing preservation between before and after Annex-B files. "
    "Required: before_path, after_path, and codec (h264 or h265). Separate CA paths, a closed "
    "caller-declared transformation, and a bounded pipeline_id are optional and never influence "
    "classification. Returns the complete deterministic preservation assessment 0.1, including "
    "coverage, signing-metadata relationship, transitions, findings, and limitations. A valid "
    "after-state does not imply full source coverage or source authenticity. Read-only."
)
_READ_ONLY = ToolAnnotations(
    read_only_hint=True,
    destructive_hint=False,
    idempotent_hint=True,
    open_world_hint=False,
)
_raw_arguments: ContextVar[dict[str, Any] | None] = ContextVar("nanexus_mcp_raw_arguments", default=None)


def load_runtime(environ: Mapping[str, str]) -> CapabilityService:
    """Build the capability service from process configuration.

    Empty allowed roots and a missing executable refuse startup.
    """

    roots = AllowedRoots.parse(environ.get("NANEXUS_ALLOWED_ROOTS"))
    executable = environ.get("NANEXUS_VIDEO_TRUST", "").strip()
    if executable == "":
        raise RuntimeError("NANEXUS_VIDEO_TRUST is not set.")
    timeout = _timeout(environ.get("NANEXUS_VERIFY_TIMEOUT"))
    return CapabilityService(CoreClient(executable, timeout_seconds=timeout), roots)


def create_server(service: CapabilityService) -> MCPServer:
    """Register the three read-only tools. Does not open a socket."""

    server = MCPServer(
        name="nanexus-video-trust",
        version=PRODUCT_VERSION,
        instructions=SERVER_INSTRUCTIONS,
    )
    _register(server, service.verify_file, VERIFY_FILE_CAPABILITY, VERIFY_FILE_DESCRIPTION)
    _register(
        server,
        service.assess_video_integrity,
        ASSESS_VIDEO_INTEGRITY_CAPABILITY,
        ASSESS_VIDEO_INTEGRITY_DESCRIPTION,
    )
    _register_compare(server, service)
    _capture_raw_arguments(server)
    return server


def main() -> None:
    """Start the stdio server. Logs go to stderr."""

    logging.basicConfig(level=logging.INFO, stream=sys.stderr, format="%(levelname)s %(message)s")
    try:
        service = load_runtime(os.environ)
    except (AllowedRootsNotConfigured, RuntimeError, ValueError) as exc:
        logger.error("server configuration failed: %s", exc)
        raise SystemExit(2) from exc
    logger.info("starting experimental read-only stdio server")
    create_server(service).run(transport="stdio")


def _register(
    server: MCPServer,
    method: Callable[..., CapabilityEnvelope],
    name: str,
    description: str,
) -> None:
    async def tool(
        input_file: str,
        codec: Codec,
        trust_anchor: str | None = None,
    ) -> CapabilityEnvelope:
        raw = _raw_arguments.get()
        try:
            payload: dict[str, Any]
            if raw is None:
                payload = {"input_file": input_file, "codec": codec}
                if trust_anchor is not None:
                    payload["trust_anchor"] = trust_anchor
            else:
                payload = raw
            return _tool_result(method(payload))
        finally:
            _raw_arguments.set(None)

    tool.__name__ = name.replace(".", "_")
    server.tool(name=name, description=description, annotations=_READ_ONLY)(tool)


def _register_compare(server: MCPServer, service: CapabilityService) -> None:
    async def tool(
        before_path: str,
        after_path: str,
        codec: Codec,
        before_ca_path: str | None = None,
        after_ca_path: str | None = None,
        transformation: TransformationKind | None = None,
        pipeline_id: str | None = None,
    ) -> PreservationCapabilityEnvelope:
        raw = _raw_arguments.get()
        try:
            payload = raw or {
                "before_path": before_path,
                "after_path": after_path,
                "codec": codec,
                "before_ca_path": before_ca_path,
                "after_ca_path": after_ca_path,
                "transformation": transformation,
                "pipeline_id": pipeline_id,
            }
            return _tool_result(service.compare_preservation(payload))  # type: ignore[return-value]
        finally:
            _raw_arguments.set(None)

    tool.__name__ = "video_trust_compare_preservation"
    server.tool(
        name=COMPARE_PRESERVATION_CAPABILITY,
        description=COMPARE_PRESERVATION_DESCRIPTION,
        annotations=_READ_ONLY,
    )(tool)


def _capture_raw_arguments(server: MCPServer) -> None:
    """Keep the caller's argument object so extra fields reach the capability."""

    for tool in server._tool_manager.list_tools():
        original = tool.fn_metadata.validate_arguments
        tool_name = tool.name

        def validate(arguments: dict[str, Any], original: Callable[..., dict[str, Any]] = original,
                     tool_name: str = tool_name) -> dict[str, Any]:
            _raw_arguments.set(dict(arguments))
            try:
                return original(arguments)
            except ValidationError:
                if tool_name == COMPARE_PRESERVATION_CAPABILITY:
                    return {"before_path": "x", "after_path": "x", "codec": Codec.h264}
                return {"input_file": "x", "codec": Codec.h264, "trust_anchor": None}

        object.__setattr__(tool.fn_metadata, "validate_arguments", validate)
        tool.parameters["additionalProperties"] = False


def _tool_result(envelope: CapabilityEnvelope | PreservationCapabilityEnvelope) -> CallToolResult:
    """One envelope, as structured content and compact JSON text."""

    return CallToolResult(
        content=[TextContent(type="text", text=envelope.model_dump_json())],
        structuredContent=envelope.model_dump(mode="json"),
        isError=envelope.execution_status is ExecutionStatus.failed,
    )


def _timeout(value: str | None) -> float:
    if value is None or value.strip() == "":
        return 60.0
    try:
        timeout = float(value)
    except ValueError as exc:
        raise ValueError("NANEXUS_VERIFY_TIMEOUT must be a number of seconds.") from exc
    if timeout <= 0 or timeout > MAX_TIMEOUT_SECONDS:
        raise ValueError("NANEXUS_VERIFY_TIMEOUT must be greater than 0 and at most 300 seconds.")
    return timeout


if __name__ == "__main__":
    main()
