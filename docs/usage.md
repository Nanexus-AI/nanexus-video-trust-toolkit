# CLI usage

Experimental / pre-release. File commands use Annex-B elementary streams;
`verify-live` is the separate bounded RTSP/TCP reference-lab command.

`video-trust-integration` is a separate finite, read-only local-process adapter
for adjacent systems. Its contract and policy are documented in
[`integration-v0.1.md`](integration-v0.1.md).

Example:

```bash
export NANEXUS_INTEGRATION_ALLOWED_ROOTS=/srv/video-trust-inputs
printf '%s\n' '{"document_type":"nanexus_video_trust_integration_request","schema_version":"0.1","request_id":"verify-1","operation":"verify_file","input":{"codec":"h264","input_file":"/srv/video-trust-inputs/clip.h264"}}' \
  | video-trust-integration
```

M1 signing is a **reference-lab** operation. Valid signatures and certificate
checks do **not** establish camera/source authenticity.

## Commands

### Verify a bounded live stream

```bash
video-trust verify-live --codec h264 --duration 30 --ca ca.pem \
  --jsonl rtsp://camera.example/stream
```

Credentials are never accepted in the URL. Set both
`NANEXUS_RTSP_USERNAME` and `NANEXUS_RTSP_PASSWORD` in the process environment
when authentication is required; setting only one is rejected. Values are
passed directly to `rtspsrc` and are never included in public output.

`--duration` is required and accepts 1–300 seconds. `--jsonl` emits the live
0.1 contract; `--summary-only` requires JSONL and suppresses intermediate
presentation without changing the terminal summary. Human output is the
default.

Fixed bounds are: 5-second connection and read-stall bounds, two non-dropping
AppSink buffers, 8 MiB per sample, 1,024 contract events, 64 KiB per JSON
document, 160 bytes per stable diagnostic, and a 2-second shutdown grace
policy. Reaching a resource bound ends the epoch as `resource_limit` and emits
the summary when safe finalization succeeds.

Exit precedence is definitive invalid evidence (`1`), operational failure
(`3`), unsigned/not-verifiable/unresolved evidence (`4`), then successful
bounded completion (`0`). CLI and policy errors are `2`. Thus later transport
failure cannot hide invalid evidence, and transport failure is never reported
as signature invalidity.

The fixed graph is `rtspsrc` over TCP, one codec-specific RTP depayloader,
fixed byte-stream/access-unit caps, and a bounded non-dropping `appsink`. There
is no reconnect, decode, encode, conversion, muxing, recording, arbitrary
pipeline, or arbitrary GStreamer option. Only one explicitly selected H.264 or
H.265 video stream is supported.

Live valid evidence does not establish source authenticity. Joining does not
establish prior coverage, preservation is not assessed, and an unresolved tail
is not corruption. This command is not a production monitor or service.
The [`scoped live case study`](m4-frigate-live-case-study.md) records one tested
Frigate/go2rtc configuration; it is not a general compatibility claim.

### Sign

```bash
video-trust sign \
  --codec h264 \
  --key signer.key.pem \
  --cert signer-chain.pem \
  -o signed.h264 \
  unsigned.h264
```

* `--cert` is a PEM certificate **chain** (leaf … trust anchor).
* Use `--force` to overwrite an existing output.
* Use `--quiet` to suppress the success summary.

### Verify

```bash
video-trust verify --codec h264 --ca ca.pem signed.h264
video-trust verify --codec h265 --ca ca.pem --json signed.h265
```

* `--ca` is optional. Without it, signature integrity may still be reported;
  certificate status stays `not_provided`.
* `--json` writes machine-readable JSON to stdout only (see
  [`json-v0.1.md`](json-v0.1.md)).

### Inspect

```bash
video-trust inspect --codec h264 --ca ca.pem signed.h264
video-trust inspect --codec h265 --ca ca.pem --json signed.h265
```

Inspection uses the same verification pipeline but reports richer observations.
Human-readable text is the default. `--json` writes the distinct inspection
document to stdout only; see [`inspection-v0.1.md`](inspection-v0.1.md) and its
formal [JSON Schema](schemas/media-signing-inspection-0.1.json).

Use `verify` for the compact verdict/axes contract and `inspect` when validation
counts, raw timestamp values, or vendor observations are needed. Inspection
does not make a stronger authenticity claim. Its completed-operation exit code
matches `verify` for the same input; CLI/input and runtime failures use 2 and 3.

### Tamper (test material only)

```bash
video-trust tamper --codec h264 --operation corrupt-vcl -o bad.h264 signed.h264
video-trust tamper --codec h264 --operation strip-signing-sei -o stripped.h264 signed.h264
video-trust tamper --codec h264 --operation truncate --count 3 -o trunc.h264 signed.h264
```

Operations create controlled fixtures for verification tests. They are not a
general video editor.

### Compare preservation

```bash
video-trust compare-preservation --codec h264 \
  --before-ca ca.pem --after-ca ca.pem \
  --transformation proprietary-or-unknown \
  --pipeline-id tested-export --json \
  signed.h264 exported.h264
```

Both inputs are finite Annex-B files. The command reports artifact identity,
NAL/signing-metadata correlation, source coverage, before/after verification,
and Media Signing preservation as separate dimensions. Transformation and
pipeline ID are caller-declared context and never change classification.
Containers and RTSP are not accepted. See [`preservation-v0.1.md`](preservation-v0.1.md)
and the formal [JSON Schema](schemas/media-signing-preservation-assessment-0.1.json).

## Complete workflow

```text
unsigned.h264
  → video-trust verify …          → UNSIGNED (exit 4)
  → video-trust sign …            → signed.h264
  → video-trust verify … --ca …   → VALID (exit 0)
  → video-trust tamper corrupt-vcl
  → video-trust verify … --ca …   → INVALID (exit 1)
```

Observed M1 classifications (both H.264 and H.265):

| Operation | Typical overall | Typical exit |
| --- | --- | --- |
| unsigned verify | UNSIGNED | 4 |
| sign → verify | VALID | 0 |
| corrupt-vcl | INVALID | 1 |
| strip-signing-sei | UNSIGNED | 4 |
| truncate | PARTIAL | 4 |

Details and caveats: [`m1-contracts.md`](m1-contracts.md).

## Exit codes (summary)

* **verify / inspect:** 0 positive · 1 negative integrity · 2 CLI/input · 3 runtime · 4 unsigned/incomplete/not verifiable
* **sign / tamper:** 0 success · 2 CLI/input/path · 3 runtime
* **compare-preservation:** 0 preserved · 1 partial/not preserved · 2 CLI/input · 3 runtime · 4 indeterminate/not applicable

## Agent access

The experimental Agent layer can verify and compare preservation through MCP
stdio. It is read-only: it does not expose `sign`, `tamper`, or M2 `inspect`.
See [`agent-interface.md`](agent-interface.md).
