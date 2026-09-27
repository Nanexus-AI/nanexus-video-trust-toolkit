# Building Nanexus Video Trust Toolkit (M1 in development)

This repository provides a C++20 Trust Core and an in-development `video-trust`
CLI. Current CLI capability: **Annex-B verify** for H.264/H.265 elementary
streams. `sign` and `tamper` are not implemented yet. There is no stable release.

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

## Example: verify Annex-B

```bash
video-trust verify --codec h264 input.h264
video-trust verify --codec h265 --ca ca.pem --json input.h265
```

Exit codes and trust axes: [`m1-contracts.md`](m1-contracts.md).

## Test fixtures

Integration tests generate synthetic Annex-B streams and a test-only PKI via
`scripts/gen-verify-fixtures.sh`. Signed fixtures are produced with the
test helper `oms_sign_annexb` (official ONVIF C API), not a Nanexus sign
product command.

See also [`upstream-media-signing.md`](upstream-media-signing.md).
