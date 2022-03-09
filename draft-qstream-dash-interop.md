Qstream <-> DASH interoperation
===============================

Qstream is capable of being losslessy converted into DASH, and vice versa.
This document serves as a specification with which conversion should be done.

DASH to Qstream
===============

DASH manifest
-------------
A `time_sync` packet MUST be generated from the time present in the DASH manifest.
The time zone is necessarily lost, as well as the HTTP time source. Instead, clients
are presumed to already have good time synchronization.

DASH init segments
------------------
The data from the per-stream init segments MUST be made into `init_packets` as-is,
when combined with the data from the manifest.

DASH media segments
-------------------
The data in media segments MUST be copied into `data_packets` as-is. Timestamps
for each packet MUST be converted correctly and MUST be continuous.
Each new DASH media segment MUST create a new `index_packet`, with the data in
the `index_packet` being the overall byte position of each keyframe.</br>
The `index_packet` MUST be present before the first packet contained in the DASH
media segment.

Reverse signalling
------------------
Receivers MUST sent out `Control data` packets as outlined in the `Reverse signalling`
specification in order to seek.
