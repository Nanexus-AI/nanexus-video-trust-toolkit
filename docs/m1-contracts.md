# M1 contracts

Contracts for the in-development `video-trust` CLI. M1 is **not** complete and
there is **no** stable `v0.1.0` release yet. This milestone currently exposes
**Annex-B `verify` and `sign`**. `tamper` is not implemented.

## Exit codes

### Shared / verify

| Code | Meaning |
| --- | --- |
| 0 | Operation successful / verification positive |
| 1 | Verification completed with a negative integrity/authenticity result |
| 2 | Invalid CLI, unsupported/malformed input, or parse/input error |
| 3 | Runtime/internal/upstream failure |
| 4 | Verification completed but media is unsigned or not verifiable |

PARTIAL guidance (verify):

* integrity evidence of failure → `1`
* insufficient evidence / unverifiable → `4`

### Sign-specific

| Code | Meaning |
| --- | --- |
| 0 | Signing completed successfully |
| 2 | CLI / input / path / key / certificate / malformed-media / overwrite error |
| 3 | Internal / runtime / upstream signing failure |

Exit codes `1` and `4` are verification-specific and are **not** used by `sign`.

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

## Signing implementation notes

* Plugin: **unthreaded** ONVIF signing plugin (M1 default; not a permanent product guarantee).
* Generated signing SEIs use upstream **emulation-prevention bytes** (`sei_epb=true`) so Annex-B start-code scanning does not split signature payloads.
* `--cert` expects a PEM **certificate chain** (leaf … trust anchor). Upstream removes the anchor before embedding.

## Stdout / stderr

* **stdout**: human-readable verify result / JSON (`--json`); concise sign success text (unless `--quiet`)
* **stderr**: usage errors, malformed input, runtime/upstream failures

## Key terminology

Use general signing terms: signing private key, signer certificate, certificate
chain, trust anchor / CA. Device/manufacturer identity is outside the M1
reference-lab trust boundary.
