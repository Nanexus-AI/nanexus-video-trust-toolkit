"""Driver slot for a future model runtime.

A driver turns one task into an observable transcript. The scorer does
not know which runtime produced it.
"""

from __future__ import annotations

from typing import Protocol

from tests.eval.corpus import EvalTask
from tests.eval.transcript import EvalTranscript, ModelTier


class EvalDriver(Protocol):
    name: str
    tier: ModelTier

    def run(self, task: EvalTask) -> EvalTranscript:
        """Run one task and return observable behavior only."""
