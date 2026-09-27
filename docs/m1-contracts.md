# M1 contracts

Contracts for the in-development `video-trust` CLI. M1 is **not** complete and
there is **no** stable `v0.1.0` release yet. This milestone currently exposes
**Annex-B verify only** (`sign` / `tamper` are not implemented).

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Operation successful / verification positive |
| 1 | Verification completed with a negative integrity/authenticity result |
| 2 | Invalid CLI, unsupported/malformed input, or parse/input error |
| 3 | Runtime/internal/upstream failure |
| 4 | Verification completed but media is unsigned or not verifiable |

PARTIAL guidance:

* integrity evidence of failure → `1`
* insufficient evidence / unverifiable → `4`

## Trust axes (no single boolean)

Verification results separate:

* media signing presence
* signature integrity (authenticity)
* continuity
* verification completeness
* certificate / signing-key provenance (trust-anchor axis)
* source authenticity (**always `not_established` in M1**)

Valid signature ≠ trusted certificate ≠ known signer ≠ established source authenticity.

### Source authenticity decision

Upstream `OMS_PROVENANCE_*` validates the **signing public key / certificate
chain against a trust anchor**. That is certificate-axis evidence, not camera
or device source identity. Mapping provenance into a `SourceAuthenticity::ProvenanceOk`
(or similar) state would overclaim.

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

## Stdout / stderr

* **stdout**: human-readable result, or JSON only when `--json`
* **stderr**: usage errors, malformed input, runtime/upstream failures

## Key terminology

Use general signing terms: signing private key, signer certificate, certificate
chain, trust anchor / CA. Device/manufacturer identity is outside the M1
reference-lab trust boundary.
