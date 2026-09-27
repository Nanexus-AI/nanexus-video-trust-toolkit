# M0 feasibility plan (public summary)

M0 answers:

> Can the official ONVIF Media Signing framework reliably support the technical
> foundation of Nanexus Video Trust Toolkit?

M0 is a **feasibility spike**, not a product release.

## Demonstrations

```text
Pinned official upstream (tag r25.12.6)
        ↓
reproducible Meson/Ninja build
        ↓
official H.264 signer → official validator PASS
        ↓
official H.265 signer → official validator PASS
        ↓
controlled tamper → official validator detects / correctly classifies
        ↓
API/lifecycle assessment → M0 Gate
```

## Feasibility checks

| Gate | PASS means |
| --- | --- |
| M0-1 | Official framework builds reproducibly at the pinned revision |
| M0-2 | Official H.264 signer → official validator = PASS |
| M0-3 | Official H.265 signer → official validator = PASS |
| M0-4 | Controlled tampering is detected or correctly classified |
| M0-5 | Upstream C API/lifecycle understood enough for C++20 wrapper judgment |

Gates are not weakened merely to force a PASS.

## First controlled-tamper method (pre-design)

**Selected primary method:** deterministic **video NAL payload byte corruption**.

* Start from a known-good officially signed H.264 (or H.265) file that already validates PASS.
* Locate a hashable VCL NAL (primary coded slice), preserve start-code and NAL header bytes, flip one or more payload bytes at a fixed offset.
* Re-run the official validator with the same trusted CA.

**Expected outcome:** authenticity **not** OK — preferably `OMS_AUTHENTICITY_NOT_OK` (validation string may show `N` for affected NAL Units). Full authenticity OK is a M0 tamper-detection FAIL.

**Rationale:** keeps enough bitstream structure for the validator to process the stream; targets integrity hashing rather than decoder display behavior; reproducible; more unambiguous than SEI removal (which may classify as `OMS_NOT_SIGNED`).

**Fallback / optional second case:** remove ONVIF Media Signing SEI NAL Units from a known-good signed stream; expect the validator **not** to report full authenticity OK (for example `OMS_NOT_SIGNED` or not-feasible). Use only if primary corruption fails to yield an interpretable result.

## Execution outline

2. Fetch upstream at the pin in `docs/upstream-media-signing.md`
3. Build library (+ tests) and, separately, example apps with a local install prefix
4. Run official signer/validator on upstream H.264 and H.265 fixtures
5. Apply the selected controlled tamper; re-validate

## Out of scope for M0

Nanexus CLI (`video-trust`), M1 lab features, RTSP, GUI, ARM targets, C2PA, new
cryptography, PKI productization, conformance claims, releases, and `v0.1.0` tags.
