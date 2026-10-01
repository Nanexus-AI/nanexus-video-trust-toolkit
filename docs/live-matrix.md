# Synthetic live validation matrix

The M4 synthetic matrix is a developer/reference-lab test. It publishes only
generated Annex-B material through a pinned MediaMTX container and consumes it
through the public RTSP/TCP `verify-live` path. It is not a camera, VMS, or
production-network compatibility test.

The case manifest is `tests/fixtures/live-matrix.json`. Fixtures are generated
outside the repository from the deterministic sources described in
`docs/fixtures.md`; private keys, generated media, raw endpoints, and run
artifacts are not committed. The runner validates every JSONL line against the
live 0.1 schema, enforces the expected exit and summary, and compares repeated
semantic traces after removing only the optional non-normative `runtime` field.

Required environment variables are:

```text
VIDEO_TRUST=/absolute/path/to/live-enabled/video-trust
LIVE_MATRIX_FIXTURES=/absolute/path/to/generated/fixtures
LIVE_MATRIX_CA=/absolute/path/to/generated/ca.pem
```

Run the complete matrix with:

```bash
VIDEO_TRUST="$PWD/build/nanexus/video-trust" \
LIVE_MATRIX_FIXTURES=/private/generated/live-matrix \
LIVE_MATRIX_CA=/private/generated/pki/ca.pem \
./scripts/run-live-matrix.sh
```

The wrapper uses `bluenviron/mediamtx:1.15.3` by default, binds RTSP and raw
RTP inputs to loopback, applies bounded subprocess lifetimes in the Python
harness, and removes its container on every exit. `--case CASE_ID` selects a
bounded subset. Set `LIVE_MATRIX_OUTPUT` to select the generated-evidence
directory.

The raw-RTP path exists only to deliver malformed and greater-than-8-MiB
access units without an FFmpeg publisher parsing or discarding them first.
All ordinary cases use FFmpeg stream copy into MediaMTX and RTSP/TCP into the
product. No decode, re-encode, recording, or private network is involved.
