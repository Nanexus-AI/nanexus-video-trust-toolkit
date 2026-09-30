# Experimental Agent interface (M1.5)

Experimental software on `main`. Not a stable API, not a tagged release,
and not a general-purpose Agent.

The C++ `video-trust` core verifies and compares Annex-B H.264/H.265. This
Python layer exposes typed results from `verify --json` and
`compare-preservation --json` over MCP stdio. It does not reimplement Media
Signing or preservation semantics.

An external runtime owns planning, conversation, memory, and orchestration.
This repository owns the domain capability, the contract, the evidence, the
error semantics, and the safety checks under the Agent.

## Design rule

> Design the contract for the weakest model you reasonably want to support;
> recover flexibility for stronger models through capability depth, not ambiguity.

Inputs are typed. Codecs are enums. Outputs are deterministic. L2 adds a
semantic reading of the same verification for a caller that wants it. Depth
is not a claim that one model or one tool is better. No model is officially
supported by these docs.

## Levels

| Level | Tool | Role in M1.5 |
| --- | --- | --- |
| L1 | `video_trust.verify_file` | Primitive verification record |
| L1 | `video_trust.compare_preservation` | Deterministic before/after preservation primitive |
| L2 | `video_trust.assess_video_integrity` | Domain reading of that same record |
| L3 | — | Not in M1.5 |

L1 and L2 share one core execution. Either may be the right call. L2 does
not replace L1.

MCP exposes these three tools. The richer `video-trust inspect` text/JSON
surface remains available through the CLI and domain layer, not as another
Agent capability.

## What you need

* The `video-trust` binary from [`build.md`](build.md)
* Python 3.12 or newer
* [uv](https://docs.astral.sh/uv/)
* Media under a directory you explicitly allow

The Agent package is not published to PyPI. Install it from this repository:

```bash
cd agent
uv sync --frozen
```

`uv sync --frozen` uses `agent/uv.lock`. Runtime dependencies are
`pydantic>=2.12,<3` (locked 2.13.5, MIT) and `mcp>=2.2,<3` (locked 2.2.0,
MIT, official MCP Python SDK). The package itself is Apache-2.0.

Developer tests, which do not call a live model:

```bash
cd agent
uv run --python 3.12 pytest -q
```

## Start the stdio server

```bash
export NANEXUS_VIDEO_TRUST="$PWD/../build/nanexus/video-trust"
export NANEXUS_ALLOWED_ROOTS=/path/to/media
# optional, seconds; default 60; must be > 0 and <= 300
# export NANEXUS_VERIFY_TIMEOUT=60
uv run --python 3.12 python -m nanexus_video_trust_agent.mcp_server
```

`NANEXUS_ALLOWED_ROOTS` uses the platform path separator (`:` on Linux).
An empty value refuses startup (exit 2). Logs go to stderr. MCP messages
use stdin and stdout. The server does not listen on a port.

Any MCP stdio client can launch that command. This repository does not ship
a chat Agent.

## Tools

The two single-file tools take:

| Field | Required | Meaning |
| --- | --- | --- |
| `input_file` | yes | File path. Must resolve inside an allowed root. |
| `codec` | yes | `h264` or `h265` |
| `trust_anchor` | no | PEM trust anchor. Omit it to skip certificate-chain evaluation. |

### `video_trust.verify_file`

Returns the verification document: Media Signing presence, signature
integrity, continuity, verification completeness, certificate status,
`source_authenticity`, and `overall`.

`overall` may be `VALID`, `INVALID`, `UNSIGNED`, `PARTIAL`, or
`NOT_VERIFIABLE`. Those are successful capability executions. They are not
tool failures.

### `video_trust.assess_video_integrity`

Runs the same verification once and adds a deterministic reading:

* `integrity_assessment` — `signing_integrity_intact`,
  `signing_integrity_failed`, `no_media_signing`, `partial_evidence`,
  or `not_verifiable`
* `trust_assessment` — for example
  `certificate_trust_not_evaluated` or
  `signing_key_validated_against_trust_anchor`
* `limitations` — stable codes and messages, including that source
  authenticity is not established
* `follow_up_hints` — stable codes such as re-verify with a trust anchor,
  or do not treat a partial result as VALID

No language model runs inside the service. A validated signing key is not a
trusted camera, not source authenticity, and not proof the depicted event
is real.

### `video_trust.compare_preservation`

This read-only L1 deterministic primitive returns the complete frozen
`media_signing_preservation_assessment` schema `0.1`; it does not reconstruct
preservation from two single-file calls.

| Field | Required | Meaning |
| --- | --- | --- |
| `before_path`, `after_path` | yes | Annex-B files inside an allowed root |
| `codec` | yes | `h264` or `h265` |
| `before_ca_path`, `after_ca_path` | no | Independent PEM anchors for the corresponding state |
| `transformation` | no | Closed CLI enum; caller-declared and untrusted |
| `pipeline_id` | no | Descriptive single-line text, 1–128 characters |

The result directly exposes artifact identity, correlation, full/subset/unknown
coverage, applicability, preservation, signing-metadata relationship,
verification and inspection transitions, findings, and limitations.
Transformation and pipeline ID are forwarded as descriptive context and never
influence classification.

Preservation is distinct from single-file verification. Valid signing does not
establish source authenticity. A valid after-state does not establish full
source coverage. Structurally changed H.265 can remain cryptographically valid.
`PARTIAL` can mean incomplete trailing signing context rather than corruption,
and preservation can remain `indeterminate`.

## Envelope

A call returns a capability envelope:

* `execution_status` — `success` or `failed`
* `result` — present on success, including negative domain outcomes
* `errors` — typed errors on failure; empty on success
* `evidence` — metadata below
* `limitations` — statements the caller must not upgrade past
* `safety_level` — `read`

On the MCP transport, a failed execution is an error result and still
carries the structured envelope. A negative `overall` is not an error result.

### Typed errors

`INVALID_REQUEST`, `PATH_NOT_ALLOWED`, `FILE_NOT_FOUND`,
`UNSUPPORTED_CODEC`, `MALFORMED_MEDIA`, `INVALID_TRUST_ANCHOR`,
`CORE_INPUT_REJECTED`, `CORE_UNAVAILABLE`, `CORE_EXECUTION_FAILED`,
`CORE_TIMEOUT`, `CORE_OUTPUT_TOO_LARGE`, `CONTRACT_MISMATCH`.

Public error text does not include raw core stderr or a host path.

## Evidence

Evidence metadata identifies the execution, not the camera:

* root-relative file/anchor references (single-file or before/after)
* lowercase SHA-256 of the bytes read
* `invocation_id`
* `executed_at` in UTC
* codec
* core name `video-trust`, version, schema `0.1`, and exit code

The SHA-256 is not the ONVIF media signature. Absolute host paths are not
part of the public envelope.

## Safety

* The Agent surface is read-only. It cannot sign, tamper, write, delete,
  or reconfigure media.
* Paths are canonicalized before the root check. A symlink that resolves
  outside an allowed root is rejected.
* `..` path segments and empty roots are rejected.
* `video-trust` is started with an argument vector, not a shell.
* The child environment is limited to `PATH`, `LD_LIBRARY_PATH`, `LANG`,
  and `LC_ALL`.
* Stdout and stderr from the core are capped. Stderr is not copied into
  the public envelope.
* The default timeout is 60 seconds. The maximum is 300.

## Versions

| Label | Value | What it names |
| --- | --- | --- |
| Product | `v0.1.0` | File-based lab pre-release. Not an M1.5 tag. |
| Core JSON schema | `0.1` | `video-trust verify --json` |
| Agent contract | `0.1` | Capability envelope |
| Capability | `0.1` | All three independently versioned capabilities |
| Evaluation suite / scorer | `0.2` | Test harness; M3-I adds preservation scenarios without changing scorer semantics |

## Evaluation

Usability is measured. It is not assumed because MCP exists.

The harness in `agent/tests/eval/` scores observable behavior only: tool
choice, arguments, interpretation of stated fields, whether required claims
were addressed, explicit trust overclaim, error recovery, and extra tool
calls. A structured field may be asserted, denied, or left unstated.
Omission is not treated as a forbidden claim. Hidden reasoning is not
collected. The harness does not call a model to judge another model.

Live model runs are manual. Continuous integration does not need provider
credentials, a local model server, or a GPU.

## Known limits

* Experimental. Field names and codes can still change before a stable API.
* Annex-B H.264 and H.265 only.
* Two read-only tools. No goal-level tool.
* No separate MCP exposure for the M2 inspection surface.
* `source_authenticity` remains `not_established`.
* The CLI `sign` and `tamper` commands are not Agent tools.
