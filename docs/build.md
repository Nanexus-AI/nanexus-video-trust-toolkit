# Building Nanexus Video Trust Toolkit (M1 in development)

This repository provides a C++20 Trust Core and an in-development `video-trust`
CLI. Current CLI capability: **Annex-B `verify`, `sign`, and `tamper`** for
H.264/H.265 elementary streams. There is no stable release.

## Requirements

* Linux x86_64 (Ubuntu 24.04 baseline)
* C++20 compiler (GCC 13 tested)
* Meson (>= 0.61) and Ninja
* OpenSSL 3.x (`pkg-config openssl`)
* ffmpeg (optional; used by fixture generation for tests)
* Network access once to fetch the pinned ONVIF framework

## ONVIF dependency model

```bash
./scripts/fetch-upstream.sh
./scripts/build-upstream.sh          # installs to .oms-prefix by default
```

Pin: tag `r25.12.6` / commit `cf7785ab993c18d921094e8e505c2c34a1350f28`, unthreaded plugin.

## Configure, build, test, install

```bash
meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
meson install -C build/nanexus
```

## Example: sign → tamper → verify

```bash
video-trust sign \
  --codec h264 \
  --key signer-key.pem \
  --cert signer-chain.pem \
  -o signed.h264 \
  input.h264

video-trust tamper \
  --codec h264 \
  --operation corrupt-vcl \
  -o tampered.h264 \
  signed.h264

video-trust verify --codec h264 --ca ca.pem tampered.h264
```

Tamper operations intentionally create controlled test material. They are not a
general media editor. M1 signing does **not** establish camera/source authenticity.

Operations: `corrupt-vcl`, `strip-signing-sei`, `truncate` (optional `--count N`).

Contracts: [`m1-contracts.md`](m1-contracts.md).
