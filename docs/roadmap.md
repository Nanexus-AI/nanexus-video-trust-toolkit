# Roadmap

High-level milestone picture for Nanexus Video Trust Toolkit.

**Project status:** Experimental / Pre-release.  
**Stable end-user release:** none yet.  
**First intended usable developer release:** **M1 / `v0.1.0`**.

This is an independent open-source project using the ONVIF Media Signing
framework/standard. It is **not** an official ONVIF project.

| Milestone | Title | Status |
| --- | --- | --- |
| **M0** | Feasibility Spike | **Complete** |
| **M1** | File-Based Reference Lab | **Complete** — `v0.1.0` released (Pre-release) |
| **M1.5** | Experimental Agent interface | **Implemented on `main`** — not a stable API or a tagged release |
| **M2** | Inspect + Report | Planned |
| **M3** | VMS/NVR Preservation Testing | Planned |
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

## Later milestones (summary)

* **M2** — richer inspect/report surfaces for signed media
* **M3** — preservation behavior through VMS/NVR paths
* **M4** — passive live RTSP verification
* **M5** — ARM64 / edge deployment (e.g. RK3588 / Jetson class targets)
* **M6** — integration layer for adjacent systems
* **M7** — optional gateway for legacy signing environments
* **M8** — native Media Signing camera path

Milestone titles may be refined as design work proceeds; the progression above is
the agreed high-level picture.
