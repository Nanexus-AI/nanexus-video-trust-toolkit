"""Allowed-root checks for agent-facing file paths.

Canonical absolute paths stay inside this module. Callers receive a
root-relative reference or a policy error whose message has no host path.
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path

from nanexus_video_trust_agent.contracts import ErrorCode

_LABEL_RE = re.compile(r"^[A-Za-z0-9_-]+$")


class AllowedRootsNotConfigured(Exception):
    """Raised when a caller tries to use the policy with no roots."""


class PolicyFailure(Exception):
    """A path was rejected before any core process started."""

    def __init__(self, code: ErrorCode, message: str, reference: str | None = None) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.reference = reference


@dataclass(frozen=True)
class AuthorizedPath:
    """Internal authorization result.

    `canonical` is for enforcement and process argv only.
    `reference` is the value that may appear in agent-facing evidence.
    """

    canonical: Path
    reference: str


class AllowedRoots:
    def __init__(self, roots: list[str | Path], labels: list[str] | None = None) -> None:
        if not roots:
            raise AllowedRootsNotConfigured("Allowed roots are not configured.")
        resolved: list[Path] = []
        for root in roots:
            candidate = Path(root).expanduser().resolve(strict=False)
            if not candidate.is_dir():
                raise ValueError("An allowed root is missing or is not a directory.")
            resolved.append(candidate)
        if labels is None:
            labels = [f"root-{index}" for index in range(len(resolved))]
        if len(labels) != len(resolved) or any(_LABEL_RE.fullmatch(label) is None for label in labels):
            raise ValueError("Allowed-root labels must be opaque tokens.")
        self._roots = resolved
        self._labels = labels

    @classmethod
    def parse(cls, value: str | None, labels: list[str] | None = None) -> AllowedRoots:
        if value is None or value.strip() == "":
            raise AllowedRootsNotConfigured("Allowed roots are not configured.")
        parts = [part for part in value.split(os.pathsep) if part]
        if not parts:
            raise AllowedRootsNotConfigured("Allowed roots are not configured.")
        return cls(parts, labels=labels)

    def authorize(self, raw_path: str) -> AuthorizedPath:
        if raw_path.strip() == "" or "\x00" in raw_path or "\n" in raw_path or "\r" in raw_path:
            raise PolicyFailure(ErrorCode.INVALID_REQUEST, "Path must be a single non-empty path.")
        try:
            resolved = Path(raw_path).expanduser().resolve(strict=False)
        except (OSError, RuntimeError) as exc:
            raise PolicyFailure(
                ErrorCode.PATH_NOT_ALLOWED,
                "Path is outside the allowed roots.",
            ) from exc

        matched: tuple[int, Path] | None = None
        for index, root in enumerate(self._roots):
            if resolved == root or _is_relative_to(resolved, root):
                matched = (index, root)
                break
        if matched is None:
            raise PolicyFailure(ErrorCode.PATH_NOT_ALLOWED, "Path is outside the allowed roots.")

        index, root = matched
        reference = _reference(resolved, root, self._labels[index], multiple=len(self._roots) > 1)
        if not resolved.exists():
            raise PolicyFailure(
                ErrorCode.FILE_NOT_FOUND,
                "File was not found inside an allowed root.",
                reference=reference or None,
            )
        if not resolved.is_file():
            raise PolicyFailure(
                ErrorCode.INVALID_REQUEST,
                "Path is not a regular file.",
                reference=reference or None,
            )
        return AuthorizedPath(canonical=resolved, reference=reference)


def _is_relative_to(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _reference(resolved: Path, root: Path, label: str, *, multiple: bool) -> str:
    relative = resolved.relative_to(root).as_posix()
    if relative in ("", "."):
        return ""
    if multiple:
        body = f"{label}/{relative}"
    else:
        body = relative
    if body.startswith("/") or ".." in body.split("/"):
        raise PolicyFailure(ErrorCode.PATH_NOT_ALLOWED, "Path is outside the allowed roots.")
    return body
