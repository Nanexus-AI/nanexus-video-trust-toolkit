# Live machine contract 0.1

## Status and identity

The M4 live contract is a versioned pre-release compatibility contract. Its
document family is `media_signing_live_session_event` and its schema version is
`0.1`. The document type, version, event names, enum meanings, required-field
semantics, ordering rules, and one-summary guarantee are frozen for compatible
0.1 producers. Incompatible changes require a new schema version.

This is a separate contract from finite-file verification JSON 0.1,
`media_signing_inspection` 0.1,
`media_signing_preservation_assessment` 0.1, and Agent contract 0.1.

The formal schema is
[`schemas/media-signing-live-session-event-0.1.json`](schemas/media-signing-live-session-event-0.1.json).
The transport is JSON Lines: every line is one complete schema-valid object;
consumers never concatenate the lines into a larger JSON document.

## Event model

The vocabulary intentionally contains only material transitions:

1. `session_started` opens the bounded stream and states its universal
   limitations.
2. `epoch_started` starts one codec/connection/validator lifecycle.
3. `verification_observation` reports one material M4-B transition. It may
   contain one new closed result and always contains the accumulated closed
   summary, current tail, and latest relevant official counts.
4. `epoch_ended` closes an epoch and records the operational stop reason.
5. `session_summary` is the one terminal event.

There are no per-NAL events. One M4-B combined semantic observation maps to one
`verification_observation`.

`sequence` begins at zero and increases by exactly one. Epoch indices begin at
one and are monotonic. An epoch must start before its observations and must end
before another starts. Exactly one `session_summary` is required, it is last,
and no event may follow it. The stateful renderer rejects duplicate/out-of-order
observations, duplicate summaries, active-epoch finalization, and post-summary
emission.

## Closed evidence and the live tail

`closed_evidence` and `tail_state` are orthogonal. Closed outcomes are `valid`,
`invalid`, `unsigned`, and `not_verifiable`. A closed result retains separate
media-signing presence, signature-integrity, continuity, completeness,
certificate, source-authenticity, public-key-change, and count fields.

Tail states are `awaiting_evidence`, `no_pending_tail`, `open_pending`, and
`ended_with_unresolved_tail`. Therefore both of these are ordinary,
machine-readable states:

```text
closed_evidence.outcome = valid + tail_state = open_pending
closed_summary.valid > 0       + final_tail_state = ended_with_unresolved_tail
```

An unresolved tail is not corruption and is never flattened to finite-file
`PARTIAL`. `unsigned` is emitted only when deterministic official-framework
evidence established it; it can coexist with a later unresolved tail.
`not_verifiable` means the observed evidence could not support a definitive
valid/invalid/unsigned conclusion.

## Sessions, epochs, and summaries

An epoch is one connection, codec, validator lifecycle, and startup boundary.
Counts and closed-result totals may accumulate at session level, but continuity
is scoped to an epoch. The summary always says
`cross_epoch_continuity: not_established`; a multi-epoch summary also includes
the matching limitation code. It never invents continuity over an unobserved
gap.

The terminal summary includes the stop reason, epoch count, observed codecs,
received/validated totals, closed-observation count and outcome counts, worst
closed outcome, final tail and unresolved indicator, typed boundary counts,
certificate status, source authenticity, bounded findings, and limitations.
The operational stop reason is independent of all trust results. CLI exit
mapping is intentionally absent; M4-D owns that policy.

`worst_closed_outcome` uses the deterministic precedence `invalid`,
`not_verifiable`, `unsigned`, `valid`; it is `null` if no closed observation
exists. This compact field does not replace the separate axes or counts.

## Findings, limitations, and privacy

Findings contain only one of four stable codes plus the fixed severity `info`.
Free-form upstream messages are not serialized. Findings are unique and limited
to 16. Limitations are a unique closed enum limited to five values:

- `source_authenticity_not_established`
- `pre_connection_coverage_not_established`
- `cross_epoch_continuity_not_established`
- `preservation_not_assessed`
- `rtsp_tcp_single_stream_scope`

The public API exposes no field for an RTSP URL, credential, host, path,
environment variable, GStreamer diagnostic, or raw third-party error. Optional
runtime values accept only a bounded opaque token and timestamp character set.

Valid live evidence establishes neither source/camera authenticity nor the
identity of depicted events. Joining a stream does not establish coverage
before the connection. Live verification does not assess preservation.

## Runtime metadata and equivalence

The optional `runtime` object may contain `session_id` and `observed_at`. These
values are non-normative: they may differ between runs and never affect
classification, sequence, epoch, aggregation, or finalization. Semantic
equivalence is tested after removing the complete `runtime` member from each
line.

## Representative JSONL

This abbreviated healthy sequence shows closed valid evidence while the tail
is still open; a later observation closes the tail before the terminal summary:

```jsonl
{"document_type":"media_signing_live_session_event","schema_version":"0.1","event_type":"session_started","sequence":0,"source_authenticity":"not_established","limitations":["source_authenticity_not_established","pre_connection_coverage_not_established","preservation_not_assessed","rtsp_tcp_single_stream_scope"]}
{"document_type":"media_signing_live_session_event","schema_version":"0.1","event_type":"epoch_started","sequence":1,"epoch":1,"codec":"h264","tail_state":"awaiting_evidence"}
{"document_type":"media_signing_live_session_event","schema_version":"0.1","event_type":"verification_observation","sequence":2,"epoch":1,"codec":"h264","tail_state":"open_pending","material_change":true,"closed_evidence":{"outcome":"valid","media_signing":"detected","signature_integrity":"ok","continuity":"intact","verification_completeness":"complete","certificate_status":"not_provided","source_authenticity":"not_established","public_key_has_changed":false,"newly_validated_nalus":10,"newly_validated_frames":5,"accumulated_validated_nalus":10,"accumulated_validated_frames":5,"findings":[]},"closed_summary":{"valid":1,"invalid":0,"unsigned":0,"not_verifiable":0},"official_counts":{"received_nalus":12,"validated_nalus":10,"pending_nalus":2,"received_frames":6,"validated_frames":5,"pending_frames":1}}
```

The contract test renderer provides and validates six complete sequences:

- `healthy`: valid closure, open tail, later closure, orderly summary
- `valid_unresolved`: prior valid closure plus final unresolved tail
- `invalid`: definitive invalid evidence survives operational completion
- `unsigned`: deterministically established unsigned evidence
- `unsigned_unresolved`: unsigned closure plus a later unresolved tail
- `multi_epoch`: H.265 then H.264, with an explicit boundary and no
  cross-epoch continuity claim

The same suite validates every line independently and enforces whole-stream
ordering/finalization rules. Human observation and summary renderers present
the same typed model, while summary-only presentation returns the unchanged
terminal summary document.
