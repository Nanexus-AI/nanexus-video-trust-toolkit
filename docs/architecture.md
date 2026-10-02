# Architecture (public overview)

## Purpose

Nanexus Video Trust Toolkit is intended to become an open-source, vendor-neutral
Video Trust toolkit. The first technical focus is **ONVIF Media Signing**.

Longer-term areas (not implemented yet) may include video integrity tooling,
signer/source identity, certificate/PKI handling, time integrity, evidence
verification, provenance/chain of custody, passive live
verification, and C2PA interoperability.

## Layering

```text
ONVIF Media Signing Framework
            │
            ▼
Verification / domain mapping
            │
            ├────────► VerificationResult ──► verify text / JSON 0.1
            │
            └────────► InspectionResult ────► inspect text / JSON 0.1
                                             (CLI/domain only)

Agent path (typed read-only contracts):
VerificationResult / PreservationAssessment ──► Python CoreClient
            │
            ▼
Agent capability layer
  L1  video_trust.verify_file
  L1  video_trust.compare_preservation
  L2  video_trust.assess_video_integrity
            │
            ▼
MCP stdio adapter
            │
            ▼
External Agent / runtime
```

The C++ core does not depend on MCP or on Python. `VerificationResult` provides
the compact M1 axes and verdict used by `verify`; `InspectionResult` adds typed
observations for `inspect` without changing those axes. Both are derived from
the same official ONVIF validation pipeline. The capability layer does
not depend on an Agent runtime. The MCP adapter only registers tools and
returns envelopes. Direct CLI use does not go through that adapter.

The Nanexus layer must use the official Media Signing implementation for:

* cryptographic primitives used by Media Signing
* Media Signing wire format and signing SEI format
* hash-chain / signature mechanics

## Milestone posture

* **M0** — feasibility spike against the official framework (build, H.264/H.265
  sign→verify, controlled tamper classification, API understanding). **Complete.**
* **M1** — file-based reference lab (`v0.1.0` released): sign / verify / tamper
  with structured results and CI. **Complete.**
* **M1.5** — experimental read-only Agent capability layer over `video-trust verify`,
  with L1 `video_trust.verify_file`, L2 `video_trust.assess_video_integrity`, and
  a stdio MCP adapter. **Implemented on `main`.** It is not a stable SDK, not a
  tagged release, and not an Agent product. See [`agent-interface.md`](agent-interface.md).
* **M2** — file-based inspect/report surface with human-readable output and a
  separate inspection JSON 0.1 contract. **Implemented on current `main`, not
  in the `v0.1.0` release.** It adds richer observations, not a stronger trust
  verdict, and is not exposed as a separate Agent/MCP capability. See
  [`inspection-v0.1.md`](inspection-v0.1.md).
* **M3** — finite-file correlation and preservation assessment, synthetic
  H.264/H.265 transformation matrix, configuration-scoped VMS/NVR field
  characterization, and L1 `video_trust.compare_preservation`. **Complete and
  published on current `main`; not a tagged release.** It does not add M4 live
  RTSP behavior.
* **M4** — bounded, one-stream H.264/H.265 RTSP/TCP verification through a
  fixed GStreamer ingest graph, with a separate live JSON Lines 0.1 contract.
  **Complete locally and awaiting a publication decision; unpublished.** It
  does not decode, render, transcode, re-encode, restream,
  or record, and it makes no continuity claim across an unobserved gap. See
  [`m4-frigate-live-case-study.md`](m4-frigate-live-case-study.md).

See [`roadmap.md`](roadmap.md). This project is independent open-source work that
uses the ONVIF Media Signing framework; it is not an official ONVIF project.

## Experimental stdio server

Configuration and tool contracts: [`agent-interface.md`](agent-interface.md).

```bash
cd agent
NANEXUS_VIDEO_TRUST=/path/to/video-trust \
NANEXUS_ALLOWED_ROOTS=/path/to/media \
uv run --python 3.12 python -m nanexus_video_trust_agent.mcp_server
```

`NANEXUS_ALLOWED_ROOTS` is `os.pathsep`-separated. Empty roots refuse startup.
Optional `NANEXUS_VERIFY_TIMEOUT` is seconds, at most 300. The server exposes
`video_trust.verify_file`, `video_trust.compare_preservation`, and
`video_trust.assess_video_integrity`. It does not sign or modify files, and its
results do not establish source authenticity.
