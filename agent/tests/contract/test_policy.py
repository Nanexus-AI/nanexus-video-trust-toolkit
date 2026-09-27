"""Allowed-root policy tests."""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from nanexus_video_trust_agent.contracts import ErrorCode
from nanexus_video_trust_agent.policy import AllowedRoots, AllowedRootsNotConfigured, PolicyFailure


def _write(path: Path, data: bytes = b"clip") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def test_empty_roots_refuse_use() -> None:
    with pytest.raises(AllowedRootsNotConfigured):
        AllowedRoots([])
    with pytest.raises(AllowedRootsNotConfigured):
        AllowedRoots.parse(None)
    with pytest.raises(AllowedRootsNotConfigured):
        AllowedRoots.parse("")
    with pytest.raises(AllowedRootsNotConfigured):
        AllowedRoots.parse(os.pathsep)


def test_nested_file_is_root_relative(tmp_path: Path) -> None:
    root = tmp_path / "video-lab"
    clip = _write(root / "nested" / "clip.h264")
    policy = AllowedRoots([root])
    authorized = policy.authorize(str(clip))
    assert authorized.reference == "nested/clip.h264"
    assert str(root) not in authorized.reference
    assert authorized.canonical == clip.resolve()


def test_parent_escape_prefix_sibling_and_symlink(tmp_path: Path) -> None:
    root = tmp_path / "video-lab"
    outside = _write(tmp_path / "video-lab-evil" / "clip.h264")
    outside.chmod(0o000)
    nested = root / "nested"
    nested.mkdir(parents=True)
    link = root / "escape.h264"
    link.symlink_to(outside)
    policy = AllowedRoots([root])

    escaped = policy_error(policy, str(root / "nested" / ".." / ".." / "video-lab-evil" / "clip.h264"))
    assert escaped.code is ErrorCode.PATH_NOT_ALLOWED
    assert escaped.reference is None
    assert str(outside) not in escaped.message

    sibling = policy_error(policy, str(outside))
    assert sibling.code is ErrorCode.PATH_NOT_ALLOWED

    linked = policy_error(policy, str(link))
    assert linked.code is ErrorCode.PATH_NOT_ALLOWED
    assert linked.reference is None


def test_missing_file_and_directory(tmp_path: Path) -> None:
    root = tmp_path / "video-lab"
    root.mkdir()
    (root / "subdir").mkdir()
    policy = AllowedRoots([root])

    missing = policy_error(policy, str(root / "missing.h264"))
    assert missing.code is ErrorCode.FILE_NOT_FOUND
    assert missing.reference == "missing.h264"
    assert str(root) not in missing.message

    directory = policy_error(policy, str(root / "subdir"))
    assert directory.code is ErrorCode.INVALID_REQUEST
    assert directory.reference == "subdir"


def test_multiple_roots_use_opaque_labels(tmp_path: Path) -> None:
    first = tmp_path / "one"
    second = tmp_path / "two"
    first.mkdir()
    clip = _write(second / "clip.h264")
    policy = AllowedRoots([first, second])
    authorized = policy.authorize(str(clip))
    assert authorized.reference == "root-1/clip.h264"
    assert str(second) not in authorized.reference


def policy_error(policy: AllowedRoots, path: str) -> PolicyFailure:
    with pytest.raises(PolicyFailure) as caught:
        policy.authorize(path)
    return caught.value
