# Local integration contract 0.1

`video-trust-integration` is a bounded, read-only, one-request/one-process
adapter for existing finite Nanexus trust operations. It reads exactly one JSON
request from standard input, emits exactly one JSON response to standard
output, and terminates. It opens no listener, retains no state, invokes no
shell, and does not modify supplied media or trust anchors.

The request identity is `nanexus_video_trust_integration_request` version
`0.1`; the response identity is
`nanexus_video_trust_integration_response` version `0.1`. The formal closed
schemas are in `docs/schemas/`. Integration versioning is independent from
product version `0.1.0` and the embedded domain schema versions.

## Operations

- `verify_file`: `codec`, `input_file`, optional `trust_anchor`
- `inspect_file`: `codec`, `input_file`, optional `trust_anchor`
- `compare_preservation`: `codec`, `before_file`, `after_file`, optional
  before/after trust anchors, closed transformation enum, and bounded
  `pipeline_id`

Live RTSP is intentionally absent from integration 0.1. The existing
`verify-live` CLI and live JSONL 0.1 contract are unchanged.

## Execution versus trust

`execution.status` is `completed` or `failed`. A completed response embeds the
unchanged JSON object emitted by the existing domain renderer. INVALID,
UNSIGNED, PARTIAL, NOT_VERIFIABLE, preservation indeterminate, and
not-applicable are completed domain outcomes, not execution failures. A failed
response has `result: null`, `evidence: null`, and no trust verdict.

The closed error codes are `malformed_request`,
`unsupported_contract_version`, `unsupported_operation`, `path_not_allowed`,
`input_not_found`, `input_rejected`, `timeout`, `resource_limit`,
`dependency_unavailable`, `transport_failure`, `contract_mismatch`, and
`internal_failure`. Some codes are reserved for compatible future operations;
finite 0.1 does not perform network transport and does not implement an
in-process deadline. The supervising caller owns the process deadline and must
terminate and reap the adapter if it expires.

## Policy and bounds

`NANEXUS_INTEGRATION_ALLOWED_ROOTS` is required process configuration. It is a
platform path-list of existing directories, limited to 16 entries. Inputs may
be absolute or relative but are canonicalized and must resolve to regular files
beneath one configured root. Symlink escape is rejected. Responses contain
only root-relative references (`root-N/` prefixed when multiple roots exist),
never accepted absolute paths. Each evidence entry also carries a lowercase
SHA-256 identifying the exact bytes read by this integration layer. It is not
an ONVIF signature, provenance, custody, or authenticity proof.

The request is capped at 64 KiB and the response at 2 MiB. Request identifiers
are 1–64 ASCII letters, digits, underscores, or hyphens. Paths are capped at
4096 bytes and pipeline identifiers at 128 bytes. Objects are closed and
unknown fields are rejected. Diagnostics use fixed messages no longer than 256
characters. Concurrency is external: one process executes one request.

The adapter requires system `nlohmann-json` 3.11.3 or later at build time. It
is header-only and adds no runtime JSON library. Finite operation remains
independent from optional GStreamer/live support.

## Trust limits

The integration wrapper does not add trust conclusions. Embedded documents
retain their existing schema and meaning. In particular, VALID does not prove
camera/source identity or depicted-event authenticity; certificate trust does
not prove camera identity; signature validity does not establish custody or
preservation; and subset evidence does not establish full source coverage.
There is no generic `authentic` Boolean.
