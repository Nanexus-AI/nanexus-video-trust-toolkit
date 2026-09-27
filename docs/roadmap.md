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
| **M1** | File-Based Reference Lab | **v0.1.0 package frozen** (tag/Release pending) |
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

## M1 — File-Based Reference Lab (v0.1.0 package)

First intended usable developer release (`v0.1.0`): file-based reference lab with
`sign` / `verify` / `tamper`, H.264/H.265 Annex-B, structured results, CLI + JSON,
deterministic fixtures, and CI. Release package is frozen; annotated tag and
GitHub Release await final human approval.

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
