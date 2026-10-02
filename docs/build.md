# Building Nanexus Video Trust Toolkit

Experimental / pre-release developer reference lab. The `v0.1.0` release
contains Annex-B `verify` / `sign` / `tamper` for H.264 and H.265; current
`main` also contains `inspect`, `compare-preservation`, and bounded
`verify-live`; M4 is complete locally but remains unpublished pending a
coordinator decision and is not a new tagged release.

## Supported platform

* Linux x86_64
* Ubuntu 24.04 baseline
* **GCC** C++20 is the current supported compiler baseline (Clang is not part
  of required CI)

## Packages (Ubuntu)

```bash
sudo apt-get install -y build-essential pkg-config meson ninja-build \
  libssl-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good ffmpeg git ca-certificates python3-jsonschema
```

* **ffmpeg** is used by test fixture generation (not required to run a
  pre-built `video-trust` binary against your own Annex-B files).
* **python3-jsonschema** is used by the inspection and preservation JSON Schema
  contract tests.
  It is a test/development dependency and is not required to run a pre-built
  `video-trust` binary against media.
* **OpenSSL 3.x** via `libssl-dev` / `pkg-config openssl`.
* `verify-live` dynamically links the LGPL GStreamer core/app libraries and
  uses the LGPL `rtspsrc` and RTP depayloader plugins from plugins-good. No
  GStreamer binary is vendored and no GPL-only plugin is required.

## ONVIF dependency

The official [Media Signing Framework](https://github.com/onvif/media-signing-framework)
is fetched and installed into a **local prefix** (gitignored). It is not vendored
into git by default.

```text
tag:    r25.12.6
commit: cf7785ab993c18d921094e8e505c2c34a1350f28
plugin: unthreaded
```

```bash
./scripts/fetch-upstream.sh
./scripts/build-upstream.sh
# default install: $PWD/.oms-prefix
```

Optional: `OMS_PREFIX=/some/prefix ./scripts/build-upstream.sh`

## Configure / build / test / install

```bash
meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
meson install -C build/nanexus   # installs video-trust (typically to /usr/local/bin)
```

Use `-Dlive_rtsp=enabled` to require live support at configure time or
`-Dlive_rtsp=disabled` for a finite-file-only build. The default `auto` enables
it when both `gstreamer-1.0` and `gstreamer-app-1.0` are available.

`oms_prefix` points Meson at the ONVIF headers and shared library. The link uses
an rpath to that prefix for local runs.

Installed user-facing artifact: **`video-trust`** only (no public SDK headers).

## CLI version

```bash
video-trust --version
```

Version matches the Meson project version (`0.1.0`). JSON verification output
uses a separate `schema_version` of `"0.1"` — that is not the product SemVer.

## Next

* [`usage.md`](usage.md) — command examples
* [`live-matrix.md`](live-matrix.md) — reproducible synthetic RTSP/TCP matrix
* [`m4-frigate-live-case-study.md`](m4-frigate-live-case-study.md) — scoped live RTSP observation
* [`m1-contracts.md`](m1-contracts.md) — exit codes and trust semantics
* [`json-v0.1.md`](json-v0.1.md) — JSON contract
* [`inspection-v0.1.md`](inspection-v0.1.md) — inspection text/JSON contract
* [`preservation-v0.1.md`](preservation-v0.1.md) — preservation text/JSON contract
* [`fixtures.md`](fixtures.md) — test media provenance
