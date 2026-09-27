# JSON verification contract (`schema_version` 0.1)

Emitted by `video-trust verify --json`. Stdout is JSON only; diagnostics go to
stderr. Field names are stable `snake_case`. There is **no** boolean
`authentic` field.

Formal JSON Schema is deferred for M1; this document is the contract.

## Top-level object

| Field | Type | Meaning |
| --- | --- | --- |
| `schema_version` | string | Always `"0.1"` for this release line |
| `codec` | string enum | `h264` \| `h265` |
| `media_signing` | object | `{ "present": true\|false }` |
| `signature_integrity` | string enum | integrity axis |
| `continuity` | string enum | continuity axis |
| `verification_completeness` | string enum | completeness axis |
| `certificate_status` | string enum | signing-key / CA axis |
| `source_authenticity` | string enum | always `not_established` in M1 |
| `public_key_has_changed` | bool | upstream public-key change flag |
| `overall` | string enum | convenience summary (not a replacement for axes) |
| `vendor` | object\|null | optional `{ "manufacturer": "..." }` |
| `versions` | object | `{ "signing": string\|null, "validation": string\|null }` |
| `findings` | array | `{ "code": string, "message": string }` |

## Enumerations

### `signature_integrity`

`not_applicable` · `not_feasible` · `ok` · `ok_with_missing_info` · `not_ok` · `version_mismatch`

### `continuity`

`not_applicable` · `intact` · `missing_info` · `broken`

### `verification_completeness`

`complete` · `incomplete` · `not_feasible`

### `certificate_status`

`not_provided` · `not_feasible` · `not_ok` · `ok` · `feasible_without_trusted`

### `source_authenticity`

`not_established` only (M1)

### `overall`

`VALID` · `INVALID` · `UNSIGNED` · `NOT_VERIFIABLE` · `PARTIAL`

Derivation rules: [`m1-contracts.md`](m1-contracts.md).

## Semantics notes

* `certificate_status: ok` means signing-key provenance against a trust anchor,
  **not** established camera/source authenticity.
* `overall: VALID` requires positive integrity and does **not** require
  certificate or source trust.
* Nullable strings use JSON `null` (for example missing signing version after
  strip-signing-sei).

## Example (signed valid, abbreviated)

```json
{
  "schema_version": "0.1",
  "codec": "h264",
  "media_signing": { "present": true },
  "signature_integrity": "ok",
  "continuity": "intact",
  "verification_completeness": "complete",
  "certificate_status": "ok",
  "source_authenticity": "not_established",
  "public_key_has_changed": false,
  "overall": "VALID",
  "vendor": null,
  "versions": { "signing": "r25.12.6", "validation": "r25.12.6" },
  "findings": [
    {
      "code": "SIGNING_KEY_PROVENANCE_OK",
      "message": "Signing public key validated against trust anchor; source authenticity not established"
    }
  ]
}
```
