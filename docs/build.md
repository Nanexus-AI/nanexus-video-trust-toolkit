# Building Nanexus Video Trust Toolkit

Experimental / pre-release developer reference lab. The `v0.1.0` release
contains Annex-B `verify` / `sign` / `tamper` for H.264 and H.265; current
`main` also contains `inspect`, `compare-preservation`, and bounded
`verify-live`; M4 and the scoped M5 ARM64 portability work are complete and
published on current `main`, but neither creates a new tagged release or GitHub
release.

## Validated platforms and scope

* **Primary development/reference platform:** Ubuntu 24.04 x86_64.
* **GCC** C++20 is the current supported compiler baseline (Clang is not part
  of required CI)
* **ARM64 tested configuration:** NVIDIA Jetson Orin Nano Developer Kit,
  `aarch64`, Ubuntu 24.04, kernel `6.8.12-1021-tegra`, GCC 13.3.0, glibc 2.39,
  and GStreamer 1.24.2.

ARM64 portability has been validated on that exact Jetson configuration. This
establishes a native ARM64 build/runtime result under the tested environment;
it does not establish compatibility with every ARM64 board, every Jetson,
RK3588, every distribution, or every package layout. The listed stack records
what was tested rather than imposing all of those exact versions as universal
requirements. No prebuilt ARM64 package or cross-compilation workflow is
provided.

## Packages (Ubuntu)

```bash
sudo apt-get install -y build-essential pkg-config meson ninja-build \
  libssl-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good nlohmann-json3-dev ffmpeg git ca-certificates \
  python3-jsonschema
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
* `video-trust-integration` uses the MIT-licensed, header-only nlohmann JSON
  package at build time. It adds no JSON runtime library and remains independent
  from optional GStreamer/live support.

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

The build helper applies the tracked
`patches/media-signing-framework-r25.12.6-reject-undefined-tlv.patch`
idempotently. It rejects the reserved undefined TLV tag before decoder dispatch
after the malformed-input failure was reproduced on x86_64 and ARM64. The
change is architecture-neutral, does not change the `r25.12.6` pin, and must be
reassessed when adopting a future ONVIF release. See
[`upstream-media-signing.md`](upstream-media-signing.md).

## Configure / build / test / install

```bash
meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
meson install -C build/nanexus   # installs video-trust (typically to /usr/local/bin)
```

For a non-root staged install smoke:

```bash
DESTDIR="$PWD/destdir" meson install -C build/nanexus
OMS_LIBDIR="$(find "$PWD/.oms-prefix" -type f \
  -name 'libmedia-signing-framework.so.*' -printf '%h\n' -quit)"
test -n "$OMS_LIBDIR"
LD_LIBRARY_PATH="$OMS_LIBDIR" destdir/usr/local/bin/video-trust --help
```

Use `-Dlive_rtsp=enabled` to require live support at configure time or
`-Dlive_rtsp=disabled` for a finite-file-only build. The default `auto` enables
it when both `gstreamer-1.0` and `gstreamer-app-1.0` are available.

`oms_prefix` points Meson at the ONVIF headers and shared library. Meson uses
its configured `libdir` plus generic `lib`/`lib64` candidates, avoiding an
x86-specific path. The build-tree binary records a RUNPATH to the selected
ONVIF library directory and runs directly. Meson removes that build RUNPATH
when installing: a staged/non-system install therefore needs an explicit
loader path as shown above, while a system deployment must install the ONVIF
library in a loader-configured location and refresh the loader cache. This is
a known local-prefix deployment limitation, not a relocatable binary-package
guarantee. Container, appliance, and prebuilt-package deployment are outside
the validated scope.

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
