# Building the Nanexus core (M1 foundation)

This repository currently provides the **C++20 Trust Core** foundation for the
upcoming `video-trust` reference lab (sign / verify / tamper are not implemented yet).

## Requirements

* Linux x86_64 (Ubuntu 24.04 baseline)
* C++20 compiler (GCC 13 tested)
* Meson (>= 0.61) and Ninja
* OpenSSL 3.x (`pkg-config openssl`)
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

Nanexus then links against that prefix:

```bash
meson setup build/nanexus -Doms_prefix="$PWD/.oms-prefix"
meson compile -C build/nanexus
meson test -C build/nanexus
```

See also [`upstream-media-signing.md`](upstream-media-signing.md) and
[`m1-contracts.md`](m1-contracts.md).
