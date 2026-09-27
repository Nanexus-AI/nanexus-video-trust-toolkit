# M0 feasibility findings (public)

**Status:** Complete — feasibility checks PASS.  
**M0 is a feasibility spike**, not ONVIF conformance, certification, or a product release.

## Upstream baseline

| Field | Value |
| --- | --- |
| Repository | https://github.com/onvif/media-signing-framework |
| Tag | `r25.12.6` |
| Commit | `cf7785ab993c18d921094e8e505c2c34a1350f28` |
| Project/release version | `25.12.6` |

## Platform / toolchain (summary)

* Ubuntu 24.04, Linux x86_64
* GCC/G++ 13.3
* OpenSSL 3.0.13
* Meson 1.12.x + Ninja 1.13.x
* GStreamer 1.24.x (including videoparsers for `h264parse` / `h265parse`)
* libcheck 0.15.x (unit tests)

## Results

| Check | Result |
| --- | --- |
| Library build + official unit tests | PASS (3/3 tests) |
| Official example signer/validator build | PASS |
| H.264: upstream unsigned fixture → official sign → official verify | PASS (`PUBLIC KEY IS VALID`, `VIDEO IS VALID`) |
| H.265: upstream unsigned fixture → official sign → official verify | PASS (`PUBLIC KEY IS VALID`, `VIDEO IS VALID`) |
| Controlled VCL NAL payload corruption → official verify | PASS (`VIDEO IS INVALID` while public key remains valid) |
| C API suitable for a future C++20 RAII wrapper | **VIABLE** |

### Controlled tamper (summary)

A deterministic single-byte corruption was applied to a hashable H.264 VCL NAL payload
inside a known-good signed MP4, preserving length prefixes and the NAL header. The
official validator then reported the video as invalid while still accepting the public
key — i.e. media integrity failure was detected/classified as not fully authentic.

## C++20 wrapper implication

The public C API exposes an opaque session, explicit configure-then-stream lifecycle,
structured authenticity/provenance results, and a plugin ABI for signing backends. A
thin C++20 wrapper is judged **viable** without reimplementing Media Signing
cryptography or SEI/hash-chain mechanics.

## Important limitations

* Trusted CA configuration is required for provenance/`PUBLIC KEY IS VALID`.
* Official example apps that load bundled test keys expect a working directory under a
  path containing `media-signing-framework` (API itself accepts explicit PEM buffers).
* At tag `r25.12.6`, the example validator source needs a small upstream fix later
  published as PR #238 to build cleanly under GCC 13 with `-Werror`. The library at the
  pinned tag did not require source changes.
* M0 used official MP4 example workflows; broader container/transport coverage is out of scope.

## Feasibility checks

| Check | Result |
| --- | --- |
| Reproducible upstream build | PASS |
| H.264 sign → verify | PASS |
| H.265 sign → verify | PASS |
| Controlled tamper | PASS |
| C++20 wrapper viability | PASS |

**M0 RESULT: PASS**

