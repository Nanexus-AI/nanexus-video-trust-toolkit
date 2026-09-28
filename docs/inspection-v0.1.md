# Media Signing inspection JSON 0.1

This document defines Nanexus's first machine-readable Media Signing inspection
contract. It reports typed observations from the ONVIF Media Signing validation
pipeline while keeping integrity, continuity, certificate provenance, and source
authenticity as separate concerns.

The CLI renders human-readable inspection text by default:

```bash
video-trust inspect --codec h264 --ca ca.pem signed.h264
```

Pass `--json` for this machine-readable contract:

```bash
video-trust inspect --codec h264 --ca ca.pem --json signed.h264
```

H.265 uses the same forms with `--codec h265`. `--ca` is optional.

The formal schema is
[`schemas/media-signing-inspection-0.1.json`](schemas/media-signing-inspection-0.1.json)
and uses JSON Schema Draft 2020-12.

## Relationship to verify JSON

Inspection JSON is a distinct document with its own version lifecycle. It does
not replace or revise `video-trust verify --json`; the M1 verify contract remains
at schema version `0.1`.

The `verification` object is a copied M1 verification representation captured
from the same result. Its axes and derivation rules retain their M1 meanings.
Inspection observations do not feed back into `overall` or any verification axis.

## Shape

```text
document_type: "media_signing_inspection"
schema_version: "0.1"
verification:
  schema_version
  codec
  media_signing.present
  signature_integrity
  continuity
  verification_completeness
  certificate_status
  source_authenticity
  public_key_has_changed
  overall
  vendor
  versions.signing
  versions.validation
  findings[] { code, message }
accumulated_validation:
  number_of_received_nalus
  number_of_validated_nalus
  number_of_pending_nalus
  number_of_received_frames
  number_of_validated_frames
  number_of_pending_frames
  first_timestamp
  last_timestamp
latest_validation:
  number_of_expected_hashable_nalus
  number_of_received_hashable_nalus
  number_of_pending_hashable_nalus
  start_timestamp
  end_timestamp
vendor:
  manufacturer
  firmware_version
  serial_number
```

All objects are closed with `additionalProperties: false`. This makes misspelled,
unreviewed, and diagnostic fields fail validation instead of being silently
accepted. New public fields therefore require a documented schema revision.

## Unavailable values

Optional scalar observations are always present and use JSON `null` when the
domain model has no value. Zero remains the number `0` and is never converted to
`null`. This policy applies to the three latest-validation hashable-NAL counts
and the three vendor observations.

Upstream explicitly defines negative unavailable/error values for expected and
received hashable-NAL counts. Nanexus also maps a negative pending hashable-NAL
count to unavailable defensively, although the upstream header does not define a
negative sentinel for that field. The public contract exposes only a
non-negative integer or `null`; it does not claim an upstream meaning for every
possible negative pending value.

The embedded M1 verification representation retains its existing rules,
including nullable `vendor` and nullable version strings. A present empty
string is preserved as `""` rather than converted to `null`; in human output it
renders as `(empty)`, while an unavailable optional renders as `unavailable`.

## Timestamps

Timestamp fields are signed JSON integers containing the authoritative raw
upstream value: 100-nanosecond intervals since 1601-01-01 UTC (FILETIME units).
No ISO-8601 value is synthesized. The upstream report does not provide a
separate availability flag, so `0` is preserved as `0` and must not be
interpreted by this contract as unavailable. Latest-validation timestamps
describe the upstream partial-GOP validation span; older recordings may report
equal start and end values.

## Trust limits

Vendor manufacturer, firmware version, and serial number are observations, not
proof of device identity, trusted-camera status, or source authenticity.
Certificate status describes signing-key certificate provenance only. It does
not establish that the depicted event is real, unstaged, or captured by a
particular trusted camera. In schema 0.1, `source_authenticity` remains
`not_established`.

The contract intentionally excludes upstream `validation_str`, `nalu_str`, and
the combined `authenticity_and_provenance` value. It exposes no `authentic`,
`trusted_camera`, or `real_event` shortcut.

## Compatibility

Consumers should select this document by both `document_type` and
`schema_version`. Renames, removals, changed meanings, or changed requiredness
require a new inspection schema version. Product versions, verify schema
versions, and inspection schema versions are independent.
