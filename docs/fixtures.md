# Test fixtures and provenance

M1 integration tests generate fixtures at test time. Binary Annex-B streams and
test PKI are **not** committed to the public repository by default.

## Generation

```bash
OMS_SIGN_ANNEXB=/path/to/oms_sign_annexb ./scripts/gen-verify-fixtures.sh /path/to/out
```

The Meson integration tests invoke this automatically.

## Contents

| Artifact | Provenance |
| --- | --- |
| `h264/unsigned.h264`, `h265/unsigned.h265` | Synthetic ffmpeg lavfi color frames (tiny) |
| `*/signed.*` | Signed with official ONVIF C API via test helper `oms_sign_annexb` / product `video-trust sign` |
| `pki/*` | Ephemeral OpenSSL test CA + signer (**test-only**, not production identity) |

## Rules

* No commercial/private camera footage.
* No upstream MP4 fixtures committed for convenience.
* Private keys in generated PKI are synthetic and gitignored (`*.pem` / `*.key`).
* Do not reuse test keys outside local tests.

See `scripts/gen-verify-fixtures.sh` for regeneration details (including EPB-safe
signing for Annex-B).
