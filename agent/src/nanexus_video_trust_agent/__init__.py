"""Read-only Video Trust contracts, core client, and capability functions."""

from nanexus_video_trust_agent.capabilities import CapabilityService
from nanexus_video_trust_agent.contracts import PRODUCT_VERSION

__version__ = PRODUCT_VERSION

__all__ = ["CapabilityService", "__version__"]
