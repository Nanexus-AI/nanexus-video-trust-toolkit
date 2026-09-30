# Roadmap

High-level milestone picture for Nanexus Video Trust Toolkit.

**Project status:** Experimental / Pre-release.  
**Stable end-user release:** none yet.  
**Current published developer release:** **M1 / `v0.1.0`** (Pre-release).

M1.5 and M2 are implemented in current `main` development history. M3 is
complete and published on current `main`. None is part of the `v0.1.0` tag or
implies that a later release exists.

This is an independent open-source project using the ONVIF Media Signing
framework/standard. It is **not** an official ONVIF project.

| Milestone | Title | Status |
| --- | --- | --- |
| **M0** | Feasibility Spike | **Complete** |
| **M1** | File-Based Reference Lab | **Complete** — `v0.1.0` released (Pre-release) |
| **M1.5** | Experimental Agent interface | **Implemented on `main`** — not a stable API or a tagged release |
| **M2** | Inspect + Report | **Implemented on current `main`** — unreleased; not a tagged release |
| **M3** | VMS/NVR Preservation Testing | **Complete and published on current `main`** — not a tagged release |
| **M4** | Passive Live RTSP Verification | Planned |
| **M5** | ARM64 / Edge Deployment | Planned |
| **M6** | Integration Layer | Planned |
| **M7** | Optional Legacy Signing Gateway | Planned |
| **M8** | Native Media Signing Camera | Planned |

## M0 — Feasibility Spike (complete)

Prove that the pinned official ONVIF Media Signing Framework builds and that
official H.264/H.265 sign→verify plus controlled tamper detection work, and that
the official C API is a viable foundation for a thin C++20 Nanexus layer.

See [`m0-feasibility.md`](m0-feasibility.md).

## M1 — File-Based Reference Lab (`v0.1.0` released)

First usable developer release (`v0.1.0`): file-based reference lab with
`sign` / `verify` / `tamper`, H.264/H.265 Annex-B, structured results, CLI + JSON,
deterministic fixtures, and CI. Published as a GitHub Pre-release.

## M1.5 — Experimental Agent interface (on `main`)

Typed read-only capabilities over the same `video-trust verify` core:
`video_trust.verify_file` (L1) and `video_trust.assess_video_integrity` (L2),
plus a thin MCP stdio adapter and an evaluation harness. Sign and tamper stay
on the CLI. This is experimental. It does not replace the domain roadmap and
it is not a new product version tag.

From M2 onward, each milestone should note, alongside the domain work:

* any new Agent-facing capability and its level (L1, L2, or a future L3)
* contract, evidence, and safety impact
* whether stronger and smaller models can use the same explicit contract
* backward compatibility

That review sits next to the domain milestone. It does not turn the project
into an Agent platform.

## M2 — Inspect + Report (implemented on current `main`)

Adds `video-trust inspect` over the existing file-based ONVIF validation
pipeline for Annex-B H.264/H.265. Human-readable output is the default;
`--json` emits a distinct `media_signing_inspection` document with inspection
schema version `0.1`. The formal schema is
[`schemas/media-signing-inspection-0.1.json`](schemas/media-signing-inspection-0.1.json),
with contract details in [`inspection-v0.1.md`](inspection-v0.1.md).

Inspection adds typed validation counts, raw FILETIME-style timestamp values,
and optional vendor observations around the unchanged M1 verification context.
It does not produce a stronger trust verdict: vendor observations do not prove
device identity, and certificate trust does not establish source authenticity.
The M1 verify JSON contract remains at version `0.1`.

M2 inspection is available through the CLI/domain surface, not a separate MCP
tool. Publication review and any future tag or release remain separate steps.

## M3 — VMS/NVR Preservation Testing (complete on current `main`)

Adds bounded H.264/H.265 artifact correlation, an independent preservation
domain and closed JSON Schema `0.1`, `video-trust compare-preservation`, and a
reproducible synthetic transformation matrix. A sanitized real-system study
measures finite signed media through RTSP/TCP ingest, Frigate 0.17.1 recording,
realtime export, Annex-B extraction, and Nanexus assessment. The result is
limited to the tested configuration; it is not a universal Frigate or VMS/NVR
compatibility claim. See [`m3-frigate-case-study.md`](m3-frigate-case-study.md).

The Agent catalog adds one approved read-only L1 primitive,
`video_trust.compare_preservation`, while preserving the two existing tool
contracts. M3 does not add live RTSP verification; that remains M4 work.

## Later milestones (summary)

* **M3** — finite stored/exported-media preservation assessment (complete on current `main`)
* **M4** — passive live RTSP verification
* **M5** — ARM64 / edge deployment (e.g. RK3588 / Jetson class targets)
* **M6** — integration layer for adjacent systems
* **M7** — optional gateway for legacy signing environments
* **M8** — native Media Signing camera path

Milestone titles may be refined as design work proceeds; the progression above is
the agreed high-level picture.
