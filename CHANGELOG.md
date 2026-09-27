# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Preparing first tagged developer release **`v0.1.0`** (not tagged yet).

### Added

- Annex-B H.264/H.265 `video-trust sign`, `verify`, and `tamper`
- Structured multi-axis verification results (text + `--json`, `schema_version` 0.1)
- Deterministic tamper operations: `corrupt-vcl`, `strip-signing-sei`, `truncate`
- Integration with pinned official ONVIF Media Signing Framework (`r25.12.6`, unthreaded plugin)
- Meson/Ninja build, install of `video-trust`, and Ubuntu x86_64 GCC CI
- Exit-code contracts for verify / sign / tamper
- Developer docs: build, usage, JSON contract, fixtures provenance

### Notes

- Experimental / pre-release; not ONVIF-certified
- Source authenticity remains `not_established` for M1 reference-lab material
- Input is Annex-B elementary stream only (no MP4/MKV/RTSP in v0.1.0)

## [0.1.0] — TBD

Release notes will be published with the `v0.1.0` tag. Until then, see
**Unreleased** above.
