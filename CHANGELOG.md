# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- M2 file-based `video-trust inspect` for Annex-B H.264/H.265, with
  human-readable output by default and a distinct `--json` inspection document
- Inspection JSON contract `media_signing_inspection` / schema version `0.1`,
  plus formal JSON Schema at
  `docs/schemas/media-signing-inspection-0.1.json`
- Typed accumulated/latest validation counts, raw FILETIME-style timestamps,
  and optional vendor observations, without changing M1 verification axes
- M3 bounded H.264/H.265 preservation correlation, assessment contract and
  schema `0.1`, comparison CLI, synthetic transformation matrix, and scoped
  Frigate case study
- M4 bounded one-stream H.264/H.265 RTSP/TCP verification, live JSON Lines
  contract and schema, synthetic validation matrix, and configuration-scoped
  Frigate/go2rtc case study; complete and published on current `main`, without
  a new tag or GitHub release
- M5 architecture-neutral ONVIF library discovery plus native ARM64 build,
  finite/live parity, and bounded stability validation on an NVIDIA Jetson
  Orin Nano Developer Kit running Ubuntu 24.04; this exact-platform result is
  not universal ARM64, Jetson-family, or RK3588 support
- A tracked architecture-neutral hardening patch for pinned ONVIF Media
  Signing Framework `r25.12.6` that rejects the reserved undefined TLV tag
  before decoder dispatch; reproduced and regression-tested on x86_64 and
  ARM64 without changing the upstream version pin
- M6 finite local-process integration contract `0.1`, internal C++ facade, and
  bounded `video-trust-integration` adapter for verify, inspect, and
  preservation comparison; embedded domain documents retain their existing
  `0.1` schemas and meanings
- Experimental Agent interface on `main` (not a release tag): Python capability
  layer, read-only MCP stdio tools `video_trust.verify_file`,
  `video_trust.assess_video_integrity`, and
  `video_trust.compare_preservation`, plus an evaluation harness
- Public docs: `docs/agent-interface.md`, architecture and roadmap notes for M1.5
- Continuous integration runs the Python Agent test suite without live model credentials

### Notes

- Product version remains `0.1.0`. Core JSON schema, Agent contract, and
  capability versions remain `0.1`. The separate inspection JSON schema also
  begins at `0.1`. Evaluation suite/scorer is `0.2`.
- M2 inspection remains CLI/domain-facing only. M3 adds the deterministic
  read-only preservation comparison tool without exposing inspection as a
  separate Agent/MCP capability.
- M4 does not add an Agent/MCP tool. Its live result does not establish source
  identity, prior/full coverage, preservation, or generic camera compatibility.
- M5 does not add an Agent/MCP tool or change trust/schema contracts. Ubuntu
  24.04 x86_64 remains the primary development/reference platform; the local
  ONVIF patch must be reassessed when the pin moves to a future release.
- M6 finite integration does not add an Agent/MCP tool or expose live RTSP.
  Execution failures remain separate from completed trust/domain outcomes.
- Post-`v0.1.0` development continues here after the first developer release.

## [0.1.0] — 2026-09-27

First developer release of the file-based Media Signing reference lab.

### Added

- Annex-B H.264 and H.265 `video-trust sign`, `verify`, and `tamper`
- Structured multi-axis verification results (human-readable text and `--json`)
- JSON verification contract with `schema_version` `"0.1"` (distinct from product SemVer)
- Deterministic tamper operations: `corrupt-vcl`, `strip-signing-sei`, `truncate`
- Integration with the pinned official ONVIF Media Signing Framework (`r25.12.6`,
  unthreaded signing plugin)
- Meson/Ninja build; install of the `video-trust` binary
- Ubuntu 24.04 x86_64 GCC continuous integration
- Documented process exit-code contracts for verify / sign / tamper
- Developer documentation: build, usage, JSON contract, fixture provenance

### Limitations

- Experimental / pre-release; not an ONVIF-certified or conformant product
- Valid signatures and certificate checks do **not** establish camera/source
  authenticity or prove that depicted events are real
- Annex-B elementary streams only (no MP4/MKV, RTSP/live, VMS, ARM64, or
  production PKI tooling in this release)
- Source authenticity remains `not_established` for reference-lab material
