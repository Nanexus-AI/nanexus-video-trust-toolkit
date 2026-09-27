# Nanexus Video Trust Toolkit

Vendor-neutral **Video Trust** toolkit for file-based Media Signing workflows,
built on the official ONVIF Media Signing Framework.

> **Independent open-source project.** Not an official ONVIF product. No ONVIF
> sponsorship, endorsement, certification, or conformance is claimed.

## Project status

```text
Project Status:              Experimental / Pre-release
M0 Feasibility:              Complete
M1 File-based reference lab: Nearing v0.1.0 (release candidate; not tagged)
Stable end-user release:     None
```

M1 provides a usable developer reference lab. **`v0.1.0` is not released until
a tag and GitHub Release exist.**

## What it is

A thin C++20 CLI around the official
[`onvif/media-signing-framework`](https://github.com/onvif/media-signing-framework)
(MIT) for **Annex-B H.264/H.265** elementary streams:

* `video-trust sign`
* `video-trust verify` (text + `--json`)
* `video-trust tamper` (deterministic test mutations)

Results separate trust axes. A valid signature is **not** proof that video
depicts reality, came from a trusted camera, or is legal evidence.

## What it is not

* VMS / NVR / camera client / media player
* ONVIF-certified or conformant product
* Proof of “real video” or established source authenticity
* MP4/MKV, RTSP, live, ARM64, or production PKI tooling (future milestones)

## Quick start (Ubuntu 24.04 x86_64)

```bash
sudo apt-get install -y build-essential pkg-config meson ninja-build \
  libssl-dev ffmpeg git ca-certificates

git clone https://github.com/Nanexus-AI/nanexus-video-trust-toolkit.git
cd nanexus-video-trust-toolkit

./scripts/fetch-upstream.sh
./scripts/build-upstream.sh          # installs to .oms-prefix/

meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
```

Example lab workflow (synthetic keys/media you provide):

```bash
video-trust sign --codec h264 --key signer.key.pem --cert signer-chain.pem \
  -o signed.h264 unsigned.h264
video-trust verify --codec h264 --ca ca.pem signed.h264
video-trust tamper --codec h264 --operation corrupt-vcl -o bad.h264 signed.h264
video-trust verify --codec h264 --ca ca.pem bad.h264
```

More detail: [`docs/build.md`](docs/build.md), [`docs/usage.md`](docs/usage.md),
[`docs/m1-contracts.md`](docs/m1-contracts.md), [`docs/json-v0.1.md`](docs/json-v0.1.md).

## Platform

* Linux x86_64 / Ubuntu 24.04
* C++20 (GCC required for M1; Clang not in required CI)
* Meson + Ninja + OpenSSL 3.x
* Official ONVIF framework pin `r25.12.6` (unthreaded signing plugin)

## License

Nanexus code: **Apache-2.0** (`LICENSE`, `NOTICE`).  
Upstream Media Signing Framework: **MIT** (fetched locally; not vendored by default).

## Documentation

* [`docs/build.md`](docs/build.md) — build / install
* [`docs/usage.md`](docs/usage.md) — CLI examples and workflows
* [`docs/m1-contracts.md`](docs/m1-contracts.md) — exit codes, trust axes, tamper semantics
* [`docs/json-v0.1.md`](docs/json-v0.1.md) — JSON `schema_version` 0.1
* [`docs/fixtures.md`](docs/fixtures.md) — test fixture provenance
* [`docs/roadmap.md`](docs/roadmap.md) — milestones
* [`CHANGELOG.md`](CHANGELOG.md)
