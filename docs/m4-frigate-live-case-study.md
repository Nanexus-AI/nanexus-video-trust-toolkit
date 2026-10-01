# M4 scoped live RTSP case study

This case study records one controlled, configuration-scoped observation of
`video-trust verify-live`. It is not a general camera, RTSP server, Frigate, or
VMS/NVR compatibility statement.

## Tested configuration

The test used pre-generated ONVIF Media Signing H.264 and H.265 elementary
streams with the project's pinned framework version. FFmpeg stream-copy fed a
pinned MediaMTX source relay; a Frigate 0.17.1 installation's bundled go2rtc
1.9.10 instance restreamed that source; and `verify-live` joined the resulting
RTSP stream over TCP. MediaMTX was 1.15.3, FFmpeg was 6.1.1, GStreamer was
1.24.2, and the ONVIF Media Signing Framework was r25.12.6.

The temporary Frigate change added one go2rtc stream only. It did not add a
camera role or involve Frigate decoding, detection, recording, or encoding.
The source relay and Frigate restream were transport components, not trust
authorities.

## Protocol and observations

Ten bounded runs were made: three clean deadline-bounded runs per codec, one
orderly caller cancellation per codec, and one abrupt upstream transport loss
per codec.

| Codec | Scenario | Runs | Closed evidence before stop | Final tail | Stop / exit |
| --- | --- | ---: | --- | --- | --- |
| H.264 | clean bounded observation | 3 | valid | unresolved | deadline / 4 |
| H.264 | orderly caller stop | 1 | valid | unresolved | caller cancellation / 4 |
| H.264 | abrupt upstream loss | 1 | valid | unresolved | transport failure / 3 |
| H.265 | clean bounded observation | 3 | valid | unresolved | deadline / 4 |
| H.265 | orderly caller stop | 1 | valid | unresolved | caller cancellation / 4 |
| H.265 | abrupt upstream loss | 1 | valid | unresolved | transport failure / 3 |

All ten runs produced schema-valid JSON Lines with contiguous event sequence
numbers and exactly one terminal summary. No event followed the summary. Prior
closed valid evidence remained visible when a run ended with an unresolved
tail or transport failure; pending evidence was not classified as corruption.

In the tested configuration, joining aligned validation at an accepted H.264
IDR or H.265 IRAP boundary. Subsequent signing evidence closed normally while
new input formed an open pending tail. At the bounded stop, that final open
tail was reported as unresolved rather than invalid. H.265 VPS/SPS/PPS and
prefix/signing-SEI structure did not create a false tamper result.

The orderly cases were caller-requested bounded shutdowns. The abrupt cases
removed the upstream publisher and were reported separately as transport
failures. In both cases, already closed evidence was retained.

The three H.265 clean runs had identical measured counters. Two H.264 clean
runs matched exactly; the other observed one fewer received NAL and frame at
the deadline boundary. Validated counts, terminal semantics, and normalized
event types were identical. That boundary-sensitive scheduling variation has
not been normalized away.

## Scope and limitations

In the tested configuration, both codecs delivered interpretable Media
Signing evidence through the two RTSP relays. This demonstrates the bounded
reference-lab path for this specific combination of versions and fixtures.

It does not establish source identity, camera identity, prior or complete
coverage, preservation, custody, or the truth of depicted events. Certificate
trust does not establish that an endpoint is a particular camera. RTSP join
may omit media and signing history that preceded the join, and no continuity
is claimed across an unobserved gap. A pending or final unresolved tail is not
itself corruption, and H.265 structural variation is not itself tampering.

The fixtures used a particular GOP and signing cadence. Startup delay,
parameter-set repetition, buffering, join behavior, pending-tail duration,
and shutdown behavior may differ with other cameras, servers, signing cadence,
or network conditions. Production monitoring, broad interoperability, ARM64
edge deployment, and gateway work remain outside this case study.
