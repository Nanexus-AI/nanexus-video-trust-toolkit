# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
for future releases. **M0 is not a release.**

## [Unreleased]

### Added

- C++20 Trust Core foundation: Meson build, ONVIF adapter, verification result model, unit tests
- Build helpers and docs for pinned media-signing-framework integration (`docs/build.md`)
- Frozen M1 exit-code and trust-axis contracts (`docs/m1-contracts.md`)
- Public roadmap (`docs/roadmap.md`) covering M0–M8 milestone progression
- Initial repository bootstrap for Nanexus Video Trust Toolkit
- Documentation for architecture, upstream Media Signing pin, and M0 feasibility findings
- Helper script to fetch the pinned ONVIF `media-signing-framework` revision

### Changed

- Marked project status Experimental / Pre-release with M0 Complete and M1 / v0.1.0 as next
- Clarified independence from ONVIF (uses Media Signing framework; not an official ONVIF project)
- Updated public M0 feasibility findings with execution results at pin `r25.12.6`
- Clarified media-signing-framework project/release version vs Meson build-tool version in upstream docs
- Documented approved upstream pin `r25.12.6` and first M0 controlled-tamper method
