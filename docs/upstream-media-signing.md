# Upstream: ONVIF Media Signing Framework

## Pin

| Field | Value |
| --- | --- |
| Upstream | https://github.com/onvif/media-signing-framework |
| **Approved pin** | tag **`r25.12.6`** |
| Commit (peeled) | `cf7785ab993c18d921094e8e505c2c34a1350f28` |
| **media-signing-framework project/release version** at pin | `25.12.6` (from upstream `project(... version : ...)` / `ONVIF_MEDIA_SIGNING_VERSION`) |
| License | MIT (Copyright (c) 2025 ONVIF) |
| Fetch helper | `./scripts/fetch-upstream.sh` |

### Version terminology

Do **not** confuse:

* **media-signing-framework project/release version** — the ONVIF library/release identifier (for example `25.12.6` at tag `r25.12.6`, or unreleased `26.6.1` on current upstream `master`).
* **Meson build-system tool version** — the host `meson` program used to configure the build (for example `1.3.x` from Ubuntu packages or `1.12.x` from a project tool venv). This is independent of the upstream project version.

### Pin decision

* **No official release tag newer than `r25.12.6`** existed when this pin was chosen.
* Upstream `master` at that time (`b82fddc67803a2a625c9131da1ed9d83995f8e45`) declared unreleased project version **`26.6.1`** (not a Git tag).
* Material unreleased change on that `HEAD` vs `r25.12.6`: PR #237 adjusts validator authenticity when arbitrary TLV data appears together with other tags; plus docs/test-file/CI housekeeping. Public C API headers were unchanged.
* Prefer a **stable official release/tag** for M0 reproducibility. Unreleased `26.6.1` was **not** required for the planned M0 sign/verify/tamper path.
* **Rejected alternatives:** pin to untagged `HEAD` / `26.6.1` solely because it is newer; pin to older `v1.0.2` (superseded by `r25.12.x` calendar releases).

## Build system

* Build tools: **Meson** (tool, `>= 0.49`) + **Ninja**
* Mandatory: OpenSSL >= 3.0 (`pkg-config openssl`)
* Optional: libcheck (unit tests), GLib (threaded signing plugin), GStreamer (example apps)
* Default signing plugin: **unthreaded**
* Example apps (`-Dsigner` / `-Dvalidator` / `-Dbuild_all_apps`) use a **separate**
  Meson build directory from unit tests

## Public C API (headers under `lib/src/includes/`)

| Header | Role |
| --- | --- |
| `onvif_media_signing_common.h` | Session create/free/reset, codecs, return codes, version |
| `onvif_media_signing_signer.h` | Key pair, SEI pull, add NAL for signing, configuration setters |
| `onvif_media_signing_validator.h` | Trusted CA, add NAL + authenticate, authenticity report |
| `onvif_media_signing_plugin.h` | Signing plugin ABI |
| `onvif_media_signing_helpers.h` | Test key/cert helpers |

### Signer lifecycle (summary)

1. `onvif_media_signing_create(OMS_CODEC_H264|OMS_CODEC_H265)`
2. `onvif_media_signing_set_signing_key_pair` (manufacturer PEM key + cert chain; mandatory)
3. Optional configuration setters (hash, vendor info, EPB, frequency, certificate SEI, …)
4. Per NAL: `onvif_media_signing_get_sei` then `onvif_media_signing_add_nalu_for_signing`
5. Optional `onvif_media_signing_set_end_of_stream`
6. `onvif_media_signing_free`

Timestamps are UTC-based 100-nanosecond intervals since 1601-01-01.

### Validator lifecycle (summary)

1. `onvif_media_signing_create(codec)`
2. `onvif_media_signing_set_trusted_certificate` (PEM CA; before stream start)
3. Per NAL: `onvif_media_signing_add_nalu_and_authenticate` → optional report
4. Inspect `MediaSigningAuthenticityResult` / `MediaSigningProvenanceResult`
5. Free report and session

Important authenticity values include `OMS_NOT_SIGNED`,
`OMS_AUTHENTICITY_NOT_FEASIBLE`, `OMS_AUTHENTICITY_NOT_OK`,
`OMS_AUTHENTICITY_OK_WITH_MISSING_INFO`, `OMS_AUTHENTICITY_OK`,
`OMS_AUTHENTICITY_VERSION_MISMATCH`.

## Example programs

* `examples/apps/signer` — GStreamer element that signs MP4/MKV H.26x files
* `examples/apps/validator` — GstAppSink validator; writes `validation_results.txt`
* Fixtures: `examples/test-files/test_{h264,h265}.mp4`, signed counterparts, `ca.pem`

Typical commands (after apps install to a local prefix):

```bash
export GST_PLUGIN_PATH=/path/to/prefix
./prefix/bin/signer -c h264 examples/test-files/test_h264.mp4
./prefix/bin/validator -b -C examples/test-files/ca.pem -c h264 signed_test_h264.mp4
```

## Tests

Unit tests under `tests/check/` (libcheck). Do not configure apps and unit tests in
the same Meson build folder.

## Nanexus policy

Do not vendor upstream sources into this repository unless a later decision requires
it and licensing/provenance are documented. Prefer pin + fetch / dependency integration.
