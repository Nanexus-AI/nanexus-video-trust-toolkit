# Architecture (public overview)

## Purpose

Nanexus Video Trust Toolkit is intended to become an open-source, vendor-neutral
Video Trust toolkit. The first technical focus is **ONVIF Media Signing**.

Longer-term areas (not implemented yet) may include video integrity tooling,
signer/source identity, certificate/PKI handling, time integrity, VMS/NVR signing
preservation, evidence verification, provenance/chain of custody, passive live
verification, and C2PA interoperability.

## Layering intent

```text
Experimental read-only capability layer (in development; not a stable interface)
        │
        │ video-trust verify --json
        ▼
Nanexus Video Trust Toolkit (C++20 CLI)
        │
        │ uses (does not reimplement)
        ▼
ONVIF media-signing-framework (official C library + plugins)
        │
        ▼
OpenSSL 3.x
```

The Nanexus layer must use the official Media Signing implementation for:

* cryptographic primitives used by Media Signing
* Media Signing wire format and signing SEI format
* hash-chain / signature mechanics

## Milestone posture

* **M0** — feasibility spike against the official framework (build, H.264/H.265
  sign→verify, controlled tamper classification, API understanding). **Complete.**
* **M1** — file-based reference lab (`v0.1.0` released): sign / verify / tamper
  with structured results and CI. **Complete.**
* **M1.5** — experimental read-only Agent capability layer over `video-trust verify`,
  including an experimental stdio MCP server. **In development.** It is not a stable
  SDK or an Agent product.

See [`roadmap.md`](roadmap.md). This project is independent open-source work that
uses the ONVIF Media Signing framework; it is not an official ONVIF project.

## Experimental stdio server

```bash
NANEXUS_VIDEO_TRUST=/path/to/video-trust \
NANEXUS_ALLOWED_ROOTS=/path/to/media \
python -m nanexus_video_trust_agent.mcp_server
```

`NANEXUS_ALLOWED_ROOTS` is `os.pathsep`-separated. Empty roots refuse startup.
Optional `NANEXUS_VERIFY_TIMEOUT` is seconds, at most 300. The server exposes only
`video_trust.verify_file` and `video_trust.assess_video_integrity`. It does not
sign or modify files, and a verification result does not establish source authenticity.
