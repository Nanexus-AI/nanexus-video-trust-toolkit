# Nanexus Video Trust Toolkit v0.1.0

> Copy for the GitHub Release body. Do not confuse with product history in
> `CHANGELOG.md`. This file is the approved Release text for tag `v0.1.0`.

## What this release is

First **developer** release of the Nanexus Video Trust Toolkit file-based Media
Signing reference lab. Experimental / pre-release software for integrators and
researchers.

## Highlights

* Annex-B H.264 and H.265 signing (`video-trust sign`)
* Verification with structured multi-axis trust results (`video-trust verify`)
* Human-readable and JSON output (`schema_version` `"0.1"`)
* Deterministic tamper testing (`corrupt-vcl`, `strip-signing-sei`, `truncate`)
* Pinned official ONVIF Media Signing Framework (`r25.12.6`, unthreaded plugin)
* Meson/Ninja build; Ubuntu 24.04 x86_64 GCC CI

## Quick example

```bash
video-trust sign --codec h264 --key signer.key.pem --cert signer-chain.pem \
  -o signed.h264 unsigned.h264
video-trust verify --codec h264 --ca ca.pem signed.h264
video-trust tamper --codec h264 --operation corrupt-vcl -o bad.h264 signed.h264
video-trust verify --codec h264 --ca ca.pem bad.h264
```

## Supported environment

* Ubuntu 24.04 x86_64
* GCC (C++20)
* Meson / Ninja
* OpenSSL 3.x

## Important trust boundary

Valid signatures and certificate checks do **not** prove that the depicted event
is real or that the source camera identity is established. In this release,
`source_authenticity` remains `not_established` for reference-lab material.

## Limitations

Not included in v0.1.0:

* MP4 / MKV containers
* RTSP / live input
* VMS / NVR integration
* ARM64 packaging
* Production PKI / enrollment
* ONVIF certification or conformance claims
* Guaranteed stable public SDK

## Documentation

* [README](../README.md)
* [Build](build.md)
* [Usage](usage.md)
* [Contracts](m1-contracts.md)
* [JSON v0.1](json-v0.1.md)
* [Changelog](../CHANGELOG.md)

## Assets

Source archives only (automatic GitHub source tarball/zip). No attached binaries,
packages, fixtures, or key material.
