# M7 signing gateway core (experimental)

M7 — Legacy Signing Gateway Reference Prototype is in progress. Its first
M7-A slice supplies an internal C++ stream-oriented signer on Ubuntu 24.04
x86_64. Active RTSP gateway orchestration, downstream transport, Jetson
validation (M7-B), and final end-to-end validation/closure (M7-C) remain open.
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

## Live-path continuation

M4's bounded fixed graph is the reuse precedent:
RTSP/TCP → `rtspsrc` → codec RTP depay → compressed byte-stream/AU → bounded
NAL consumer. Its current callback does not expose buffer discontinuity.
Gateway integration must surface uncertain continuity before feeding active
signing state, while preserving the frozen passive M4 contract. That adapter
and bounded runtime remain a subsequent M7-A slice.

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
Generated private keys stay in ignored build directories.

M1–M6 schemas and behavior are unchanged. The M6 adapter remains finite, and
MCP exposes exactly `video_trust.verify_file`,
`video_trust.assess_video_integrity`, and `video_trust.compare_preservation`.
M7 does not expose active signing through Agent/MCP or implement hardware
productization. No M7 Jetson support is claimed by this x86 slice.
