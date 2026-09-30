# M3 Frigate preservation case study

This is a sanitized, configuration-scoped M3 case study. It records what was
observed with Frigate 0.17.1; it does **not** claim that Frigate generally
preserves ONVIF Media Signing or that other VMS/NVR pipelines behave alike.

## Tested path

The experiment used finite controlled synthetic signed H.264 and H.265 sources,
RTSP/TCP, a stream-copy recording/export path, and post-export Annex-B files.
No live verification product behavior was added.

```text
signed source
    ↓
RTSP transport
    ↓
Frigate recording
    ↓
Frigate realtime export
    ↓
Annex-B extraction
    ↓
Nanexus verification / preservation assessment
```

The controlled source and transport checkpoints were verified before the VMS
stage. Tests used test-only signing material and an isolated temporary camera
configuration. Private network, credential, host, filesystem, and PKI details
are intentionally omitted.

## Recording and export boundary

In the tested configuration, selected finalized recording media could remain
`VALID` with `complete` verification. An ordinary time-bounded realtime export
ended after its last retained signing closure. Its retained signatures still
had integrity `ok`, but trailing units remained pending, so the result was
`PARTIAL` and `incomplete`.

Repeating the export with a later end that included subsequent signing closure
evidence restored `VALID` and `complete`. The ordinary and extended exports
were each repeated and produced the same bytes and verification observations
for that recording.

This yields an operational distinction:

> The desired visual interval may differ from the evidence-bearing media
> interval required for complete verification.

There is no universal guard duration. Signing cadence, stored segment
boundaries, and export behavior all matter. A consumer should measure its
pipeline and select boundaries from observed closure evidence, not assume a
fixed number of seconds.

The preservation assessment remained deliberately conservative where bounded,
non-unique correlation could not establish coverage: `preservation =
indeterminate` and `coverage = unknown` are legitimate results, even when the
after artifact is `VALID/complete`.

## H.264 and H.265 observations

H.264 more often retained a straightforward ordered-subsequence relationship
through the tested recording/export path. H.265 showed stronger structural
effects: RTSP/container processing and Annex-B extraction could repeat or
insert VPS/SPS/PPS units and normalize elementary-stream structure. Some H.265
comparisons were therefore `structurally_changed` rather than ordered subsets.

Those structural changes did not automatically remove signing metadata or
invalidate signatures. In the measured H.265 recording and extended export,
signing remained present and verification remained `VALID/complete`. The
ordinary H.265 export showed the same pending-tail `PARTIAL/incomplete`
boundary pattern as H.264.

> Codec structural change is not equivalent to signature failure or tampering.

## Verification is not preservation

```text
verification result
≠
preservation result
```

The two answer different questions. Examples supported by the M3 contract and
experiments include:

* an after artifact can be `VALID` while preservation is `indeterminate`;
* signing metadata can survive while source coverage remains `unknown`;
* an ordered subset can verify without proving that the full source was
  exported; and
* `PARTIAL` can reflect incomplete trailing context rather than invalid
  signatures or corruption.

The assessment also keeps artifact identity, stream correlation,
signing-metadata relationship, certificate status, time observations, and
source coverage separate. Neither verification nor preservation establishes
camera identity, source/event authenticity, a complete original recording,
trusted chronology, custody, or trustworthy VMS behavior.

## Scope and reproducibility boundary

The result applies to the exact tested Frigate 0.17.1 stream-copy path, finite
synthetic sources, codecs, transport, recording selection, export calls, and
extraction procedure. It is evidence for that configuration, not general
ONVIF conformance testing of Frigate. M3 evaluates stored/exported artifacts;
passive or continuous live RTSP verification remains future M4 work.

For the deterministic machine contract, see
[`preservation-v0.1.md`](preservation-v0.1.md). For the reproducible synthetic
matrix used alongside the field study, see
[`../tests/fixtures/preservation-matrix.json`](../tests/fixtures/preservation-matrix.json).
