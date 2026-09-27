# Third-party dependencies

This directory holds instructions and optional local checkouts of upstream projects.

## ONVIF Media Signing Framework

* Upstream: https://github.com/onvif/media-signing-framework
* License: MIT
* Pin and API notes: [`../docs/upstream-media-signing.md`](../docs/upstream-media-signing.md)
* Fetch: `../scripts/fetch-upstream.sh`

By default, the fetched tree under `media-signing-framework/` is **not** committed.
Prefer pin + local fetch over vendoring. If vendoring is ever required, document
provenance, license, and exact revision in the same commit.
