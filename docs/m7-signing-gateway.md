# M7 signing gateway workflow (experimental)

M7 — Legacy Signing Gateway Reference Prototype supplies a bounded foreground
`gateway-sign` command, H.264/H.265 compressed-media signing, discontinuity-aware
RTSP/TCP ingest, and durable separate epoch artifacts. Implementation and scoped
validation are complete. This remains an experimental reference prototype,
not a production appliance or service; product version remains `0.1.0`.

Validation covers Ubuntu 24.04 x86_64 and native operation on the documented
Jetson Orin Nano ARM64 configuration. Real-camera evidence is limited to the
Tapo C210 H.264 RTSP/TCP path. H.265 is validated synthetically on both platforms.

## Compressed media boundary

`StreamSigner` in `videotrust/stream_signer.hpp` accepts one complete unsigned
Annex-B `NalUnit` at a time. A single official ONVIF session persists across
NALs and access units. Generated Media Signing SEIs are inserted in the
framework's prescribed order. Input NAL bytes remain unchanged; no decoding,
encoding, or transcoding occurs. Both H.264 and H.265 use the same orchestration.
Already present ONVIF signing SEIs are rejected; unrelated SEIs are retained.

`Create` takes externally supplied laboratory key/certificate paths and a
`SignedNalSink`. PEM inputs must be regular files, at most 256 KiB each, with
an unencrypted key matching the leaf certificate. The official framework
checks its required chain format and owns signing mechanics. Temporary key
buffers are cleared. No key material is included in errors or reports.
This is not production PKI, secure key storage, enrollment, or fleet management.

```cpp
StreamSigningOptions options;
options.codec = Codec::H264;
options.key_pem_path = "lab-key.pem";
options.cert_pem_path = "lab-chain.pem";
auto signer = StreamSigner::Create(options, sink);
// Check every result; submit ordered complete NALs from each compressed AU.
// timestamp is gateway observation UTC FILETIME, not camera capture time.
auto error = signer.value().AddNal(nal, timestamp_100ns);
// On clean input EOS: Finalize(). On uncertain continuity: Abort().
```

The example omits application error handling; callers must check `Create`,
every `AddNal`, and `Finalize` before proceeding.

## Egress and limits

The synchronous sink receives a borrowed span with one original NAL or
inserted SEI. It must accept all bytes or return an error; it must not silently
drop media. It owns deadlines/cancellation, file flush/close, and artifact
retention. There is no internal output queue or generic RTSP server.
A successful finalization describes core emission only: applications must
also check their sink's final flush/close.

Fixed maximum NAL/SEI size is 8 MiB, total output is configurable from one
byte to 1 GiB per epoch, and metadata drain is limited to 64 SEIs per call.
The maximum signing-unit length is configurable from 1 to 300 frames
(default 300), using the official partial-GOP API. The core is single-threaded
and non-reentrant. Sinks must return promptly; this source-level API does not
itself enforce a wall-clock deadline.

## Epoch and failure semantics

A successful `Create` starts one epoch with fresh official signing state.
`Finalize` forces official EOS and drains trailing metadata; only then does
state become `Finalized`. `Abort` ends an active epoch without manufacturing
trailing metadata. Destruction does not implicitly finalize.

Malformed/oversized input, official signer errors, and sink rejection or
exceptions produce terminal `Failed` state. `SigningFailure` distinguishes
`Input`, `Signer`, and `Output`; identity/configuration failures are returned
by `Create` before output. Terminal epochs cannot accept more media or
finalize again. Failures may leave already accepted output with an unresolved
tail; never label that artifact cleanly finalized.

A gap, reconnect, key/certificate replacement, or process restart requires a
fresh signer and separately identified output artifact. No continuity is
inferred between epochs. Automatic reconnect and identity hot-swap are absent.

## Discontinuity-aware live orchestration

`LiveSigningOrchestrator` in `videotrust/live_signing.hpp` consumes a narrow
internal event boundary: complete compressed AU, discontinuity, clean EOS,
intentional bounded stop, abort, or terminal failure. It waits for IDR/IRAP
startup and creates a fresh `StreamSigner` plus `SigningEpochSink` for each
epoch. All NALs in an AU share one gateway observation timestamp.

The sink factory must provide distinct output storage for each epoch and each
runtime invocation; IDs are local to a run. Each sink receives exactly one
close callback, carrying its signer state. Flush/close must succeed before the
runtime reports finalized output. A close failure is a terminal output failure.
The runtime caps epoch count at eight and cumulative output at a configurable
maximum of 1 GiB, in addition to A1's per-epoch limits. AUs are bounded to
8 MiB and 4,096 NALs. Caller callbacks must be synchronous, non-reentrant,
prompt, and cooperative; arbitrary blocking caller code cannot be preempted.
A3 supplies an ordinary filesystem sink; A2 also tests bounded memory sinks.

| Input/lifecycle | Signing behavior |
| --- | --- |
| Continuous AU after random-access startup | Continue the current signer |
| Explicit discontinuity | Abort and close the old sink without final SEIs; wait for new random access |
| Explicitly resumed random-access AU | Fresh signer and separate sink/epoch |
| Caller-controlled clean EOS | Finalize, drain metadata, close once |
| Healthy intentional bounded stop | Finalize only the complete observed prefix; no whole-source coverage claim |
| Cancellation or abrupt loss | Abort; no clean completion |
| Identity, input, signer, write, or close failure | Terminal failure; no restart within the failed orchestrator |

`RunGStreamerLiveSigning` reuses the same fixed graph as M4:
RTSP/TCP → `rtspsrc` → codec RTP depay → compressed byte-stream/AU → bounded
`appsink`. The existing passive runner is a wrapper around that shared graph;
M4's JSONL 0.1, stop meanings, CLI, and trust semantics remain unchanged.
There is no arbitrary pipeline/property passthrough or downstream RTSP server.

Only active ingest enables guards. Depay input probes track ordered RTP
sequence (including wrap), SSRC and loss/corruption flags. Output probes track
discontinuity/corruption and stream-start, caps, segment, flush and GAP events.
Initial stream setup and first-buffer DISCONT are startup, while later
uncertainty latches loss. Repeated SETUP/PLAY after media begins, pad removal,
RTCP timeout, pipeline errors and read stalls also terminate active ingest.
Normal IDRs do not reset an epoch.

A real RTSP run stops on the first uncertainty. Probes record only a thread-safe
latch; application-thread code owns signing. Loss is checked around AU delivery,
and queued pre-gap samples may be conservatively discarded. The pipeline is
not resumed or automatically reconnected. A caller restart must use a fresh
runtime and distinct output artifact. `ContinuityLost` is distinct from other
execution failures.

RTSP bus EOS is conservative: testing showed that an abrupt socket close can
surface as EOS. Active ingest therefore aborts on that ambiguous termination;
it does not manufacture clean media closure. Clean EOS remains an explicit
internal event for sources that can establish that lifecycle. Intentional
bounded observations can finalize their observed prefix while retaining the
source-coverage and gateway-identity limitations.

GStreamer's [RTP jitterbuffer documentation](https://gstreamer.freedesktop.org/documentation/rtpmanager/rtpjitterbuffer.html)
describes lost-packet events, and its [appsink documentation](https://gstreamer.freedesktop.org/documentation/app/appsink.html)
describes the sample/EOS boundary. The active guards also check RTP sequence
directly rather than relying on optional lost-event emission alone.

## Foreground operator workflow

Build with `-Dlive_rtsp=enabled` and the dependencies in [build.md](build.md).
Use one configured RTSP/TCP video source and externally managed laboratory
identity files. The unencrypted key must match the leaf in the official-format
certificate chain. The output parent directories must already exist; choose a
**new absolute directory** for each invocation.

```sh
video-trust gateway-sign --codec h264 --duration 30 \
  --key /path/to/gateway.key.pem --cert /path/to/gateway-chain.pem \
  --output-root /path/to/new-session rtsp://camera.example/stream

video-trust verify --codec h264 --ca /path/to/trust-anchor.pem --json \
  /path/to/new-session/epoch-000001/signed.h264
video-trust inspect --codec h264 --ca /path/to/trust-anchor.pem --json \
  /path/to/new-session/epoch-000001/signed.h264
```

For H.265 use `--codec h265` and `signed.h265`. Runtime duration is a strict
integer from 1 to 300 seconds, with the existing five-second connect/read-stall
policy. Exit 0 means a finalized observed prefix and acknowledged artifact
persistence; it is **not** a verification verdict. Exit 2 means configuration,
identity or output-root error; exit 3 means runtime, output, cancellation or
continuity loss. SIGINT/SIGTERM request an abort and retain the observed media.
No media/random-access startup means no successful signing epoch.

Credentials follow `verify-live`: set both `NANEXUS_RTSP_USERNAME` and
`NANEXUS_RTSP_PASSWORD` in the process environment when needed. Each is bounded
to 256 bytes. Do not place credentials in the URL or command arguments.
The command prints generic diagnostics and records neither credentials nor
endpoint/identity paths in artifacts. Environment credentials are a local
reference convention, not a production credential store.

### Artifact layout and interpretation

```text
new-session/
  epoch-000001/
    signed.h264  # signed.h265 for H.265
    epoch.json
  session.json
```

The internal event orchestrator can produce `epoch-000002` through
`epoch-000008` after explicit gap/restart events. The RTSP operator command
stops on the first continuity loss; it does not reconnect automatically. Restart
with a new output root. Epoch indices are local to that root. Never concatenate
epoch files to suggest continuity across a gap.

`nanexus.gateway-epoch.v1` records a bounded index, codec, fixed media basename,
acknowledged bytes, signer state, start/termination reason, failure stage,
`source_identity: not_established` and `cross_epoch_continuity: false`.
`nanexus.gateway-session.v1` records the terminal run result and counts.
These are unsigned operator metadata, separate from existing verifier and
inspection schemas. They are not cryptographic evidence or an authenticity
verdict; verify the media independently. No artifact hash is added because
this workflow directly verifies the media bytes.

`finalized` means the official signer finalized the observed prefix and the
media sink synchronized/closed successfully. `aborted` retains an unresolved
tail without manufacturing an EOS signature. Such media may contain valid
signed prefixes; that does not turn it into a finalized epoch. `failed` records
terminal signer/output failure. Initially `incomplete` metadata is persisted;
on sink closure its reason is `pending_run_result`, and successful run sealing
updates the reason and creates `session.json`. A missing session record,
pending metadata or `.pending` document indicates incomplete run evidence.
Never infer clean run completion from a partial artifact alone.

### Write safety and bounds

Output roots are absolute, at most 4096 bytes, with no empty, dot, dot-dot or
symlink components; existing parent directories must be trusted ordinary
filesystem directories. Root and epoch directories are created exclusively
with mode 0700, files with mode 0600. Any existing root (including an empty
directory or symlink) fails; there is no `--force` and no arbitrary cleanup.
Descriptor-relative no-follow opens keep writes under the held directories.
The writer replaces only its own metadata in its newly created private directory,
using exclusive temporary files, fsync and atomic rename. It never replaces
existing operator media. Protect the output tree against other processes running
as the same user and against external filesystem modification.

Media writes are synchronous and bounded to 1 GiB cumulative/eight epochs,
with the A1/A2 NAL/AU bounds. Metadata is at most 4096 bytes per document.
Media fsync/close, metadata fsync/rename and directory synchronization are checked.
Failures return nonzero and retain partial/incomplete evidence; no success is
claimed. `bytes_acknowledged` counts complete accepted writes; a failed partial
write may leave additional unacknowledged bytes. Regular filesystem operations
are cooperative: byte bounds and ingest duration are not a hard deadline for
blocked disk I/O. Durability depends on the filesystem honoring synchronization;
use ordinary local storage for this reference workflow.

## Trust and validation

A valid signature establishes gateway signer observation from the signing
boundary onward. Gateway signer identity is separate from camera/source
identity. It does not establish pre-gateway integrity, camera key possession,
camera cryptographic identity, or depicted-event authenticity.
`source_authenticity` remains `not_established`; preservation remains separate.

`stream_signer_integration` generates synthetic H.264/H.265 and local test PKI,
then checks incremental output with the existing independent Nanexus verifier,
compressed byte/order preservation, tamper rejection, identity errors,
abort/restart, terminal states, and input/output bounds and failures.
`live_signing_epochs` checks both codecs, no bridging across a gap, explicit
EOS versus abort, identity/input/signer/output/close failures, epoch bounds,
and continuity flags/events. Its signer-error injection is test-only.
`live_signing_rtsp` uses a sealed loopback test origin for continuous, RTP-gap,
cancellation, and socket-disconnect runs, plus unchanged passive M4 runs for
both codecs. This fixture is not a product server or a generic media framework.
`epoch_artifacts` tests separate durable files across a gap and write/sync/close/
publication errors for both codecs. `gateway_sign_cli` tests real foreground
loopback invocations, verify/inspect, cancellation/disconnect, path safety and
credential redaction. Generated private keys stay in ignored build directories.

M1–M6 schemas and behavior are unchanged. The M6 adapter remains finite, and
MCP exposes exactly `video_trust.verify_file`,
`video_trust.assess_video_integrity`, and `video_trust.compare_preservation`.
M7 does not expose active signing through Agent/MCP or implement hardware
productization.

### Scoped platform and camera evidence

Native M7 validation used an NVIDIA Jetson Orin Nano Developer Kit, aarch64,
Ubuntu 24.04.4, L4T R39.2.1, kernel 6.8.12-1021-tegra, GCC 13.3.0 and
GStreamer 1.24.2. The exact implementation was built natively with independently
staged M7-local dependencies: nlohmann JSON 3.11.3 and pinned ONVIF r25.12.6
with the tracked undefined-TLV hardening patch. No system-wide install or
M5/M6 workspace dependency was required. No CUDA, TensorRT, NVDEC, NVENC or
NVIDIA-specific media API is required by the signing path.

Both synthetic codecs produced finalized durable artifacts independently
verified as VALID. ARM64 regression passed all 32 eligible tests; the optional
software FFmpeg preservation matrix was explicitly excluded because the
protected NVIDIA FFmpeg build lacked its required encode/remux behavior.
This does not establish generic ARM64, Jetson-family or camera compatibility.

Two independent five-second C210 H.264 observations finalized separate artifacts
and verified as VALID with source authenticity `not_established`. A controlled
SIGINT to the gateway client aborted its active epoch; a fresh process and output
root then produced a separately VALID five-second artifact. The old artifact
remained unchanged. This validates client cancellation/restart, not automatic
reconnect or disruptive network-fault recovery. No real H.265 camera was tested.

No downstream RTSP restream/server, multi-camera orchestration, production key
provisioning, secure-element/HSM guarantee, long-duration stability or channel
capacity qualification is provided. Aborted epochs remain unresolved-tail
artifacts, separate from cleanly finalized epochs. Neither VALID nor certificate
trust proves camera identity; unsigned media is not thereby invalid, and
transport/execution failure is separate from a trust failure.
