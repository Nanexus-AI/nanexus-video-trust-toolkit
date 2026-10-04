# M6 adjacent-system reference integration

## Scope

M6 demonstrates how an adjacent video system can supply finite media to the
bounded Nanexus local-process integration contract. The reference is a small
Python standard-library consumer, not a Frigate plugin, service, recorder,
restreamer, or vendor SDK wrapper.

```text
Adjacent system or exported artifact
                │
                ▼
examples/adjacent_system_consumer.py
                │ one integration request on stdin
                ▼
video-trust-integration
                │ one integration response on stdout
                ▼
Bounded execution/domain summary
```

The consumer reaches only the public integration `0.1` process contract. It
does not call internal C++ APIs, verify signatures, correlate NAL units, or
derive preservation. Its only interpretation is structural: execution failure
is distinct from a completed domain outcome, and it selects a few existing
domain fields for a concise summary.

## Reproducible synthetic workflow

Build the project and generate the ordinary test fixtures, then compare one
signed source with a deterministic truncated export-like artifact:

```bash
video-trust tamper --codec h264 --operation truncate --count 3 \
  --force -o /tmp/exported-truncated.h264 signed.h264

python3 examples/adjacent_system_consumer.py \
  --adapter build/nanexus/video-trust-integration \
  --allowed-root /path/to/fixture-root \
  --operation compare_preservation \
  --codec h264 \
  --before-file /path/to/fixture-root/signed.h264 \
  --after-file /path/to/fixture-root/exported-truncated.h264 \
  --before-trust-anchor /path/to/fixture-root/ca.pem \
  --after-trust-anchor /path/to/fixture-root/ca.pem
```

The automated reference test covers completed VALID verification, completed
UNSIGNED inspection, identical-artifact preservation, an export-like truncated
artifact, allowed-root rejection, malformed adapter output, and parent-enforced
timeout/cleanup. UNSIGNED and indeterminate domain states remain completed
executions rather than transport failures.

The consumer returns zero for any structurally valid completed domain result,
one for a typed adapter execution failure, and two for consumer/adapter
protocol or lifecycle failure. It does not expose an `authentic`,
`camera_verified`, or `trusted_video` Boolean.

## Scoped Frigate-adjacent observation

The same unmodified consumer was run read-only against a retained finite H.264
source/export pair from the earlier Frigate 0.17.1 preservation study. No
Frigate configuration, database, recording, export, or go2rtc control action
was performed during M6.

The integration execution completed and embedded
`media_signing_preservation_assessment` `0.1`. The consumer reported Media
Signing preservation `indeterminate` and source coverage `unknown`, matching
the existing conservative M3 assessment. This is evidence that the generic M6
boundary can consume one real adjacent-system artifact pair; it is not generic
Frigate, go2rtc, VMS, or NVR compatibility.

On the measured x86_64 run, the one-shot comparison completed in approximately
0.06 seconds, used about 15 MiB peak RSS, and produced a 1,005-byte consumer
summary. A comparable synthetic comparison completed in approximately 0.02
seconds, used about 14 MiB peak RSS, and produced a 1,006-byte summary. These
are bounded observations from small fixtures, not performance guarantees.

## ARM64 checkpoint

The finite integration target was configured and built from the committed M6
source on the previously documented NVIDIA Jetson Orin Nano / Ubuntu 24.04
configuration with live support disabled. The isolated target build completed
in approximately 29 seconds. One synthetic H.264 request completed with the
existing VALID domain result, schema `0.1`, and root-relative evidence; the
adapter process measured approximately 0.01 seconds and 7 MiB peak RSS.

This extends evidence for the M6 finite adapter only on that exact documented
configuration. It does not establish universal ARM64, Jetson-family, or RK3588
support.

## Limitations

- Finite Annex-B H.264/H.265 inputs only.
- Passive live verification remains the separate M4 `verify-live` CLI surface;
  it is not an M6 integration operation.
- An artifact supplied by an adjacent system does not establish its custody,
  completeness, or relationship to all source media.
- VALID does not prove camera/source identity or depicted-event authenticity.
- Certificate trust does not prove camera identity.
- Preservation `indeterminate` and subset/unknown coverage are legitimate
  results, not execution failures.
- The C++ facade is internal/source-level and is not a stable external ABI.
