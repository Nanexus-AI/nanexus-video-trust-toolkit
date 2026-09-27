# Nanexus Video Trust Toolkit

Vendor-neutral **Video Trust** toolkit focused on authenticating media integrity using
the official ONVIF Media Signing framework as the signing/validation core.

> **This is an independent open-source project.** It is **not** an official ONVIF
> product, and it does **not** claim ONVIF sponsorship, endorsement, certification,
> or conformance.

## Project status

```text
Project Status:              Experimental / Pre-release
Current milestone:           M0 Feasibility — Complete
Stable end-user release:     None
First intended usable
developer release:           M1 / v0.1.0  (not started)
```

M0 is a **completed feasibility spike**, not a stable product release. There is no
supported end-user installation yet. This repository is currently useful primarily
for **developers, researchers, integrators**, and people following ONVIF Media
Signing work.

## What this project is

Nanexus Video Trust Toolkit aims to become a thin, vendor-neutral C++20 tooling
layer around the official
[`onvif/media-signing-framework`](https://github.com/onvif/media-signing-framework)
(MIT). The Nanexus layer will use that framework for Media Signing cryptography,
wire format, and SEI/hash-chain mechanics — it will **not** reimplement them.

## What M0 proved

M0 demonstrated:

* the pinned official ONVIF Media Signing Framework builds successfully;
* official H.264 signing → official validation succeeds;
* official H.265 signing → official validation succeeds;
* controlled signed-media corruption is detected by the official validator;
* the official C API appears viable as the lower-level foundation for a thin
  C++20 Nanexus trust layer.

M0 does **not** mean ONVIF certification/conformance, production validation,
camera-source authenticity, legal evidence certification, or a finished Nanexus
verifier. Details: [`docs/m0-feasibility.md`](docs/m0-feasibility.md).

## What M0 does not provide yet

* stable Nanexus CLI
* supported end-user installation
* production-ready verification
* RTSP / live verification
* MP4/container workflow as a Nanexus product feature
* VMS/NVR integration
* supported SDK
* ARM64 / RK3588 / Jetson deployment
* C2PA integration
* full PKI management
* formal ONVIF conformance / certification

## Roadmap (high level)

| Milestone | Intent | Status |
| --- | --- | --- |
| **M0** | Feasibility spike | **Complete** |
| **M1** | File-based reference lab (`sign` / `verify` / `tamper`) — first intended usable developer release (`v0.1.0`) | **Next** |
| **M2** | Inspect + report | Planned |
| **M3** | VMS/NVR preservation testing | Planned |
| **M4** | Passive live RTSP verification | Planned |
| **M5** | ARM64 / edge deployment | Planned |
| **M6** | Integration layer | Planned |
| **M7** | Optional legacy signing gateway | Planned |
| **M8** | Native Media Signing camera | Planned |

See [`docs/roadmap.md`](docs/roadmap.md).

## License

Original Nanexus project code: **Apache License 2.0** (see `LICENSE` and `NOTICE`).

Upstream ONVIF Media Signing framework: **MIT** (fetched separately; see
`third_party/` and `docs/upstream-media-signing.md`). Upstream sources are **not**
vendored into this repository by default.

## Build direction

* Language: C++20 (Trust Core foundation started; CLI not yet implemented)
* Platform baseline: Linux x86_64 / Ubuntu 24.04
* Build: Meson + Ninja
* Signing core: official ONVIF Media Signing (C API), unthreaded plugin

See [`docs/build.md`](docs/build.md) for configure/build/test steps.

## Documentation

* [`docs/roadmap.md`](docs/roadmap.md) — milestone progression
* [`docs/architecture.md`](docs/architecture.md) — layering intent
* [`docs/build.md`](docs/build.md) — Meson build and ONVIF pin integration
* [`docs/m1-contracts.md`](docs/m1-contracts.md) — exit codes and trust-result axes
* [`docs/m0-feasibility.md`](docs/m0-feasibility.md) — sanitized M0 findings
* [`docs/upstream-media-signing.md`](docs/upstream-media-signing.md) — upstream pin

## Upstream fetch

```bash
./scripts/fetch-upstream.sh
```

See `scripts/fetch-upstream.sh` for the pinned tag/commit.
