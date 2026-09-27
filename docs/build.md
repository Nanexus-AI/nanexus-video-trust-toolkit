# Building Nanexus Video Trust Toolkit (M1 in development)

This repository provides a C++20 Trust Core and an in-development `video-trust`
CLI. Current CLI capability: **Annex-B `verify` and `sign`** for H.264/H.265
elementary streams. `tamper` is not implemented yet. There is no stable release.

## Requirements

* Linux x86_64 (Ubuntu 24.04 baseline)
* C++20 compiler (GCC 13 tested)
* Meson (>= 0.61) and Ninja
* OpenSSL 3.x (`pkg-config openssl`)
* ffmpeg (optional; used by fixture generation for tests)
* Network access once to fetch the pinned ONVIF framework

## ONVIF dependency model

The official [ONVIF Media Signing Framework](https://github.com/onvif/media-signing-framework)
is **not vendored**. It is fetched at the approved pin and installed into a local
prefix (gitignored):

```text
tag    r25.12.6
commit cf7785ab993c18d921094e8e505c2c34a1350f28
plugin unthreaded
```

```bash
./scripts/fetch-upstream.sh
./scripts/build-upstream.sh          # installs to .oms-prefix by default
```

## Configure, build, test, install

```bash
meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
meson install -C build/nanexus   # installs the video-trust binary
```

## Example: sign then verify

```bash
# --cert is a PEM chain: leaf certificate … trust anchor (CA).
video-trust sign \
  --codec h264 \
  --key signer-key.pem \
  --cert signer-chain.pem \
  -o signed.h264 \
  input.h264

video-trust verify --codec h264 --ca ca.pem signed.h264
video-trust verify --codec h265 --ca ca.pem --json signed.h265
```

Use `--force` to overwrite an existing output. Signing is reference-lab only and
does **not** establish camera/source authenticity.

Exit codes and trust axes: [`m1-contracts.md`](m1-contracts.md).

## Test fixtures

Integration tests generate synthetic Annex-B streams and a test-only PKI via
`scripts/gen-verify-fixtures.sh`. Do not treat generated keys as production
identity material.

See also [`upstream-media-signing.md`](upstream-media-signing.md).
