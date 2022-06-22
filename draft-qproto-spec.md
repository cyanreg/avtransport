Qproto protocol
===============
The Qproto protocol is a new standardized mechanism for unidirectional and bidirectional
multimedia transport and storage. This protocol aims to be a simple, reliable and robust
low-overhead method that borrows design decisions from other protocols and aims to be
generic, rather than specialized for certain use-cases. With minimal changes, it can be
processed as a stream or saved to a file as an intermediate step or for post-processing.

Rather than just being a new container and protocol filling a niche, this also attempts
to solve issues with current other containers, like timestamp rounding, lack of DTS,
inflexible metadata, inconvenient index positions, lack of context, inextensible formats,
and rigid overseeing organizations.

This specifications provides support for streaming over different protocols:
 - [UDP](#udp)
 - [UDP-Lite](#udp-lite)
 - [QUIC](#quic)

See the [streaming](#streaming) section for information on how to adapt Qproto for such
use-cases.

Specification conventions
-------------------------
Throughout all of this document, bit sequences are always big-endian, and numbers
are always two's complement.</br>
Keywords given in all caps are to be interpreted as stated in IETF RFC 2119.

A special notation is used to describe sequences of bits:
 - `u(bits)`: specifies the data that follows is an unsigned integer of N bits.
 - `i(bits)`: the same, but the data describes a signed integer is signed.
 - `b(bits)`: the data is an opaque sequence of bits that clients MUST NOT interpret as anything else.
 - `r(bits)`: the data is a rational number, with a numerator of `u(bits/2)` and
   following that, a denumerator of `u(bits/2)`. The denumerator MUST be greater than `0`.
 - `C(bits)`: Systematic Raptor code (IETF RFC 5053) with symbol size of 32-bits
   to correct and verify the data from the start of the packet to the start of this code.
   Implementations are allowed to skip checking it.

All floating point samples and pixels are always **normalized** to fit within the
interval `[-1.0, 1.0]`.

Protocol overview
-----------------
On a high-level, Qproto is a thin packetized wrapper around codec, metadata and
user packets and provides context and error resilience, as well as defining a
standardized way to transmit such data between clients.

An overall possible structure of packets in a general Qproto session can be:

|                                           Packet type | Description                                                                                         |
|------------------------------------------------------:|:----------------------------------------------------------------------------------------------------|
|               [Session start](#session-start-packets) | Starts the session with a signature. May be used to identify a stream of bytes as a Qproto session. |
| [Time synchronization](#time-synchronization-packets) | Optional time synchronization field to establish an epoch.                                          |
|   [Stream registration](#stream-registration-packets) | Register a new stream.                                                                              |
|      [Stream initialization data](#init-data-packets) | Stream initialization packets.                                                                      |
|              [Video information](#video-info-packets) | Required video information to correctly present decoded video.                                      |
|                   [ICC profile](#icc-profile-packets) | ICC profile for correct video presentation.                                                         |
|                         [Metadata](#metadata-packets) | Stream or session metadata.                                                                         |
|                       [Index packets](#index-packets) | Optional index packets to enable fast seeking.                                                      |
|                   [Stream data](#stream-data-packets) | Stream data packets.                                                                                |
|          [Stream segments](#stream-data-segmentation) | Segmented stream data packets.                                                                      |
|                    [Stream FEC](#stream-fec-segments) | Optional FEC data for stream data.                                                                  |
|                       [User data](#user-data-packets) | Optional user data packets.                                                                         |
|                       [End of stream](#end-of-stream) | Finalizes a stream or session.                                                                      |

Each packet MUST be prefixed with a descriptor to identify it. Below is a table
of how they're allocated.

|           Descriptor | Packet type                                           |
|---------------------:|-------------------------------------------------------|
|               0x5170 | [Session start](#session-start-packets)               |
|                  0x1 | [Time synchronization](#time-synchronization-packets) |
|                  0x2 | [Stream registration](#stream-registration-packets)   |
|           0x3 to 0x7 | [Stream initialization data](#init-data-packets)      |
|                  0x8 | [Video information](#video-info-packets)              |
|                  0x9 | [Index packets](#index-packets)                       |
|           0xa to 0xe | [Metadata](#metadata-packets)                         |
|         0x10 to 0x14 | [ICC profile](#icc-profile-packets)                   |
|     0x0100 to 0x01ff | [Stream data](#data-packets)                          |
|         0xfe to 0xff | [Stream data segment](#data-segmentation)             |
|         0xfc to 0xfd | [Stream FEC segment](#fec-segments)                   |
|     0x4000 to 0x40ff | [User data](#user-data-packets)                       |
|               0xffff | [End of stream](#end-of-stream)                       |
|                      | **Reverse signalling only**                           |
|     0x5000 to 0x50ff | [Reverse user data](#reverse-user-data)               |
|               0x8001 | [Control data](#control-data)                         |
|               0x8002 | [Feedback](#feedback)                                 |
|               0x8003 | [Stream control](#stream-control)                     |

Session start packets
---------------------
Session start packets allow receivers to plausibly identify a stream of bytes
as a Qproto session. The syntax is as follows:

| Data   | Name                 | Fixed value | Description                                                            |
|:-------|:---------------------|------------:|:-----------------------------------------------------------------------|
| b(16)  | `session_descriptor` |      0x5170 | Indicates this is a Qproto session (`Qp` in hex).                      |
| b(16)  | `session_version`    |         0x1 | Indicates the session version. This document describes version `0x0`.  |
| C(32)  | `raptor`             |      *TODO* | Raptor code to correct and verify the previous contents of the packet. |

Multiple session packets MAY be present in a session, but MUST remain
bytewise-identical.

Implementations are allowed to only compare the first 4 bytes or 8 bytes
to identify a Qproto session.

Time synchronization packets
----------------------------
In order to optionally make sense of timestamps present in the stream, or
provide a context for synchronization, the `time_sync` packet is sent through.</br>
This signals the `epoch` of the timestamps, in nanoseconds since 00:00:00 UTC on
1 January 1970 ([Unix time](https://en.wikipedia.org/wiki/Unix_time)).
The layout of the data in a `time_sync` packet is as follows:

| Data   | Name              | Fixed value  | Description                                                            |
|:-------|:------------------|-------------:|:-----------------------------------------------------------------------|
| b(16)  | `time_descriptor` |          0x1 | Indicates this is a time synchronization packet.                       |
| u(64)  | `epoch`           |              | Indicates the time epoch.                                              |
| C(32)  | `raptor`          |              | Raptor code to correct and verify the previous contents of the packet. |

This field MAY be zero, in which case the stream has no real-world time-relative context.

If this field is non-zero, senders SHOULD ensure the `epoch` value is actual.

If the stream is a file, receivers SHOULD ignore this, but MAY expose the offset
as a metadata `date` field.</br>
Also, when the stream is a file, receivers CAN replace the fields by a user-defined
value in order to permit synchronized presentation between multiple unconnected receivers.

If the stream is live, receivers are permitted to ignore the value and present
as soon as new data is received.</br>
Receivers are also free to trust the field to be valid, and interpret all
timestamps as a positive offset of the `epoch` value, and present at that time,
such that synchronized presentation between unconnected receivers is possible.</br>
Receivers are also free to consider the field trustworthy, and use its value to
estimate the sender start time, as well as the end-to-end latency.

Stream registration packets
---------------------------
This packet is used to signal stream registration.

The layout of the data is as follows:

| Data               | Name                | Fixed value | Description                                                               |
|:-------------------|:--------------------|------------:|:--------------------------------------------------------------------------|
| b(16)              | `reg_descriptor`    |         0x2 | Indicates this is a stream registration packet.                           |
| b(16)              | `stream_id`         |             | Indicates the stream ID for the new stream.                               |
| b(16)              | `related_stream_id` |             | Indicates the stream ID for which this stream is related to.              |
| b(16)              | `derived_stream_id` |             | Indicates the stream ID from which this stream is derived from.           |
| u(64)              | `bandwidth`         |             | Bandwidth in bits per second. MAY be 0.                                   |
| b(64)              | `stream_flags`      |             | Flags to signal what sort of a stream this is.                            |
| b(32)              | `codec_id`          |             | Signals the codec ID for the data packets in this stream.                 |
| r(64)              | `timebase`          |             | Signals the timebase of the timestamps present in data packets.           |
| C(32)              | `raptor`            |             | Raptor code to correct and verify the previous contents of the packet.    |

This packet MAY BE sent for an already-initialized stream. The `bandwidth` field
and the `stream_flags` fields MAY change, however the `codec_id`, `timebase`
AND `related_stream_id` fields MUST remain the same. If the latter are to change
an [end of stream](#end-of-stream) packet MUST be sent first.

The `stream_flags` field may be interpreted as such:

| Bit set | Description                                                                                                             |
|--------:|:------------------------------------------------------------------------------------------------------------------------|
|     0x1 | Stream does not require any [init data packets].                                                                        |
|     0x2 | Stream SHOULD be chosen by default amongst other streams of the same type, unless the user has specified otherwise.     |
|     0x4 | Stream is a still picture and only a single decodable frame will be sent.                                               |
|     0x8 | Stream is a cover art picture for the stream signalled in `related_stream_id`.                                          |
|    0x10 | Stream is a lower quality version of the stream signalled in `derived_stream_id`.                                       |
|    0x20 | Stream is a dubbed version of the stream signalled in `related_stream_id`.                                              |
|    0x40 | Stream is a commentary track to the stream signalled in `related_stream_id`.                                            |
|    0x80 | Stream is a lyrics track to the stream signalled in `related_stream_id`.                                                |
|   0x100 | Stream is a karaoke track to the stream signalled in `related_stream_id`.                                               |
|   0x200 | Stream is intended for hearing impaired audiences. If `related_stream_id` is not `stream_id`, both SHOULD be mixed in.  |
|   0x400 | Stream is intended for visually impaired audiences. If `related_stream_id` is not `stream_id`, both SHOULD be mixed in. |
|   0x800 | Stream contains music and sound effects without voice.                                                                  |
|  0x1000 | Stream contains non-diegetic audio. If `related_stream_id` is not `stream_id`, both SHOULD be mixed in.                 |
|  0x2000 | Stream contains textual or spoken descriptions to the stream signalled in `related_stream_id`.                          |
|  0x4000 | Stream contains timed metadata and is not inteded to be directly presented to the user.                                 |
|  0x8000 | Stream contains sparse thumbnails to the stream signalled in `related_stream_id`.                                       |

Several streams can be chained with the `0x10` bit set to indicate progressively
lower quality/bitrate versions of the same stream.

Sparse thumbnail streams MAY exactly match chapters from `related_stream_id`,
but could be sparser or more frequent.

If all bits in the mask `0x50fc` are unset, `related_stream_id` MUST match
`stream_id`, otherwise the stream with a related ID` MUST exist.

Once registered, streams generally need an [init data packet], unless the
`stream_flags & 0x1` bit is set.

Generic data packet structure
-----------------------------
To ease parsing, the specification defines a common template for data that
requires no special treatment.

The following `generic_data_structure` template is used for generic data packets:

| Data               | Name         |    Fixed value | Description                                                             |
|:-------------------|:------------ |---------------:|:------------------------------------------------------------------------|
| b(16)              | `descriptor` | *as specified* | Indicates the data packet type. Defined in later sections.              |
| b(16)              | `stream_id`  |                | Indicates the stream ID for which this packet is applicable.            |
| u(32)              | `seq_number` |                | Per-stream monotonically incrementing packet number.                    |
| u(32)              | `length`     |                | The size of the data in this packet.                                    |
| C(32)              | `raptor`     |                | Raptor code to correct and verify the previous contents of the packet.  |
| b(`data_length`*8) | `data`       |                | The packet data itself.                                                 |

In case the data needs to be segmented, the following `generic_segment_structure`
template has to be used for segments that follow the above:

| Data               | Name              |    Fixed value | Description                                                                           |
|:-------------------|:------------------|---------------:|:------------------------------------------------------------------------------------- |
| b(16)              | `seg_descriptor`  | *as specified* | Indicates the segment type. Defined in later sections along with the data descriptor. |
| b(16)              | `stream_id`       |                | Indicates the stream ID for which this packet is applicable.                          |
| u(32)              | `seq_number`      |                | Per-stream monotonically incrementing packet number.                                  |
| u(32)              | `seg_length`      |                | The size of the data segment.                                                         |
| C(32)              | `raptor`          |                | Raptor code to correct and verify the previous contents of the packet.                |
| b(`data_length`*8) | `seg_data`        |                | The data for the segment.                                                             |

If the data in a `generic_data_structure` is to be segmented, it will have
a different descriptor. The descriptor in `generic_segment_structure` shall
be different for segments that finalize the data.

Finally, in case the data requires FEC, the following `generic_fec_structure`
is to be used:

| Data                   | Name              |    Fixed value | Description                                                                           |
|:-----------------------|:------------------|---------------:|:--------------------------------------------------------------------------------------|
| b(16)                  | `fec_descriptor`  | *as specified* | Indicates this is an FEC segment packet.                                              |
| b(16)                  | `stream_id`       |                | Indicates the stream ID for whose packets are being backed.                           |
| u(32)                  | `seq_number`      |                | Per-stream monotonically incrementing packet number.                                  |
| b(32)                  | `fec_data_offset` |                | The byte offset for the FEC data for this FEC packet protects.                        |
| u(32)                  | `fec_data_length` |                | The length of the FEC data in this packet.                                            |
| u(32)                  | `fec_covered`     |                | The total amount of payload bytes covered by this and preceding FEC segments.         |
| C(32)                  | `raptor`          |                | Raptor code to correct and verify the previous contents of the packet.                |
| b(`fec_data_length`*8) | `fec_data`        |                | The FEC data that can be used to check or correct the previous data packet's payload. |

The data in an FEC packet MUST be systematic RaptorQ, as per IETF RFC 6330.</br>
Common FEC Object Transmission Information (OTI) format and Scheme-Specific FEC
Object Transmission Information as described in the document are *never* used.

The symbol size MUST be 32-bits.

The total number of bytes covered by this and previous FEC segments shall be
set in the `fec_covered` field. This MUST be greater than zero, unless
`fec_data_length` is also zero.

Init data packets
-----------------
Codecs generally require a one-off special piece of data needed to initialize them.</br>
To provide this data to receivers, the templates defined in the
[generic data packet structure](#generic-data-packet-structure) MUST be used,
with the following descriptors:

| Descriptor |                   Structure |                                                    Type |
|-----------:|----------------------------:|--------------------------------------------------------:|
|        0x3 |    `generic_data_structure` |                      Complete codec initialization data |
|        0x4 |    `generic_data_structure` |                    Incomplete codec initialization data |
|        0x5 | `generic_segment_structure` | Non-final segment for incomplete codec initializal data |
|        0x6 | `generic_segment_structure` |     Final segment for incomplete codec initializal data |
|        0x7 |     `generic_fec_structure` |              FEC segment for the codec initializal data |

For more information on the layout of the specific data, consult the
[codec-specific encapsulation](#codec-encapsulation) addenda.

However, in general, the data follows the same layout as what
[FFmpeg's](https://ffmpeg.org) `libavcodec` produces and requires.</br>
An implementation MAY error out in case it cannot handle the data in the payload.
If so, when reading a file, it MUST stop, otherwise in a live scenario,
it MUST send an `unsupported` [control data](#control-data), if such a
connection is open.

Stream data packets
-------------------
The data packets indicate the start of a stream packet, which may be fragmented into
more [stream data segments](#stream-data-segmentation). It is laid out as follows:

| Data               | Name              | Fixed value | Description                                                                                                                                            |
|:-------------------|:------------------|------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------|
| b(16)              | `data_descriptor` |    0x01\*\* | Indicates this is a stream data packet. The lower 8 bits are used as the `pkt_flags` field.                                                            |
| b(16)              | `stream_id`       |             | Indicates the stream ID for this packet.                                                                                                               |
| u(32)              | `seq_number`      |             | Per-stream monotonically incrementing packet number.                                                                                                   |
| i(64)              | `pts`             |             | Indicates the presentation timestamp for when this frame SHOULD be presented at. MAY be combined with the `epoch` field for synchronized presentation. |
| u(64)              | `duration`        |             | The duration of this packet in stream timebase unis.                                                                                                   |
| u(32)              | `data_length`     |             | The size of the data in this packet.                                                                                                                   |
| C(32)              | `data_raptor`     |             | Raptor code to correct and verify the previous contents of the packet.                                                                                 |
| b(`data_length`*8) | `packet_data`     |             | The packet data itself.                                                                                                                                |

For information on the layout of the specific codec-specific packet data, consult
the [codec-specific encapsulation](#codec-encapsulation) addenda.

The final timestamp in nanoseconds is given by the following formula:
`pts*timebase.num*1000000000/timebase.den`.
Users MAY ensure that this overflows gracefully after ~260 years.

The same formula is also valid for the `dts` field in [H.264](#h264-encapsulation).

Implementations MUST feed the packets to the decoder in an incrementing order
according to the `dts` field.

Negative `pts` values ARE allowed, and implementations **MUST** decode such frames,
however **MUST NOT** present any such frames unless `pts + duration` is greater than 0,
in which case they MUST present the data required for that duration.</br>
This enables removal of extra samples added during audio compression, as well
as permitting video segments taken out of context from a stream to bundle
all dependencies (other frames) required for their presentation.

The `pkt_flags` field MUST be interpreted in the following way:

| Bit position set | Description                                                                                                                                                                                                  |
|-----------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|             0x80 | Packet contains a keyframe which may be decoded standalone.                                                                                                                                                  |
|             0x40 | Packet contains an S-frame, which may be used in stead of a keyframe to begin decoding from, with graceful presentation degradation, or may be used to switch between different variants of the same stream. |
|             0x20 | Packet is incomplete and requires extra [data segments](#stream-data-segmentation) to be completed.                                                                                                          |
|             0x10 | Packet contains the second field of an interlaced frame. See the `interlacing` table in [video information packets](#video-info-packets).                                                                    |
|              0x1 | User-defined flag. Implementations MUST ignore it, and MUST leave it as-is.                                                                                                                                  |

If the `0x40` flag is set, then the packet data is incomplete, and at least ONE
[data segment](#stream-data-segmentation) packet with an ID of `0xfe` MUST be present to
terminate the packet.

Stream data segmentation
------------------------
Packets can be split up into separate chunks that may be received out of order
and assembled. This allows transmission over switched networks with a limited MTU,
or prevents very large packets from one stream interfering with another stream.
The packet structure used for segments is the `generic_segment_structure` from
[generic data packet structure](#generic-data-packet-structure), with the
following descriptors:

| Descriptor |                   Structure |                                         Type |
|-----------:|----------------------------:|----------------------------------------------|
|       0xff | `generic_segment_structure` | Non-final segment for incomplete stream data |
|       0xfe | `generic_segment_structure` |     Final segment for incomplete stream data |

The size of the final assembled packet is the sum of all `seg_length` fields,
plus the `data_length` field from the [stream data packet](#stream-data-packets).

Data segments and packets may arrive out of order and be duplicated.
Implementations MUST reorder them, deduplicate them and assembled them into
complete packets.

Implementations MAY try to decode incomplete data packets with missing segments
due to latency concerns.

Senders MAY send duplicate segments to compensate for packet loss,
should bandwidth permit, but SHOULD use [FEC segments](#fec-segments) instead.

Implementations SHOULD discard any packets and segments that arrive after their
presentation time. Implementations SHOULD garbage collect any packets and segments
that arrive with unrealistically far away presentation times.

Stream FEC segments
-------------------
Stream data packets and segments MAY be backed by FEC data packets.
The structure used for FEC segments follows the `generic_fec_structure` template
from the [generic data packet structure](#generic-data-packet-structure) section,
with the following descriptor:

| Descriptor |                   Structure |                                         Type |
|-----------:|----------------------------:|----------------------------------------------|
|       0xfd |     `generic_fec_structure` |   FEC data segment for the stream data packet|

Implementations MAY discard the FEC data, or MAY delay the previous packet's
decoding to correct it with the FEC data, or MAY attempt to decode the uncorrected
packet data, and if failed, retry with the corrected data packet.

The data in an FEC packet MUST be RaptorQ, as per IETF RFC 6330.</br>
The symbol size MUST be 32-bits.

The total number of bytes covered by this and previous FEC segments shall be
set in the `fec_covered` field. This MUST be greater than zero, unless
`fec_data_length` is also zero.

The same lifetime and duplication rules apply for FEC segments as they do for
regular data segments.

Index packets
-------------
The index packet contains available byte offsets of nearby keyframes and the
distance to the next index packet.

| Data               | Name               |  Fixed value | Description                                                                                                                                                                                |
|:-------------------|:-------------------|-------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| b(16)              | `index_descriptor` |          0x9 | Indicates this is an index packet.                                                                                                                                                         |
| b(16)              | `stream_id`        |              | Indicates the stream ID for the index. May be 0xffff, in which case, it applies to all streams.                                                                                            |
| u(32)              | `prev_idx`         |              | Position of the previous index packet, if any, in bytes, relative to the current position. Must be exact. If exactly 0xffffffff, indicates no such index is available, or is out of scope. |
| i(32)              | `next_idx`         |              | Position of the next index packet, if any, in bytes, relative to the current position. May be inexact, specifying the minimum distance to one. Users may search for it.                    |
| u(32)              | `nb_indices`       |              | The total number of indices present in this packet.                                                                                                                                        |
| C(32)              | `raptor`           |              | Raptor code to correct and verify the previous contents of the packet.                                                                                                                     |
| i(`nb_indices`*64) | `idx_pts`          |              | The presentation timestamp of the index.                                                                                                                                                   |
| i(`nb_indices`*32) | `idx_pos`          |              | The position of a decodable index relative to the current position in bytes. MAY be 0 if unavailable or not applicable.                                                                    |                                                                                                               |
| u(`nb_indices`*16) | `idx_chapter`      |              | If a value is greater than 0, demarks the start of a chapter with an index equal to the value.                                                                                             |

When streaming, `idx_pos` MUST be `0`.

Metadata packets
----------------
The metadata packets can be sent for the overall session, or for a specific
`stream_id` substream. The data is contained in structures templated in the
[generic data packet structure](#generic-data-packet-structure),
with the following descriptors:

| Descriptor |                   Structure |                                      Type |
|-----------:|----------------------------:|------------------------------------------:|
|        0xa |    `generic_data_structure` |                  Complete metadata packet |
|        0xb |    `generic_data_structure` |                       Incomplete metadata |
|        0xc | `generic_segment_structure` | Non-final segment for incomplete metadata |
|        0xd | `generic_segment_structure` |     Final segment for incomplete metadata |
|        0xe |     `generic_fec_structure` |              FEC segment for the metadata |

The actual metadata MUST be stored in `key` and `value` pairs as defined in BARE,
IETF `draft-devault-bare-07`.

ICC profile packets
-------------------
Embedding of ICC profiles for accurate color reproduction is supported.
Embedding of ICC profiles for accurate color reproduction is supported.
The following structure MUST be followed:

| Data                    | Name              |    Fixed value | Description                                                                                        |
|:------------------------|:------------------|---------------:|:---------------------------------------------------------------------------------------------------|
| b(16)                   | `icc_descriptor`  |  0x10 and 0x11 | Indicates this packet contains a complete ICC profile (0x6) or the start of a segmented one (0x7). |
| u(16)                   | `stream_id`       |                | The stream ID for which to apply the ICC profile.                                                  |
| u(32)                   | `seq_number`      |                | Per-stream monotonically incrementing packet number.                                               |
| u(32)                   | `icc_data_length` |                | The length of the ICC profile.                                                                     |
| C(32)                   | `raptor`          |                | Raptor code to correct and verify the previous contents of the packet.                             |
| b(`user_data_length`*8) | `icc_data`        |                | The ICC profile itself.                                                                            |

Often, ICC profiles may be too large to fit in one MTU, hence they can be segmented
in the same way as data packets, as well as be error-corrected. The syntax for
ICC segments and FEC packets is via the following
[generic data packet structure](#generic-data-packet-structure) templates:

| Descriptor |                   Structure |                               Type |
|-----------:|----------------------------:|-----------------------------------:|
|       0x12 | `generic_segment_structure` | Non-final segment for ICC profiles |
|       0x13 | `generic_segment_structure` |     Final segment for ICC profiles |
|       0x14 |     `generic_fec_structure` |    FEC segment for the ICC Profile |

If the `icc_descriptor` is `0x10`, then at least one `0x13` segment MUST be received.

**NOTE**: ICC profiles MUST take precendence over the primaries and transfer
characteristics values in [video info packets](#video-info-packets). The matrix
coefficients are still required for RGB conversion.

Video info packets
------------------
Video info packets contain everything needed to correctly interpret a video
stream after decoding.

| Data          | Name                      | Fixed value  | Description                                                                                                                                   |
|:--------------|:--------------------------|-------------:|:----------------------------------------------------------------------------------------------------------------------------------------------|
| b(16)         | `video_info_descriptor`   |          0x8 | Indicates this packet contains video information.                                                                                             |
| u(16)         | `stream_id`               |              | The stream ID for which to associate the video information with.                                                                              |
| u(32)         | `seq_number`              |              | Per-stream monotonically incrementing packet number.                                                                                          |
| u(32)         | `width`                   |              | Indicates the presentable video width in pixels.                                                                                              |
| u(32)         | `height`                  |              | Indicates the presentable video height in pixels.                                                                                             |
| u(8)          | `colorspace`              |              | Indicates the kind of colorspace the video is in. MUST be interpreted using the `colorspace` table below.                                     |
| u(16)         | `limited_range`           |              | Indicates the signal range. If `0x0`, means `full range`. If `0xffff` means `limited range`. Other values are reserved.                       |
| u(8)          | `chroma_subsampling`      |              | Indicates the chroma subsampling being used. MUST be interpreted using the `subsampling` table below.                                         |
| u(8)          | `interlaced`              |              | Video data is interlaced. MUST be interpreted using the `interlacing` table below.                                                            |
| r(64)         | `framerate`               |              | Indicates the framerate. If it's variable, MAY be used to indicate the average framerate. If video is interlaced, indicates the *field* rate. |
| u(8)          | `bit_depth`               |              | Number of bits per pixel value.                                                                                                               |
| u(8)          | `chroma_pos`              |              | Chroma sample position for subsampled chroma. MUST be interpreted using the `chroma_pos_val` table below.                                     |
| r(64)         | `gamma`                   |              | Indicates the gamma power curve for the video pixel values.                                                                                   |
| u(8)          | `primaries`               |              | Video color primaries. MUST be interpreted according to ITU Standard H.273, `ColourPrimaries` field.                                          |
| u(8)          | `transfer`                |              | Video transfer characteristics. MUST be interpreted according to ITU Standard H.273, `TransferCharacteristics` field.                         |
| u(8)          | `matrix`                  |              | Video matrix coefficients. MUST be interpreted according to ITU Standard H.273, `MatrixCoefficients` field.                                   |
| 16*r(64)      | `custom_matrix`           |              | If `matrix` is equal to 0xff, use this custom matrix instead. Top, left, to bottom right, raster order.                                       |
| u(8)          | `has_mastering_primaries` |              | If `1`, signals that the following `mastering_primaries` and `mastering_white_point` contain valid data.                                      |
| 6*r(64)       | `mastering_primaries`     |              | CIE 1931 xy chromacity coordinates of the color primaries, `r`, `g`, `b` order.                                                               |
| 2*r(64)       | `mastering_white_point`   |              | CIE 1931 xy chromacity coordinates of the white point.                                                                                        |
| u(8)          | `has_luminance`           |              | If `1`, signals that the following `min_luminance` and `max_luminance` fields contain valid data.                                             |
| r(64)         | `min_luminance`           |              | Minimal luminance of the mastering display, in cd/m<sup>2</sup>.                                                                              |
| r(64)         | `max_luminance`           |              | Maximum luminance of the mastering display, in cd/m<sup>2</sup>.                                                                              |
| C(512)        | `raptor`                  |              | Raptor code to correct and verify the previous contents of the packet.                                                                        |

Note that `full range` has many synonyms used - `PC range`, `full swing` and `JPEG range`.</br>
Similarly, `limited range` also has many synonyms - `TV range`, `limited swing`,
`studio swing` and `MPEG range`.

The `colorspace` table is as follows:
| Value | Name        | Description                                                                                                              |
|------:|:------------|:-------------------------------------------------------------------------------------------------------------------------|
|   0x0 | `MONO`      | Video contains no chroma data.                                                                                           |
|   0x1 | `RGB`       | Video data contains some form of RGB.                                                                                    |
|   0x2 | `YUV`       | Video contains some form of YUV (YCbCr).                                                                                 |
|   0x3 | `YCOCGR`    | Video contains a reversible form of YCoCg.                                                                               |
|   0x4 | `YCGCOR`    | Same as `YCOCGR`, with swapped planes.                                                                                   |
|   0x5 | `XYZ`       | Video contains XYZ color data.                                                                                           |
|   0x6 | `XYB`       | Video contains XYB color data. **NOTE**: `matrix` MUST be equal to 0xff and the matrix in `custom_matrix` MUST be valid. |
|   0x7 | `ICTCP`     | Video contains ICtCp color data.                                                                                         |

The `subsampling` table is as follows:
| Value | Name        | Description                                                                                        |
|------:|:------------|:---------------------------------------------------------------------------------------------------|
|   0x0 | `444`       | Chromatic data is not subsampled, or subsampling does not apply.                                   |
|   0x1 | `420`       | Chromatic data is subsampled at half the horizontal and vertical resolution of the luminance data. |
|   0x2 | `422`       | Chromatic data is subsampled at half the vertical resolution of the luminance data.                |

The `interlacing` table is as follows:
| Value | Name        | Description                                                                                                                                                                                               |
|------:|:------------|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|   0x0 | `PROG`      | Video contains progressive data, or interlacing does not apply.                                                                                                                                           |
|   0x1 | `TFF`       | Video is interlaced. One [data packet](#data-packets) per field. If the data packet's `pkt_flags & 0x20` bit is **unset**, indicates the field contained is the **top** field, otherwise it's the bottom. |
|   0x2 | `BFF`       | Same as `TFF`, with reversed polarity, such that packets with `pkt_flags & 0x20` bit **set** contain the **top** field, otherwise it's the bottom.                                                        |
|   0x3 | `TW`        | Video is interlaced. The [data packet](#data-packets) contains **both** fields, weaved together, with the **top** field being on every even line.                                                         |
|   0x4 | `BW`        | Same as `TW`, but with reversed polarity, such that the **bottom** field is encountered first.                                                                                                            |

The `TFF` and `TW`, as well as the `BFF` and `BW` values MAY be interchanged if
it's possible to output one or the other, depending on the setting used, if
the codec supports this.

The `chroma_pos_val` table is as follows:
| Value | Name         | Description                                                                                              |
|------:|:-------------|----------------------------------------------------------------------------------------------------------|
|   0x0 | `UNSPEC`     | Chroma position not specified or does not apply.                                                         |
|   0x1 | `LEFT`       | Chroma position is between 2 luma samples on different lines. (MPEG-2/4 422, H.264 420)                  |
|   0x2 | `CENTER`     | Chroma position is in the middle between all neighbouring luma samples on 2 lines. (JPEG 420, H.263 420) |
|   0x3 | `TOPLEFT`    | Chroma position coincides with top left's luma sample position. (MPEG-2 422)                             |
|   0x4 | `TOP`        | Chroma position is between 2 luma samples on the same top line.                                          |
|   0x5 | `BOTTOMLEFT` | Chroma position coincides with bottom left's luma sample position.                                       |
|   0x6 | `BOTTOM`     | Chroma position is between 2 luma samples on the same bottom line.                                       |

To illustrate:
| Luma line number |         Luma row 1 |     No luma |   Luma row 2 |
|-----------------:|-------------------:|------------:|-------------:|
|                1 | `Luma pixel` **3** |       **4** | `Luma pixel` |
|          No luma |              **1** |       **2** |        Empty |
|                2 | `Luma pixel` **5** |       **6** | `Luma pixel` |

User data packets
-----------------
The user-specific data packet is laid out as follows:

| Data                    | Name               | Fixed value  | Description                                                                                  |
|:------------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------|
| b(16)                   | `user_descriptor`  |     0x40\*\* | Indicates this is an opaque user-specific data. The bottom byte is included and free to use. |
| b(16)                   | `user_field`       |              | A free to use field for user data.                                                           |
| u(32)                   | `user_data_length` |              | The length of the user data.                                                                 |
| C(32)                   | `raptor`           |              | Raptor code to correct and verify the previous contents of the packet.                       |
| b(`user_data_length`*8) | `user_data`        |              | The user data itself.                                                                        |

If the user data needs FEC and segmentation, users SHOULD instead use the
[custom codec packets](#custom-codec-encapsulation).

End of stream
-------------
The EOS packet is laid out as follows:

| Data  | Name             | Fixed value  | Description                                                                                             |
|:----- |:-----------------|-------------:|:--------------------------------------------------------------------------------------------------------|
| b(16) | `eos_descriptor` |       0xffff | Indicates this is an end-of-stream packet.                                                              |
| b(16) | `stream_id`      |              | Indicates the stream ID for the end of stream. May be 0xffff, in which case, it applies to all streams. |
| u(32) | `seq_number`     |              | Per-stream monotonically incrementing packet number.                                                    |
| C(32) | `raptor`         |              | Raptor code to correct and verify the previous contents of the packet.                                  |

The `stream_id` field may be used to indicate that a specific stream will no longer
receive any packets, and implementations are free to unload decoding and free up
any used resources.

The `stream_id` MAY be reused afterwards, but this is not recommended.

If not encountered in a stream, and the connection was cut, then the receiver is
allowed to gracefully wait for a reconnection.

If encountered in a file, the implementation MAY regard any data present afterwards
as padding and ignore it. Qproto files **MAY NOT** be concatenated.

Streaming
=========

UDP
---
To adapt Qproto for streaming over UDP is trivial - simply send the data packets
as-is specified, with no changes required. The sender implementation SHOULD
resent [session start](#session-start-packets),
[time synchronization](#time-synchronization-packets) packets, as well as
all stream registration, initialization and video info packets for all streams
at most as often as the stream with the lowest frequency of `keyframe`s in order
to permit for implementations that didn't catch on the start of the stream begin
decoding.
UDP mode is unidirectional, but the implementations are free to use the
[reverse signalling](#reverse-signalling) data if they negotiate it themselves.

Implementations SHOULD segment the data such that the MTU is never exceeded and
no packet splitting occurs.

Data packets MAY be padded by appending random data or zeroes after the `packet_data`
field up to the maximum MTU size. This permits constant bitrate operation,
as well as preventing metadata leakage in the form of a packet size.

UDP-Lite
--------
UDP-Lite (IETF RFC 3828) SHOULD be preferred to UDP, if support for it is
available throughout the network.</br>
When using UDP-Lite, the **minimum** checksum coverage should be used, where
only the header (8 bytes) is checksummed.

QUIC
----
Qproto tries to use as much of the modern conveniences of QUIC (IETF RFC 9000)
as possible. As such, it uses both reliable and unreliable streams, as well
as bidirectionality features of the transport mechanism.

All data packets with descriptors `0x01**`, `0xff`, `0xfe`, `0xfd` and `0xfc`
MUST be sent over in an *unreliable* QUIC DATAGRAM stream, as per
IETF RFC 9221.</br>
All other packets MUST be sent over a reliable steam. FEC data for those packets
SHOULD NOT be signalled.

Reverse signalling
==================
The following section is a specification on how reverse signalling, where
receiver(s) communicates with the sender, should be formatted.

The same port an method MUST be used for reverse signalling as the sender's.

Control data
------------
The receiver can use this type to return errors and more to the sender in a
one-to-one transmission. The following syntax is used:

| Data                | Name               | Fixed value  | Description                                                                                              |
|:--------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------------------|
| b(16)               | `ctrl_descriptor`  |       0x8001 | Indicates this is a control data packet.                                                                 |
| b(8)                | `cease`            |              | If not equal to `0x0`, indicates a fatal error, and senders MUST NOT sent any more data.                 |
| u(32)               | `error`            |              | Indicates an error code, if not equal to `0x0`.                                                          |
| b(8)                | `resend_init`      |              | If nonzero, asks the sender to resend `time_sync` and all stream `init_data` packets.                    |
| b(128)              | `uplink_ip`        |              | Reports the upstream address to stream to.                                                               |
| b(16)               | `uplink_port`      |              | Reports the upstream port to stream on to the `uplink_ip`.                                               |
| i(64)               | `seek`             |              | Asks the sender to seek to a specific relative byte offset, as given by [index_packets](#index-packets). |

If the sender gets such a packet, and either its `uplink_ip` or its `uplink_port`
do not match, the sender **MUST** cease this connection, reopen a new connection
with the given `uplink_ip`, and resend all packets needed to begin decoding
to the new destination.

The `seek` request asks the sender to resend old data that's still available.
The sender MAY not comply if that data suddently becomes unavailable.
If the value of `seek` is equal to `(1 << 63) - 1`, then the receiver MUST comply
and start sending the newest data.

If the `resent_init` flag is set to a non-zero value, senders SHOULD flush all encoders
such that the receiver can begin decoding as soon as possible.

If operating over [QUIC](#quic), then any old data **MUST** be served
over a *reliable* stream, as latency isn't critical. If the receiver asks again
for the newsest available data, that data's payload is once again sent over an
*unreliable* stream.

The following error values are allowed:
| Value | Description                                                                                                                                                                                                                                                                      |
|------:|:---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|   0x1 | Generic error.                                                                                                                                                                                                                                                                   |
|   0x2 | Unsupported data. May be sent after the sender sends an [init packet](#init-packets) to indicate that the receiver does not support this codec. The sender MAY send another packet of this type with the same `stream_id` to attempt reinitialization with different parameters. |

Feedback
--------
The following packet MAY be sent from the receiver to the sender.

| Data                | Name               | Fixed value  | Description                                                                                                                                                                                                                                         |
|:--------------------|:-------------------|-------------:|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| b(16)               | `stats_descriptor` |       0x8002 | Indicates this is a statistics packet.                                                                                                                                                                                                              |
| b(16)               | `stream_id`        |              | Indicates the stream ID for which this packet is relevant to. MAY be 0xffff to indicate all streams.                                                                                                                                                |
| b(64)               | `bandwidth`        |              | Hint that indicates the available receiver bandwith, in bits per second. MAY be 0, in which case *infinite* MUST be assumed. Senders SHOULD respect it. The figure should include all headers and associated overhead.                              |
| u(32)               | `fec_corrections`  |              | A counter that indicates the total amount of repaired packets (packets with errors that FEC was able to correct). If FEC is not used, MAY be zero.                                                                                                  |
| u(32)               | `corrupt_packets`  |              | Indicates the total number of corrupt packets. If FEC was enabled for the stream, this MUST be set to the total number of packets which FEC was not able to repair.                                                                                 |
| u(32)               | `dropped_packets`  |              | Indicates the total number of dropped [data packets](#data-packets). A dropped packet is when the `seq_number` of a `data_packet` for a given `stream_id` did not increment monotonically, or when [data segments](#Data-segmentation) are missing. |
| u(32)               | `dropped_type`     |              | Indicates the type of packet that was likely dropped. May be `0x0` to indicate unknown or not applicable.                                                                                                                                           |

Receivers SHOULD send out a new statistics packet every time a count was updated.

If the descriptor of the dropped packed is known, receivers SHOULD set it
in `dropped_type`, and senders SHOULD resend it as soon as possible.

Stream control
--------------
The receiver can use this type to subscribe or unsubscribe from streams.

| Data  | Name               | Fixed value  | Description                                                                      |
|:----- |:-------------------|-------------:|:---------------------------------------------------------------------------------|
| b(16) | `strm_descriptor`  |       0x8003 | Indicates this is a stream control data packet.                                  |
| b(16) | `stream_id`        |              | The stream ID for which this packet applies to.                                  |
| b(8)  | `disable`          |              | If `1`, asks the sender to not send any packets relating to `stream_id` streams. |

This can be used to save bandwidth. If previously `disabled` and then `enabled`,
all packets necessary to initialize the stream MUST be resent.

Reverse user data
-----------------
| Data                    | Name               | Fixed value  | Description                                                                                  |
|:------------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------|
| b(16)                   | `user_descriptor`  |     0x50\*\* | Indicates this is an opaque user-specific data. The bottom byte is included and free to use. |
| u(32)                   | `user_data_length` |              | The length of the user data.                                                                 |
| b(`user_data_length`*8) | `user_data`        |              | The user data itself.                                                                        |

This is identical to the [user data packets](#user-data-packets), but with a different ID.

Addendum
========

Codec encapsulation
-------------------
The following section lists the supported codecs, along with their encapsulation
definitions.

 - [Opus](#opus-encapsulation)
 - [AAC](#aac-encapsulation)
 - [AV1](#av1-encapsulation)
 - [H.264](#h264-encapsulation)
 - [ASS](#ass-encapsulation)
 - [Raw audio](#raw-audio-encapsulation)
 - [Raw video](#raw-video-encapsulation)
 - [Custom](#custom-codec-encapsulation)

### Opus encapsulation

For Opus encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x4f707573 (`Opus`).

The payload in the [stream initialization data](#init-data-packets) MUST be
laid out in the following way:

| Data               | Name              | Fixed value                     | Description                                                                           |
|:-------------------|:------------------|--------------------------------:|:--------------------------------------------------------------------------------------|
| b(64)              | `opus_descriptor` | 0x4f70757348656164 (`OpusHead`) | Opus magic string.                                                                    |
| b(8)               | `opus_init_ver`   |                             0x1 | Version of the extradata. MUST be 0x1.                                                |
| u(8)               | `opus_channels`   |                                 | Number of audio channels.                                                             |
| u(16)              | `opus_prepad`     |                                 | Number of samples to discard from the start of decoding (encoder delay).              |
| b(32)              | `opus_rate`       |                                 | Samplerate of the data. MUST be 48000.                                                |
| i(16)              | `opus_gain`       |                                 | Volume adjustment of the stream. May be 0 to preserve the volume.                     |
| u(32)              | `opus_ch_family`  |                                 | Opus channel mapping family. Consult IETF RFC 7845 and RFC 8486.                      |

The `packet_data` MUST contain regular Opus packets with their front uncompressed
header intact.

In case of multiple channels, the packets MUST contain the concatenated contents
in coding order of all channels' packets.

In case the Opus bitstream contains native Opus FEC data, the FEC data MUST be
appended to the packet as-is, and no [FEC packets](#fec-packets) must be present
in regards to this stream.

Implementations **MUST NOT** use the `opus_prepad` field, but **MUST** set the
first stream packet's `pts` value to a negative value as defined in
[data packets](#data-packets) to remove the required number of prepended samples.

### AAC encapsulation

For AAC encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x41414300 (`AAC\0`).

The [stream initialization data](#init-data-packets) payload MUST be the
codec's `AudioSpecificConfig`, as defined in MPEG-4.

The `packet_data` MUST contain regular AAC ADTS packtes. Note that `LATM` is
explicitly unsupported.

Implementations **MUST** set the first stream packet's `pts` value to a negative
value as defined in [data packets](#data-packets) to remove the required number
of prepended samples.

### AV1 encapsulation

For AV1 encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x41563031 (`AV01`).

The [stream initialization data](#init-data-packets) payload MUST be the
codec's so-called `uncompressed header`. For information on its syntax,
consult the specifications, section `5.9.2. Uncompressed header syntax`.

The `packet_data` MUST contain raw, separated `OBU`s.

### H264 encapsulation

For H264 encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x48323634 (`H264`).

The [stream initialization data](#init-data-packets) payload MUST contain an
`AVCDecoderConfigurationRecord` structure, as defined in `ISO 14496-15`.

The `packet_data` MUST contain the following elements in order:
 - A single `b(64)` element with the `DTS`, the time, which when combined with
   the `epoch` field that indicates when a frame should be input into a
   synchronous 1-in-1-out decoder.
 - Raw `NAL` elements, concatenated.

Using `Annex-B` is explicitly **forbidden**.

### ASS encapsulation

For ASS encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x41535334 (`ASS4`).

ASS is a popular subtitle format with great presentation capabilities.
Although it was not designed to be streamed or packetized, doing so is possible
with the following specifications.
These match to how Matroska handles
[ASS encapsulation](https://matroska.org/technical/subtitles.html#ssaass-subtitles).

ASS contains 3 important sections:
 - Script information, in `[Script Info]`
 - Styles, in `[V4 Styles]`
 - Events, in `[Events]`
 - All other sections MUST be stripped.

First, all data MUST be converted to UTF-8.</br>

The [stream initialization data](#init-data-packets) payload MUST contain
the `[Script Info]` and `[V4 Styles]` sections as a string, unmodified.

Events listed in ASS files MUST be modified in the following way:
 - Start and end timestamps, stored in the `Marked` field, MUST be mapped to the
   packet's `pts`, `dts` and `duration` fields, and MUST be ommitted from the data.
 - All other fields MUST be stored in the `packet_data` field as a string, in the
   followin order: `ReadOrder, Layer, Style, Name, MarginL, MarginR, MarginV, Effect, Text`.
 - Comments MAY be left as-is after all the fields.

The `ReadOrder` field is a monotonically incrementing field to identify the correct
order in which to reconstruct the original ASS file.

Multiple packets with the same `pts` and `dts` ARE permitted.

### Raw audio encapsulation

For raw audio encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x52414141 (`RAAA`).

The payload in the [stream initialization data](#init-data-packets) MUST be
laid out in the following way:

| Data               | Name             | Description                                                                            |
|:-------------------|:-----------------|:---------------------------------------------------------------------------------------|
| b(16)              | `ra_channels`    | The number of channels contained OR ambisonics data if `ra_ambi == 0x1`.               |
| b(8)               | `ra_ambi`        | Boolean flag indicating that samples are ambisonics (`0x1`) or plain channels (`0x0`). |
| b(8)               | `ra_bits`        | The number of bits for each sample.                                                    |
| b(8)               | `ra_float`       | The data contained is floats (`0x1`) or integers (`0x0`).                              |
| b(`ra_channels`*8) | `ra_pos`         | The coding position for each channel.                                                  |

The position for each channel is determined by the value of the integers `ra_pos`, and may be interpreted as:
| Value | Position    |
|------:|:------------|
|     0 | Unspecified |
|     1 | Left        |
|     2 | Right       |
|     3 | Center      |
|     4 | Side left   |
|     5 | Side right  |
|     6 | Rear left   |
|     7 | Rear right  |
|     8 | Rear center |
|     9 | LFE         |

If the integer value is not on this list, assume it's unknown. Users are invited
to interpret the channel as they please.

Multiple channels with the same lable MAY be present, and it's up to users to
interpret them.

The `packet_data` MUST contain the concatenated stream of **interleaved** samples
for all channels.

The samples MUST be **normalized** between **[-1.0, 1.0]** if they're float, and
full-range *signed* if they're integers.

The size of each sample MUST be `ra_bits`, and MUST be aligned to the nearest
**power of two**, with the padding in the **least significant bits**. That means
that 24 bit samples are coded as 32 bits, with the data contained in the topmost 24 bits.

### Raw video encapsulation

For raw video encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x52415656 (`RAVV`).

The payload in the [stream initialization data](#init-data-packets) MUST be
laid out in the following way:

| Data                  | Name               | Description                                                                                                                             |
|:----------------------|:-------------------|:----------------------------------------------------------------------------------------------------------------------------------------|
| u(32)                 | `rv_width`         | The number of horizontal pixels the video stream contains.                                                                              |
| u(32)                 | `rv_height`        | The number of vertical pixels the video stream contains.                                                                                |
| u(8)                  | `rv_components`    | The number of components the video stream contains.                                                                                     |
| u(8)                  | `rv_planes`        | The number of planes the video components are placed in.                                                                                |
| u(8)                  | `rv_bpp`           | The number of bits for each **individual** component pixel.                                                                             |
| u(8)                  | `rv_ver_subsample` | Vertical subsampling factor. Indicates how many bits to shift from `rv_height` to get the plane height. MUST be 0 if video is RGB.      |
| u(8)                  | `rv_hor_subsample` | Horizontal subsampling factor. Indicates how many bits to shift from `rv_width` to get the plane width. MUST be 0 if video is RGB.      |
| b(32)                 | `rv_flags`         | Flags for the video stream.                                                                                                             |
| u(`rc_planes`*32)     | `rv_plane_stride`  | For each plane, the total number of bytes **per horizontal line**, including any padding.                                               |
| u(`rv_components`*8)  | `rc_plane`         | Specifies the plane index that the component belongs in.                                                                                |
| u(`rv_components`*8)  | `rc_stride`        | Specifies the distance between 2 horizontally consequtive pixels of this component, in bits for bitpacked video, otherwise bytes.       |
| u(`rv_components`*8)  | `rc_offset`        | Specifies the number of elements before the component, in bits for bitpacked video, otherwise bytes.                                    |
| i(`rv_components`*8)  | `rc_shift`         | Specifies the number of bits to shift right (if negative) or shift left (is positive) to get the final value.                           |
| u(`rv_components`*8)  | `rc_bits`          | Specifies the total number of bits the component's value will contain.                                                                  |
|                       |                    | Additionally, if `rv_flags & 0x2` is *UNSET*, e.g. video doesn't contain floats, the following additional fields **MUST** be present.   |
| i(`rv_components`*32) | `rc_range_low`     | Specifies the lowest possible value for the component. MUST be less than `rc_range_high`.                                               |
| i(`rv_components`*32) | `rc_range_high`    | Specifies the highest posssible value for the component. MUST be less than or equal to `(1 << rc_bits) - 1`.                            |

The purpose of the `rc_offset` field is to allow differentiation between
different orderings of pixels in an RGB video, e.g. `rgb`'s `rc_offset`s will
be `[0, 1, 2]`, whilst `bgr`'s will be `[2, 1, 0]`.
The components MUST be given in the order they appear in the stream.

The `rv_flags` field MUST be interpreted in the following way:

| Bit position set | Description                                                                                                  |
|-----------------:|:-------------------------------------------------------------------------------------------------------------|
|              0x1 | Video is RGB                                                                                                 |
|              0x2 | Video contains IEEE-754 **normalized** floating point values. Precision is determined by the `rv_bpp` value. |
|              0x4 | Video contains a straight, non-premultiplied alpha channel. Alpha is always the last component.              |
|              0x8 | Video contains a premultiplied alpha channel. Alpha is always the last component.                            |
|             0x16 | At least one pixel component is not sharing a plane, e.g. video is *planar*.                                 |
|             0x32 | Video's components are packed, e.g. video is *bitpacked*.                                                    |
|             0x64 | Video's values are **big-endian**. If unset, values are *little-endian*.                                     |

The `packet_data` field MUST contain `rv_planes`, with each plane having
`rv_plane_stride` bytes per line. The number of horizontal lines being given by
`rc_height` for plane number 0, and `rc_height >> rv_ver_subsample` for all other
planes. The actual lines MUST be filled in according to `rc_offset` and `rc_stride`.

`rv_flags` MUST NOT signal both `0x4` AND `0x8`. Such a combination of flags is
unsupported.

This structure is flexible enough to permit zero-copy or one-copy streaming
of video from most sources.

### Custom codec encapsulation

A special section is dedicated for custom codec storage. While potentially useful
for experimentation and for specialized usecases, users of such are invited to submit
an addenda to this document to formalize such containerization. This field MUST NOT be
used if the codec being contained already has a formal definition in this spec.

For custom encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x433f\*\*\*\* (`C?**`), where the bottom 2 bytes can be any value between
0x30 to 0x39 (`0` to `9` in ASCII) and 0x61 to 0x7a (`a` to `z` in ASCII).

The [stream initialization data](#init-data-packets) packet can be any length
and contain any sequence of data.

The `packet_data` field can be any length and contain any sequence of data.
