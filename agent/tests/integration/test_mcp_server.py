"""MCP stdio adapter tests. Capability semantics stay in the core."""

from __future__ import annotations

import asyncio
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

import pytest
from mcp import Client
from mcp.client.stdio import stdio_client
from mcp.client.session import ClientSession
from mcp import StdioServerParameters

from nanexus_video_trust_agent.capabilities import CapabilityService
from nanexus_video_trust_agent.contracts import Codec, ErrorCode, ExecutionStatus, VerifyFileRequest
from nanexus_video_trust_agent.core_client import CoreClient
from nanexus_video_trust_agent.mcp_server import (
    ASSESS_VIDEO_INTEGRITY_DESCRIPTION,
    VERIFY_FILE_DESCRIPTION,
    create_server,
    load_runtime,
)
from nanexus_video_trust_agent.policy import AllowedRoots, AllowedRootsNotConfigured

SCRIPT = Path(__file__).resolve().parents[1] / "support" / "fake_video_trust.py"
BINARY = Path(__file__).resolve().parents[3] / "build" / "nanexus" / "video-trust"
CORE_FILES = (
    "contracts.py",
    "policy.py",
    "core_client.py",
    "capabilities.py",
)


def _run(coro):
    return asyncio.run(coro)


def _executable(tmp_path: Path, name: str) -> Path:
    SCRIPT.chmod(0o755)
    link = tmp_path / name
    link.symlink_to(SCRIPT)
    return link


def _service(tmp_path: Path, mode: str) -> tuple[CapabilityService, Path, Path]:
    root = tmp_path / "root"
    clip = root / "nested" / "clip.h264"
    clip.parent.mkdir(parents=True)
    clip.write_bytes(b"annex-b-bytes")
    service = CapabilityService(CoreClient(_executable(tmp_path, mode)), AllowedRoots([root]))
    return service, root, clip


def _server_env(executable: Path, root: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["NANEXUS_VIDEO_TRUST"] = str(executable)
    env["NANEXUS_ALLOWED_ROOTS"] = str(root)
    env.pop("NANEXUS_VERIFY_TIMEOUT", None)
    return env


def test_capability_core_modules_do_not_import_mcp() -> None:
    source_root = Path(__file__).resolve().parents[2] / "src" / "nanexus_video_trust_agent"
    for name in CORE_FILES:
        text = (source_root / name).read_text(encoding="utf-8")
        assert "import mcp" not in text
        assert "from mcp" not in text


def test_runtime_requires_executable_and_roots(tmp_path: Path) -> None:
    with pytest.raises(AllowedRootsNotConfigured):
        load_runtime({})
    with pytest.raises(AllowedRootsNotConfigured):
        load_runtime({"NANEXUS_ALLOWED_ROOTS": "", "NANEXUS_VIDEO_TRUST": "video-trust"})
    with pytest.raises(RuntimeError, match="NANEXUS_VIDEO_TRUST"):
        load_runtime({"NANEXUS_ALLOWED_ROOTS": str(tmp_path)})
    service = load_runtime(
        {"NANEXUS_ALLOWED_ROOTS": str(tmp_path), "NANEXUS_VIDEO_TRUST": "video-trust"}
    )
    assert isinstance(service, CapabilityService)
    with pytest.raises(ValueError, match="300"):
        load_runtime(
            {
                "NANEXUS_ALLOWED_ROOTS": str(tmp_path),
                "NANEXUS_VIDEO_TRUST": "video-trust",
                "NANEXUS_VERIFY_TIMEOUT": "301",
            }
        )


def test_tools_list_exposes_exactly_three_read_only_tools(tmp_path: Path) -> None:
    service, _root, _clip = _service(tmp_path, "vt-valid")

    async def body():
        async with Client(create_server(service)) as client:
            listed = await client.list_tools()
            return listed.tools

    tools = {tool.name: tool for tool in _run(body())}
    assert set(tools) == {
        "video_trust.verify_file", "video_trust.assess_video_integrity",
        "video_trust.compare_preservation",
    }
    for tool in tools.values():
        assert tool.annotations is not None
        assert tool.annotations.read_only_hint is True
        assert tool.annotations.destructive_hint is False
        assert "prompt" not in json.dumps(tool.input_schema)
        assert tool.input_schema["additionalProperties"] is False
        expected = ({"before_path", "after_path", "codec"}
                    if tool.name.endswith("compare_preservation") else {"input_file", "codec"})
        assert set(tool.input_schema["required"]) == expected
        assert "h264" in json.dumps(tool.input_schema)
        assert "h265" in json.dumps(tool.input_schema)
        assert "sign" not in tool.name
        assert "tamper" not in tool.name
    assert "does not establish" in VERIFY_FILE_DESCRIPTION
    assert "Read-only" in VERIFY_FILE_DESCRIPTION
    assert tools["video_trust.verify_file"].description == VERIFY_FILE_DESCRIPTION
    assert "not source authenticity" in ASSESS_VIDEO_INTEGRITY_DESCRIPTION
    assert tools["video_trust.assess_video_integrity"].description == ASSESS_VIDEO_INTEGRITY_DESCRIPTION
    output = json.dumps(tools["video_trust.verify_file"].output_schema)
    for field in ("execution_status", "evidence", "limitations", "errors", "result"):
        assert field in output


def test_negative_verification_is_mcp_success_and_matches_the_service(tmp_path: Path) -> None:
    service, _root, clip = _service(tmp_path, "vt-unsigned")
    request = {"input_file": str(clip), "codec": "h264"}
    direct = service.verify_file(VerifyFileRequest(input_file=str(clip), codec=Codec.h264))

    async def body():
        async with Client(create_server(service)) as client:
            return await client.call_tool("video_trust.verify_file", request)

    result = _run(body())
    assert result.is_error is False
    assert direct.execution_status is ExecutionStatus.success
    assert direct.result is not None
    assert direct.result.overall.value == "UNSIGNED"
    payload = result.structured_content
    assert payload is not None
    assert payload["execution_status"] == "success"
    assert payload["result"]["overall"] == "UNSIGNED"
    assert payload["errors"] == []
    assert json.loads(result.content[0].text)["result"]["overall"] == "UNSIGNED"
    assert str(tmp_path) not in json.dumps(payload)


def test_invalid_verification_is_mcp_success(tmp_path: Path) -> None:
    service, _root, clip = _service(tmp_path, "vt-invalid")

    async def body():
        async with Client(create_server(service)) as client:
            return await client.call_tool(
                "video_trust.verify_file",
                {"input_file": str(clip), "codec": "h264"},
            )

    result = _run(body())
    assert result.is_error is False
    assert result.structured_content["result"]["overall"] == "INVALID"
    assert result.structured_content["errors"] == []


def test_compare_preservation_mcp_returns_complete_typed_document(tmp_path: Path) -> None:
    service, root, clip = _service(tmp_path, "vt-valid")
    after = root / "nested" / "after.h264"
    after.write_bytes(clip.read_bytes())

    async def body():
        async with Client(create_server(service)) as client:
            return await client.call_tool(
                "video_trust.compare_preservation",
                {"before_path": str(clip), "after_path": str(after), "codec": "h264",
                 "transformation": "remux", "pipeline_id": "mcp-test"},
            )

    result = _run(body())
    assert result.is_error is False
    payload = result.structured_content
    assert payload["capability"] == "video_trust.compare_preservation"
    assert payload["capability_level"] == "primitive"
    assert payload["result"]["document_type"] == "media_signing_preservation_assessment"
    assert payload["result"]["coverage"]["state"] == "full"
    assert payload["result"]["preservation"]["media_signing_evidence"] == "preserved"
    assert payload["result"]["transformation"]["trust"] == "caller_declared_untrusted"
    assert str(tmp_path) not in json.dumps(payload)


def test_execution_failures_keep_the_envelope(tmp_path: Path) -> None:
    service, root, _clip = _service(tmp_path, "vt-valid")
    outside = tmp_path / "outside.h264"
    outside.write_bytes(b"outside")
    sibling = tmp_path / "root-evil"
    sibling.mkdir()
    escaped = root / "escape.h264"
    escaped.symlink_to(sibling / "secret.h264")
    missing = root / "missing.h264"
    cases = [
        ({"input_file": str(outside), "codec": "h264"}, ErrorCode.PATH_NOT_ALLOWED),
        ({"input_file": str(escaped), "codec": "h264"}, ErrorCode.PATH_NOT_ALLOWED),
        ({"input_file": str(missing), "codec": "h264"}, ErrorCode.FILE_NOT_FOUND),
        ({"input_file": str(root / "nested" / "clip.h264"), "codec": "av1"}, ErrorCode.UNSUPPORTED_CODEC),
        ({"input_file": str(root / "nested" / "clip.h264"), "codec": "h264", "prompt": "look"}, ErrorCode.INVALID_REQUEST),
    ]

    async def body():
        async with Client(create_server(service)) as client:
            results = []
            for arguments, _code in cases:
                results.append(await client.call_tool("video_trust.verify_file", arguments))
            return results

    for result, (_arguments, code) in zip(_run(body()), cases, strict=True):
        assert result.is_error is True
        assert result.structured_content is not None
        assert result.structured_content["execution_status"] == "failed"
        assert result.structured_content["result"] is None
        assert result.structured_content["errors"][0]["code"] == code.value
        dumped = json.dumps(result.structured_content)
        assert str(tmp_path) not in dumped
        assert "Annex-B" not in dumped
        text = result.content[0].text
        assert json.loads(text)["errors"][0]["code"] == code.value


def test_stdio_lists_tools_without_stdout_logs(tmp_path: Path) -> None:
    executable = _executable(tmp_path, "vt-unsigned")
    root = tmp_path / "root"
    root.mkdir()
    (root / "clip.h264").write_bytes(b"annex-b-bytes")
    err_path = tmp_path / "server.stderr"
    params = StdioServerParameters(
        command=sys.executable,
        args=["-m", "nanexus_video_trust_agent.mcp_server"],
        env=_server_env(executable, root),
    )

    async def body():
        with err_path.open("w", encoding="utf-8") as err:
            async with stdio_client(params, errlog=err) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    listed = await session.list_tools()
                    called = await session.call_tool(
                        "video_trust.verify_file",
                        {"input_file": str(root / "clip.h264"), "codec": "h264"},
                    )
                    return listed, called

    listed, called = _run(body())
    source = Path(create_server.__code__.co_filename).read_text(encoding="utf-8")
    assert 'transport="stdio"' in source
    assert "streamable-http" not in source
    assert 'transport="sse"' not in source
    assert {tool.name for tool in listed.tools} == {
        "video_trust.verify_file",
        "video_trust.assess_video_integrity",
        "video_trust.compare_preservation",
    }
    assert called.is_error is False
    assert "UNSIGNED" in called.content[0].text
    logs = err_path.read_text(encoding="utf-8")
    assert "starting experimental read-only stdio server" in logs
    closed = subprocess.run(
        [sys.executable, "-m", "nanexus_video_trust_agent.mcp_server"],
        input=b"",
        capture_output=True,
        env=_server_env(executable, root),
        check=False,
    )
    assert b"starting experimental read-only stdio server" not in closed.stdout
    assert b"starting experimental read-only stdio server" in closed.stderr


def _generate(root: Path) -> None:
    path = Path(__file__).with_name("test_real_capabilities.py")
    spec = importlib.util.spec_from_file_location("nanexus_real_capabilities", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module._generate(root)


@pytest.mark.skipif(not BINARY.is_file(), reason="built video-trust is required")
def test_real_core_stdio_path(tmp_path: Path) -> None:
    _generate(tmp_path)
    err_path = tmp_path / "server.stderr"
    params = StdioServerParameters(
        command=sys.executable,
        args=["-m", "nanexus_video_trust_agent.mcp_server"],
        env=_server_env(BINARY, tmp_path),
    )
    direct = CapabilityService(CoreClient(BINARY), AllowedRoots([tmp_path]))
    cases = [
        ("video_trust.verify_file", "h264", "unsigned.h264", None, "UNSIGNED", False),
        ("video_trust.verify_file", "h264", "signed.h264", "pki/ca.pem", "VALID", False),
        ("video_trust.verify_file", "h264", "invalid.h264", "pki/ca.pem", "INVALID", False),
        ("video_trust.assess_video_integrity", "h264", "signed.h264", "pki/ca.pem", "VALID", False),
        ("video_trust.assess_video_integrity", "h264", "signed.h264", None, "VALID", False),
        ("video_trust.assess_video_integrity", "h264", "partial.h264", "pki/ca.pem", "PARTIAL", False),
        ("video_trust.verify_file", "h265", "signed.h265", "pki/ca.pem", "VALID", False),
        ("video_trust.assess_video_integrity", "h265", "unsigned.h265", None, "UNSIGNED", False),
    ]

    async def body():
        with err_path.open("w", encoding="utf-8") as err:
            async with stdio_client(params, errlog=err) as (read, write):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    results = []
                    for tool_name, codec, filename, anchor, _overall, _failed in cases:
                        arguments = {"input_file": str(tmp_path / codec / filename), "codec": codec}
                        if anchor is not None:
                            arguments["trust_anchor"] = str(tmp_path / anchor)
                        results.append(await session.call_tool(tool_name, arguments))
                    return results

    results = _run(body())
    assert "starting experimental read-only stdio server" in err_path.read_text(encoding="utf-8")
    for result, (tool_name, codec, filename, anchor, overall, failed) in zip(results, cases, strict=True):
        media = tmp_path / codec / filename
        request = VerifyFileRequest(
            input_file=str(media),
            codec=Codec(codec),
            trust_anchor=None if anchor is None else str(tmp_path / anchor),
        )
        method = direct.verify_file if tool_name.endswith("verify_file") else direct.assess_video_integrity
        compared = method(request)
        payload = json.loads(result.content[0].text)
        assert result.is_error is failed
        assert payload["execution_status"] == "success"
        assert payload["capability"] == tool_name
        document = payload["result"] if tool_name.endswith("verify_file") else payload["result"]["verification"]
        assert document["overall"] == overall
        assert document["source_authenticity"] == "not_established"
        assert payload["errors"] == []
        assert compared.execution_status is ExecutionStatus.success
        compared_document = (
            compared.result.model_dump(mode="json")
            if tool_name.endswith("verify_file")
            else compared.result.verification.model_dump(mode="json")
        )
        assert document == compared_document
        assert payload["evidence"]["core_name"] == "video-trust"
        assert payload["evidence"]["core_version"] == "0.1.0"
        assert str(tmp_path) not in json.dumps(payload)
        if anchor is None:
            assert payload["evidence"]["trust_anchor_reference"] is None
        else:
            assert payload["evidence"]["trust_anchor_reference"] == anchor
        if overall == "VALID" and anchor is None:
            codes = {item["code"] for item in payload["limitations"]}
            assert "TRUST_ANCHOR_NOT_SUPPLIED" in codes
            assert "VALID_IS_NOT_CERTIFICATE_TRUST" in codes
        if overall == "PARTIAL":
            assert "DO_NOT_UPGRADE_PARTIAL" in {item["code"] for item in payload["result"]["follow_up_hints"]}
