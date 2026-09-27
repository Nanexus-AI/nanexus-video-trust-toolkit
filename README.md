# Nanexus Video Trust Toolkit

Vendor-neutral **Video Trust** toolkit.

**Status:** pre-M0 bootstrap. Not a product release. No `v0.1.0` tag.

## Focus

The first technical focus is **ONVIF Media Signing**, using the official
[`onvif/media-signing-framework`](https://github.com/onvif/media-signing-framework)
as the trusted signing and validation core.

## Milestones

| Milestone | Intent |
| --- | --- |
| **M0** | Feasibility spike: can the official Media Signing framework build and sign/verify H.264 and H.265, and detect controlled tampering? |
| **M1** | First intended release: file-based reference lab (`sign` / `verify` / `tamper`), H.264/H.265, structured results, CLI + JSON, deterministic fixtures, CI |

M0 is **not** a product release. M1 is out of scope until M0 gates pass.

## License

Original Nanexus project code: **Apache License 2.0** (see `LICENSE` and `NOTICE`).

Upstream ONVIF Media Signing framework: **MIT** (fetched separately; see `third_party/` and `docs/upstream-media-signing.md`).

## Build direction

* Language: C++20 (Nanexus layer; not started in M0)
* Platform: Linux x86_64 / Ubuntu 24.04
* Build: Meson + Ninja
* Crypto/signing core: official ONVIF Media Signing (C API), not a reimplementation

## Documentation

* [`docs/architecture.md`](docs/architecture.md) — high-level scope and layering
* [`docs/upstream-media-signing.md`](docs/upstream-media-signing.md) — upstream pin and API notes
* [`docs/m0-feasibility.md`](docs/m0-feasibility.md) — sanitized M0 plan and gates

## Upstream fetch

```bash
./scripts/fetch-upstream.sh
```

See `scripts/fetch-upstream.sh` for the pinned tag/commit.
