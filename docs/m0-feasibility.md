# M0 feasibility plan (public summary)

M0 answers:

> Can the official ONVIF Media Signing framework reliably support the technical
> foundation of Nanexus Video Trust Toolkit?

M0 is a **feasibility spike**, not a product release.

## Demonstrations

```text
unsigned H.264  → official sign → official verify PASS
unsigned H.265  → official sign → official verify PASS
signed stream   → controlled corruption → official validator detects/classifies failure
```

Plus: reproducible upstream build, and enough API/lifecycle understanding to judge a
clean C++20 wrapper.

## Feasibility checks

| Gate | PASS means |
| --- | --- |
| M0-1 | Official framework builds reproducibly at the pinned revision |
| M0-2 | Official H.264 signer → official validator = PASS |
| M0-3 | Official H.265 signer → official validator = PASS |
| M0-4 | Controlled tampering is detected or correctly classified |
| M0-5 | Upstream C API/lifecycle understood enough for C++20 wrapper judgment |

Gates are not weakened merely to force a PASS.

## Execution outline

1. Install Ubuntu packages for Meson, Ninja, OpenSSL, libcheck, GStreamer development files
2. Fetch upstream at the pin in `docs/upstream-media-signing.md`
3. Build library (+ tests) and, separately, example apps with a local install prefix
4. Run official signer/validator on upstream H.264 and H.265 fixtures
5. Corrupt a known-good signed stream in a controlled way; re-validate

## Out of scope for M0

Nanexus CLI (`video-trust`), M1 lab features, RTSP, GUI, ARM targets, C2PA, new
cryptography, PKI productization, conformance claims, releases, and `v0.1.0` tags.
