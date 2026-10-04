# Nanexus Video Trust Toolkit

Vendor-neutral **Video Trust** toolkit for file-based Media Signing workflows,
built on the official ONVIF Media Signing Framework.

The same verification core supports three file-based tasks:

* **Verify** — a compact verdict and trust-axis view, in text or stable M1 JSON 0.1.
* **Inspect** — a richer observation report, in human-readable text or its
  separate inspection JSON 0.1 contract.
* **Compare preservation** — a deterministic before/after assessment of
  verifier-observable Media Signing evidence, correlation, and source coverage.

Verification is also available through an experimental Agent interface:

* **Agent interface** — a typed, read-only capability layer, exposed over MCP stdio.

MCP is the interoperability socket. The product value is the domain result:
typed inputs, deterministic trust axes, structured evidence, and limitations
the Agent does not get to invent. Trust logic is not reimplemented for Agents.

> **Independent open-source project.** Not an official ONVIF product. No ONVIF
> sponsorship, endorsement, certification, or conformance is claimed.

## Project status

```text
Project Status:              Experimental / Pre-release
M0 Feasibility:              Complete
M1 File-based reference lab: Complete — v0.1.0 first developer release
M1.5 Agent interface:        Experimental, on main (not a stable API)
M2 Inspect + Report:         Implemented on current main (not a tagged release)
M3 VMS/NVR preservation:    Complete — published on current main (not a tagged release)
M4 Passive live RTSP:       Complete — published on current main (not a tagged release)
M5 ARM64 / edge deployment: Complete — validated on Jetson Orin Nano (not a tagged release)
Stable production release:   None
```

`v0.1.0` is the first usable **developer** release of the file-based lab
(GitHub Pre-release). Product version metadata is `0.1.0`. M1.5 adds an
experimental Agent interface, and current `main` adds M2 inspection, M3
preservation assessment, bounded M4 live verification, and scoped M5 ARM64
portability. These post-release
changes are not part of `v0.1.0`,
do not create a new tagged release, and do not make the Agent interface a
stable public API.

Version labels are separate: product `v0.1.0`, verify JSON schema `0.1`,
inspection JSON schema `0.1`, Agent contract `0.1`, capability `0.1`, and
evaluation suite/scorer `0.2`.

## What it is

A thin C++20 CLI around the official
[`onvif/media-signing-framework`](https://github.com/onvif/media-signing-framework)
(MIT) for **Annex-B H.264/H.265** elementary streams, plus an experimental
Python capability layer for Agents.

Developer commands:

* `video-trust sign`
* `video-trust verify` (text + `--json`)
* `video-trust inspect` (human-readable report + `--json`)
* `video-trust compare-preservation` (before/after text + JSON 0.1)
* `video-trust verify-live` (bounded one-stream RTSP/TCP text + JSONL 0.1)
* `video-trust tamper` (deterministic test mutations)
* `video-trust-integration` (bounded one-request/one-process finite JSON adapter)

`verify` is the compact automation-facing verdict and axes contract. `inspect`
adds validation counts, raw timestamp observations, and optional vendor
observations around the same verification result. It provides more detail, not
a stronger authenticity claim. See
[`docs/inspection-v0.1.md`](docs/inspection-v0.1.md) and the formal
[`inspection JSON Schema`](docs/schemas/media-signing-inspection-0.1.json).

Agent tools, read-only:

* `video_trust.verify_file` — the verification record (L1)
* `video_trust.compare_preservation` — deterministic before/after preservation assessment (L1)
* `video_trust.assess_video_integrity` — a deterministic reading of that same record (L2)

The Agent interface does not currently expose the richer inspection surface as
an MCP tool.

L2 is another abstraction over one core execution. It is not a better answer
than L1. An Agent uses the level that matches the task. A goal-level
capability (L3) is not part of M1.5.

Capability contracts are written for an explicit caller, including a smaller
local model:

> Design the contract for the weakest model you reasonably want to support;
> recover flexibility for stronger models through capability depth, not ambiguity.

That means typed inputs, enum choices, deterministic outputs, and extra depth
when a stronger caller wants it. It is a design rule, not a benchmark claim.
This repository does not promise that any particular model is supported.

## What a result does not establish

Results keep trust axes separate. There is no single `AUTHENTIC=true`.

`overall` `VALID` means the signature-integrity and completeness rules for
VALID were met. It does **not** by itself mean:

* trusted camera
* source authenticity
* the depicted event is real
* the content was not staged
* the signing certificate is trusted

Distinct axes include Media Signing presence, signature integrity, continuity,
verification completeness, certificate status, and source authenticity.
`source_authenticity` stays `not_established`. A SHA-256 in Agent evidence
identifies bytes this layer read. It is not the ONVIF media signature.

Preservation is another separate result. An after artifact may verify `VALID`
while preservation is `indeterminate`; a valid ordered subset does not prove
full source coverage. `PARTIAL` can result from a pending tail rather than bad
signatures. See [`docs/preservation-v0.1.md`](docs/preservation-v0.1.md).

## What it is not

* A VMS, NVR, camera client, or media player
* A general-purpose Agent, workflow engine, or multi-agent platform
* A replacement for the ONVIF Media Signing Framework
* An ONVIF-certified or conformant product
* A system that decides whether a depicted event is real
* General MP4/MKV input, a production RTSP monitoring service, universal ARM64
  platform compatibility, or production PKI tooling. M4 provides one bounded
  developer/reference-lab RTSP/TCP verification command, not a VMS, recorder,
  or camera-compatibility guarantee. M5 validates native ARM64 portability on
  one exact Jetson configuration; it is not a claim for every ARM64 or Jetson
  system.

Sign, tamper, and other write operations stay on the CLI. The MCP server
does not expose them.

## Developer quick start (Ubuntu 24.04 x86_64)

```bash
sudo apt-get install -y build-essential pkg-config meson ninja-build \
  libssl-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good nlohmann-json3-dev ffmpeg git ca-certificates \
  python3-jsonschema

git clone https://github.com/Nanexus-AI/nanexus-video-trust-toolkit.git
cd nanexus-video-trust-toolkit

./scripts/fetch-upstream.sh
./scripts/build-upstream.sh          # installs to .oms-prefix/

meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
```

Example lab workflow (synthetic keys and media you provide):

```bash
video-trust sign --codec h264 --key signer.key.pem --cert signer-chain.pem \
  -o signed.h264 unsigned.h264
video-trust verify --codec h264 --ca ca.pem signed.h264
video-trust inspect --codec h264 --ca ca.pem signed.h264
video-trust inspect --codec h264 --ca ca.pem --json signed.h264
video-trust compare-preservation --codec h264 --before-ca ca.pem \
  --after-ca ca.pem --json signed.h264 exported.h264
video-trust tamper --codec h264 --operation corrupt-vcl -o bad.h264 signed.h264
video-trust verify --codec h264 --ca ca.pem bad.h264
video-trust verify-live --codec h264 --duration 30 --ca ca.pem --jsonl \
  rtsp://camera.example/stream
```

More detail: [`docs/build.md`](docs/build.md), [`docs/usage.md`](docs/usage.md),
[`docs/m1-contracts.md`](docs/m1-contracts.md), [`docs/json-v0.1.md`](docs/json-v0.1.md),
and [`docs/inspection-v0.1.md`](docs/inspection-v0.1.md).

## Agent quick start

Requires Python 3.12+ and [uv](https://docs.astral.sh/uv/). From a built tree:

```bash
cd agent
uv sync --frozen

export NANEXUS_VIDEO_TRUST="$PWD/../build/nanexus/video-trust"
export NANEXUS_ALLOWED_ROOTS=/path/to/media
uv run --python 3.12 python -m nanexus_video_trust_agent.mcp_server
```

The process speaks MCP on stdin/stdout. It does not open a network port.
Empty `NANEXUS_ALLOWED_ROOTS` refuses startup. Optional
`NANEXUS_VERIFY_TIMEOUT` is seconds, greater than 0 and at most 300
(default 60).

Full contract, safety rules, and examples: [`docs/agent-interface.md`](docs/agent-interface.md).

## Architecture

```text
ONVIF Media Signing Framework
            │
            ▼
Verification + inspection domain mapping
            │
            ├────────► VerificationResult ─────► verify text / JSON 0.1
            │
            └────────► InspectionResult ───────► inspect text / JSON 0.1
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

The C++ core does not depend on MCP. The capability layer does not depend on
an Agent runtime. Planning, conversation, and orchestration stay outside this
repository. See [`docs/architecture.md`](docs/architecture.md).

## Platform

* Primary development/reference platform: Ubuntu 24.04 x86_64
* ARM64 portability validated on an NVIDIA Jetson Orin Nano Developer Kit
  running Ubuntu 24.04; see [`docs/build.md`](docs/build.md) for the exact
  tested stack and claim boundary
* C++20 (GCC required for M1; Clang not in required CI)
* Meson + Ninja + OpenSSL 3.x
* Official ONVIF framework pin `r25.12.6` (unthreaded signing plugin)
* Agent layer: Python ≥ 3.12, Pydantic, and the official MCP Python SDK

## Roadmap

`v0.1.0` is the published file-based reference lab. Current `main` also contains
the experimental M1.5 Agent interface, M2 inspect/report work, completed M3
preservation work, completed M4 passive RTSP work, and scoped M5 ARM64
portability published on current `main`; none is a new tagged release or GitHub
release. Later milestones continue with integration work. Each milestone also
reviews whether a new domain capability should appear to an Agent; M2 and M5
did not add Agent exposure, while M3 adds the preservation comparison primitive.

See [`docs/roadmap.md`](docs/roadmap.md).

## Issues

Bug reports, interoperability notes, fixture problems, and feature
suggestions are welcome. Early direction stays with the maintainers; this
README is not a call for broad redesign proposals.

## License

Nanexus code, including the Agent package: **Apache-2.0** (`LICENSE`, `NOTICE`).
Upstream Media Signing Framework: **MIT** (fetched locally; not vendored by default).
The Agent package depends on Pydantic and the MCP Python SDK, both MIT.

## Documentation

* [`docs/build.md`](docs/build.md) — build / install
* [`docs/usage.md`](docs/usage.md) — CLI examples and workflows
* [`docs/agent-interface.md`](docs/agent-interface.md) — experimental Agent / MCP interface
* [`docs/architecture.md`](docs/architecture.md) — core and Agent layering
* [`docs/m1-contracts.md`](docs/m1-contracts.md) — exit codes, trust axes, tamper semantics
* [`docs/json-v0.1.md`](docs/json-v0.1.md) — JSON `schema_version` 0.1
* [`docs/inspection-v0.1.md`](docs/inspection-v0.1.md) — inspect text/JSON contract
* [`docs/preservation-v0.1.md`](docs/preservation-v0.1.md) — preservation text/JSON contract
* [`docs/live-contract-v0.1.md`](docs/live-contract-v0.1.md) — live JSONL contract
* [`docs/live-matrix.md`](docs/live-matrix.md) — synthetic live validation matrix
* [`docs/m4-frigate-live-case-study.md`](docs/m4-frigate-live-case-study.md) — scoped Frigate/go2rtc live result
* [`docs/m3-frigate-case-study.md`](docs/m3-frigate-case-study.md) — sanitized, configuration-scoped M3 field result
* [`docs/schemas/media-signing-inspection-0.1.json`](docs/schemas/media-signing-inspection-0.1.json) — formal inspection JSON Schema
* [`docs/schemas/media-signing-preservation-assessment-0.1.json`](docs/schemas/media-signing-preservation-assessment-0.1.json) — formal preservation JSON Schema
* [`docs/fixtures.md`](docs/fixtures.md) — test fixture provenance
* [`docs/release-notes-v0.1.0.md`](docs/release-notes-v0.1.0.md) — frozen GitHub Release text
* [`docs/roadmap.md`](docs/roadmap.md) — milestones
* [`CHANGELOG.md`](CHANGELOG.md)
