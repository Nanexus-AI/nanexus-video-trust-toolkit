# M1 contracts

Contracts for the `video-trust` CLI in the M1 / `0.1.0` developer release line.
Product SemVer `0.1.0` is distinct from JSON `schema_version` `"0.1"`.

## Exit codes

### Shared / verify

| Code | Meaning |
| --- | --- |
| 0 | Operation successful / verification positive |
| 1 | Verification completed with a negative integrity/authenticity result |
| 2 | Invalid CLI, unsupported/malformed input, or parse/input error |
| 3 | Runtime/internal/upstream failure |
| 4 | Verification completed but media is unsigned, incomplete, or not verifiable |

PARTIAL guidance (verify):

* integrity evidence of failure → `1`
* insufficient evidence / unverifiable / incomplete stream → `4`

Truncated signed streams commonly report integrity `ok` with completeness
`incomplete` → overall `PARTIAL`, exit `4`.

### Sign-specific

| Code | Meaning |
| --- | --- |
| 0 | Signing completed successfully |
| 2 | CLI / input / path / key / certificate / malformed-media / overwrite error |
| 3 | Internal / runtime / upstream signing failure |

### Tamper-specific

| Code | Meaning |
| --- | --- |
| 0 | Tamper transformation completed successfully |
| 2 | CLI / input / path / codec / operation / malformed / no suitable target / overwrite |
| 3 | Internal / runtime failure |

Exit codes `1` and `4` are verification-specific and are **not** used by `sign` or `tamper`.

## Trust axes (no single boolean)

Verification results separate:

* media signing presence
* signature integrity (authenticity)
* continuity
* verification completeness
* certificate / signing-key provenance (trust-anchor axis)
* source authenticity (**always `not_established` in M1**)

Valid signature ≠ trusted certificate ≠ known signer ≠ established source authenticity.

M1 `sign` is reference-lab signing. It does **not** establish that media was
signed by a camera sensor or manufacturer-controlled hardware.

### Source authenticity decision

Upstream `OMS_PROVENANCE_*` validates the **signing public key / certificate
chain against a trust anchor**. That is certificate-axis evidence, not camera
or device source identity.

Therefore M1:

* maps upstream provenance → `CertificateStatus`
* keeps `SourceAuthenticity` as the single value `not_established`
* never prints “this video is authentic / real”

### Overall state (convenience only)

`Overall` / JSON `overall` is derived from integrity/continuity/completeness:

| Overall | Rule (summary) |
| --- | --- |
| `INVALID` | signature integrity negative, or continuity broken |
| `UNSIGNED` | media signing not detected / integrity not applicable |
| `NOT_VERIFIABLE` | integrity or completeness not feasible |
| `PARTIAL` | ok-with-missing-info, incomplete, or continuity missing info |
| `VALID` | integrity positive and none of the above |

`VALID` is **not** derived from certificate/source trust alone.

## Implementation notes

* Plugin: **unthreaded** ONVIF signing plugin (M1 default; not a permanent product guarantee).
* Generated signing SEIs use upstream **emulation-prevention bytes** (`sei_epb=true`).
* `--cert` expects a PEM **certificate chain** (leaf … trust anchor).
* Tamper operations: `corrupt-vcl`, `strip-signing-sei`, `truncate` (NAL-boundary; `--count N`).
* ONVIF signing SEIs are identified by SEI payload type `5` (user_data_unregistered) plus the ONVIF Media Signing UUID from upstream `kUuidMediaSigning`. Unrelated SEIs are preserved.
* `corrupt-vcl` mutates the first suitable VCL **after** an ONVIF signing SEI when available (falls back to the first VCL). Mutating media only before signing metadata is present can yield upstream `not_feasible` instead of a clean integrity failure.

## Observed M1 tamper → verify results

Both H.264 and H.265 (with matching test CA on verify):

| Operation | overall | integrity | completeness | exit |
| --- | --- | --- | --- | --- |
| `corrupt-vcl` | `INVALID` | `not_ok` | `complete` | 1 |
| `strip-signing-sei` | `UNSIGNED` | `not_applicable` | `incomplete` | 4 |
| `truncate` (`--count` ≥ 1) | `PARTIAL` | `ok` | `incomplete` | 4 |

Tamper success means the transformation was applied — not that verification failed.
Always run `video-trust verify` separately.

## Stdout / stderr

* **stdout**: human-readable verify result / JSON (`--json`); concise sign/tamper success text (unless `--quiet`)
* **stderr**: usage errors, malformed input, runtime/upstream failures

## Key terminology

Use general signing terms: signing private key, signer certificate, certificate
chain, trust anchor / CA. Device/manufacturer identity is outside the M1
reference-lab trust boundary.
