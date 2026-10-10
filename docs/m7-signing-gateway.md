# M7 signing gateway core (experimental)

M7 — Legacy Signing Gateway Reference Prototype is in progress. Its first
M7-A slice supplies an internal C++ stream-oriented signer on Ubuntu 24.04
x86_64. M7-A2 adds an internal discontinuity-aware RTSP/TCP signing runtime.
Downstream transport, an operator invocation/artifact workflow, Jetson validation
(M7-B), and final end-to-end validation/closure (M7-C) remain open.
There is no new command, network service, tagged release, or version change.

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
The test implementation uses bounded memory sinks.

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
Generated private keys stay in ignored build directories.

M1–M6 schemas and behavior are unchanged. The M6 adapter remains finite, and
MCP exposes exactly `video_trust.verify_file`,
`video_trust.assess_video_integrity`, and `video_trust.compare_preservation`.
M7 does not expose active signing through Agent/MCP or implement hardware
productization. No M7 Jetson support is claimed by this x86 slice.
