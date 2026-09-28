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
- Experimental Agent interface on `main` (not a release tag): Python capability
  layer, read-only MCP stdio tools `video_trust.verify_file` and
  `video_trust.assess_video_integrity`, and an evaluation harness
- Public docs: `docs/agent-interface.md`, architecture and roadmap notes for M1.5
- Continuous integration runs the Python Agent test suite without live model credentials

### Notes

- Product version remains `0.1.0`. Core JSON schema, Agent contract, and
  capability versions remain `0.1`. The separate inspection JSON schema also
  begins at `0.1`. Evaluation suite/scorer is `0.2`.
- M2 inspection is CLI/domain-facing only. The Agent/MCP tool catalog remains
  `video_trust.verify_file` and `video_trust.assess_video_integrity`.
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
