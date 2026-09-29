# Media Signing preservation assessment JSON 0.1

This document defines the machine-readable Nanexus contract for comparing the
verifier-observable ONVIF Media Signing evidence in one supplied before artifact
with one supplied after artifact. It serializes the deterministic preservation
domain; serialization adds no correlation, coverage, preservation, authenticity,
or transformation inference.

The formal schema is
[`schemas/media-signing-preservation-assessment-0.1.json`](schemas/media-signing-preservation-assessment-0.1.json)
and uses JSON Schema Draft 2020-12.

There is no public preservation CLI in this slice. Library consumers render an
already-derived `PreservationAssessment` with `RenderPreservationJson`.

## Identity and lifecycle

Every document has:

```json
{
  "document_type": "media_signing_preservation_assessment",
  "schema_version": "0.1"
}
```

The preservation schema lifecycle is independent from product SemVer, verify
JSON 0.1, and inspection JSON 0.1. This contract neither replaces nor revises
those documents.

## Field tree

```text
document_type
schema_version
before, after
  codec
  byte_size
  sha256 { algorithm, value }
  nal_count
  signing_sei_count
  verification
    media_signing
    signature_integrity
    continuity
    verification_completeness
    certificate_status
    source_authenticity
    public_key_has_changed
    overall
  inspection
    pending_nalus
    pending_frames
    pending_hashable_nalus
transformation
  kind
  label
  pipeline_id
  tool_version
  log_digest
  trust
artifact_identity
  byte_relation
correlation
  quality
  stream_relation
  sequence_equivalent
  after_is_ordered_subsequence
  unique_subsequence_alignment
  reordered
  duplicated_after
  normalized_payload_matches
  unique_before_range { start_nal_index, end_nal_index } | null
  unmatched_before, unmatched_after
    total_count
    sample_count
    samples_truncated
    roles { vcl, sei, signing_sei, parameter_set, other }
  diagnostic_detail_bounded
  signing_metadata
    relation
    before_count
    after_count
    matched_payload_count
    missing_from_after_count
    unmatched_after_count
    correlation_complete
    before_classification_complete
    after_classification_complete
coverage { state }
applicability { media_signing_preservation }
preservation { media_signing_evidence }
transitions
  verification
    before_overall
    after_overall
    media_signing
    signature_integrity
    continuity
    verification_completeness
    certificate
    public_key_observation
  inspection
    pending_nalus
    pending_frames
    accumulated_timestamps
    latest_timestamps
findings[] { code, message }
limitations[] { code, message }
```

All objects are closed with `additionalProperties: false`. Renaming a field,
changing its meaning or requiredness, or adding a field requires contract review
and normally a new schema version.

## Before and after snapshots

`before` and `after` identify artifact bytes without exposing input paths. The
SHA-256 object always uses `algorithm: "sha256"`; `value` is exactly 64 lowercase
hexadecimal characters and hashes the complete artifact bytes. Hash equality is
artifact identity evidence only and is not a preservation or authenticity claim.

The verification object is a selected M1-shaped snapshot. Its enum meanings and
`overall` derivation remain unchanged. The inspection object contains only M2
pending observations that materially participate in preservation derivation.
It does not redefine M1 or M2.

Full M1/M2 subdocuments were deliberately not embedded. Library versions,
vendor observations, accumulated totals, and raw timestamp values are not needed
to explain schema 0.1 preservation classification and would duplicate the
independent verify/inspection contracts.

## Artifact and stream relations

`artifact_identity.byte_relation` is `identical`, `different`, or
`indeterminate`. It is separate from normalized stream correlation and Media
Signing preservation.

`correlation.stream_relation` is `equivalent`, `ordered_subset`,
`structurally_changed`, or `indeterminate`. Factual evidence uses explicit
`yes`, `no`, or `indeterminate` strings; uncertainty is never serialized as
false. `quality` is `complete`, `ambiguous`, `resource_bounded`, or `incomplete`.

The unique before range contains zero-based inclusive NAL indexes only when the
domain has one deterministic alignment. Otherwise it is `null`. The contract
reports bounded sample counts and role summaries, not per-NAL fingerprint or
index arrays. `diagnostic_detail_bounded` remains explicit.

## Source coverage

`coverage.state` is always one of:

- `full`: the complete supplied before NAL sequence is represented under the
  defined normalized relation;
- `subset`: after is a deterministically established ordered portion of before;
- `unknown`: correlation cannot establish coverage.

`full` does not establish that before is the complete real-world recording.
`subset` does not itself mean signing evidence was partially preserved.

## Applicability and preservation

`applicability.media_signing_preservation` is `applicable`, `not_applicable`, or
`indeterminate`. An unsigned before artifact is `not_applicable`, distinct from
inability to determine applicability.

`preservation.media_signing_evidence` is exactly one of `preserved`,
`partially_preserved`, `not_preserved`, or `indeterminate`. There is no fifth
aggregate state. These values describe only applicable ONVIF Media Signing
evidence in the correlated result; they do not describe source coverage,
transformation quality, source/event authenticity, VMS trust, or custody.

## Signing metadata

`correlation.signing_metadata.relation` is `equivalent`,
`retained_for_subset`, `partially_retained`, `missing_after`,
`replaced_or_unmatched`, `not_applicable`, or `indeterminate`. Counts retain
numeric zero. `correlation_complete` is tri-state, and separate before/after
classification-complete booleans prevent an incomplete classifier result from
being interpreted as not-signing.

## Transitions

Verification transitions retain the before/after overall states and concise
change observations for M1 axes. Inspection transitions report changes to
pending counts and whether raw accumulated/latest timestamp observations
changed. Change strings are `unchanged`, `improved`, `degraded`, `changed`, or
`indeterminate`.

The timestamp transition fields do not contain timestamps, synthesize UTC, or
establish clock trust or chronology. Raw FILETIME values remain available from
the separate inspection contract when needed.

## Transformation context

Transformation kind and optional descriptive fields are caller declarations.
`trust` is always `caller_declared_untrusted`. They provide reproducibility
context but never drive correlation or preservation classification. Optional
strings are present as JSON strings or `null`; no host path is added by the
serializer.

## Findings and limitations

Findings contain a closed stable code and deterministic message. They explain
the already-derived assessment without speculative analysis. Finding order is
the deterministic domain order.

Limitations use closed stable codes and messages. Every assessment states that
source/event authenticity, VMS/transformer trust, completeness of the supplied
before artifact as a real-world recording, chain of custody, and transformation
context trust are not established. A subset adds
`SUBSET_DOES_NOT_ESTABLISH_FULL_EXPORT`.

## Null, zero, and semantic states

Semantic uncertainty and non-applicability always use enum strings such as
`unknown`, `indeterminate`, or `not_applicable`; they are never `null`.

`null` is limited to truly absent optional observations:

- transformation `label`, `pipeline_id`, `tool_version`, and `log_digest`;
- pending hashable-NAL count when M2 has no observation;
- unique before range when no unique range exists.

Numeric zero remains `0`. A present empty optional string remains `""` rather
than becoming `null`.

## Determinism and trust limits

The renderer uses stable field, finding, and limitation order. It emits no
local artifact paths, random identifiers, wall-clock generation time, raw
stderr, upstream diagnostic strings, or unbounded NAL arrays.

Consumers must not infer trusted VMS behavior, source/device/event authenticity,
complete export, omitted-content absence, or chain of custody from any hash,
certificate observation, transformation label, coverage state, or preservation
state.

## Compatibility

Consumers select documents using both `document_type` and `schema_version` and
must validate closed enum/object constraints. M1 verify JSON 0.1 and M2
inspection JSON 0.1 remain independent and unchanged.
