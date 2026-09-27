# M1 contracts (foundation freeze)

These contracts apply to the upcoming `video-trust` CLI. The CLI itself is not
implemented in the foundation slice; types and unit tests already enforce the
result/exit semantics below.

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
* certificate / trust-anchor status
* source authenticity (defaults to **not established** for reference-lab material)

Valid signature ≠ trusted certificate ≠ known signer ≠ established source authenticity.

## Key terminology

Use general signing terms: signing private key, signer certificate, certificate
chain, trust anchor / CA. Device/manufacturer identity is outside the M1
reference-lab trust boundary.
