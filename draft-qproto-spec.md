# Qproto protocol

The Qproto protocol is a new standardized mechanism for multimedia transport and
storage. This protocol aims to be a simple, reliable and robust low-overhead method
that borrows design decisions from other protocols and aims to be generic,
rather than specialized for certain use-cases. With minimal changes, it can be
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

### Specification conventions

Throughout all of this document, bit sequences are always *big-endian*, and numbers
are always *two's complement*.<br/>
Keywords given in all caps are to be interpreted as stated in IETF BCP 14.

A special notation is used to describe sequences of bits:
 - `u(N)`: specifies the data that follows is an unsigned integer of N bits.
 - `i(N)`: the same, but the data describes a signed integer is signed.
 - `b(N)`: the data is an opaque sequence of N bits that clients MUST NOT interpret as anything else.
 - `r(N*2)`: the data is a rational number, with a numerator of `i(N)` and
   following that, a denominator of `i(N)`. The denominator MUST be greater than `0`.
 - `R(k, m)`: Systematic Raptor code (IETF RFC 5053), with symbol size of 32-bits,
   having `m`-bits long parity data (`m/32` symbols), and covering `k/32` symbols,
   from the start of the packet or where the coverage of the last code ended.

Raptor code correction is optional for receiver implementations.
Senders MUST implement it.

All packets are at least 36 bytes long (288 bits), and always have an 8 byte
(64 bits) Raptor code after the first 28 bytes to verify and correct their data.

All floating point samples and pixels are always **normalized** to fit within the
interval `[-1.0, 1.0]`.

The `global_seq` number SHOULD start at 0, and MUST be incremented by 1 each time
after a packet has been sent. Once at `0xffffffff`, it MUST overflow back to `0x0`.
This overflow MUST be handled by the receiver.

### Protocol overview

On a high-level, Qproto is a thin packetized wrapper around codec, metadata and
user packets and provides context and error resilience, as well as defining a
standardized way to transmit such data between clients.

An overall possible structure of packets in a general Qproto session can be:

|                                           Packet type | Description                                                                                         |
|------------------------------------------------------:|:----------------------------------------------------------------------------------------------------|
|               [Session start](#session-start-packets) | Starts the session with a signature. May be used to identify a stream of bytes as a Qproto session. |
| [Time synchronization](#time-synchronization-packets) | Optional time synchronization field to establish an epoch.                                          |
|                   [Embedded font](#font-data-packets) | Optional embedded font for subtitles.                                                               |
|   [Stream registration](#stream-registration-packets) | Register a new stream.                                                                              |
|           [Stream duration](#stream-duration-packets) | Optionally notify of the stream(s)' duration.                                                       |
|      [Stream initialization data](#init-data-packets) | Stream initialization packets.                                                                      |
|              [Video information](#video-info-packets) | Required video information to correctly present decoded video.                                      |
|                   [ICC profile](#icc-profile-packets) | ICC profile for correct video presentation.                                                         |
|                         [Metadata](#metadata-packets) | Stream or session metadata.                                                                         |
|                       [Index packets](#index-packets) | Optional index packets to enable fast seeking.                                                      |
|       [Video orientation](#video-orientation-packets) | Video orientation.                                                                                  |
|                   [Stream data](#stream-data-packets) | Stream data packets.                                                                                |
|          [Stream segments](#stream-data-segmentation) | Segmented stream data packets.                                                                      |
|                    [Stream FEC](#stream-fec-segments) | Optional FEC data for stream data.                                                                  |
|                       [User data](#user-data-packets) | Optional user data packets.                                                                         |
|                       [End of stream](#end-of-stream) | Finalizes a stream or session.                                                                      |

Each packet MUST be prefixed with a descriptor to identify it. Below is a table
of how they're allocated.

|           Descriptor | Packet type                                           |
|---------------------:|:------------------------------------------------------|
|               0x5170 | [Session start](#session-start-packets)               |
|                  0x2 | [Stream registration](#stream-registration-packets)   |
|           0x3 to 0x7 | [Stream initialization data](#init-data-packets)      |
|                  0x8 | [Video information](#video-info-packets)              |
|                  0x9 | [Index packets](#index-packets)                       |
|           0xA to 0xE | [Metadata](#metadata-packets)                         |
|         0x10 to 0x14 | [ICC profile](#icc-profile-packets)                   |
|         0x20 to 0x24 | [Embedded font](#font-data-packets)                   |
|         0x30 to 0x31 | [FEC grouping](#fec-grouping)                         |
|                 0x40 | [Video orientation](#video-orientation-packets)       |
|         0xFC to 0xFD | [Stream FEC segment](#stream-fec-segments)            |
|         0xFE to 0xFF | [Stream data segment](#stream-data-segmentation)      |
|     0x0100 to 0x01FF | [Stream data](#data-packets)                          |
|     0x0300 to 0x03FF | [Time synchronization](#time-synchronization-packets) |
|     0x4000 to 0x40FF | [User data](#user-data-packets)                       |
|               0xF000 | [Stream duration](#stream-duration-packets)           |
|               0xFFFF | [End of stream](#end-of-stream)                       |
|                      | **Reverse signalling only**                           |
|     0x5000 to 0x50FF | [Reverse user data](#reverse-user-data)               |
|               0x8001 | [Control data](#control-data)                         |
|               0x8002 | [Feedback](#feedback)                                 |
|               0x8003 | [Resend](#resend)                                     |
|               0x8004 | [Stream control](#stream-control)                     |

## Packet description

This section lists the syntax of each individual packet type and specifies its purpose,
and rules about its usage.

Special considerations and suggestions which need to be taken into account in a streaming
scenario are described in the [streaming](#streaming) section.

### Session start packets

Session start packets allow receivers to plausibly identify a stream of bytes
as a Qproto session. The syntax is as follows:

| Data         | Name                 | Fixed value | Description                                                                |
|:-------------|:---------------------|------------:|:---------------------------------------------------------------------------|
| `b(16)`      | `session_descriptor` |      0x5170 | Indicates this is a Qproto session (`Qp` in hex).                          |
| `b(16)`      | `session_version`    |         0x0 | Indicates the session version. This document describes version `0x0`.      |
| `u(32)`      | `global_seq`         |             | Monotonically incrementing per-packet global sequence number.              |
| `b(8)`       | `producer_name_len`  |             | Length of the string in `producer_name`. MUST be less than or equal to 13. |
| `b(104)`     | `producer_name`      |             | 13 byte UTF-8 string, containing the name of the producer.                 |
| `b(16)`      | `producer_major`     |             | Major version of the producer.                                             |
| `b(16)`      | `producer_minor`     |             | Minor version of the producer.                                             |
| `b(16)`      | `producer_micro`     |             | Micro (patch) version of the producer.                                     |
| `R(224, 64)` | `raptor`             |             | Raptor code to correct and verify the contents of the packet.              |

Multiple session packets MAY be present in a session, but MUST remain
bytewise-identical.

Implementations are allowed to test the first 4 bytes to detect a Qproto stream.<br/>
The Raptor code is, like for all packets, allowed to be ignored.

### Time synchronization packets

Time synchronization packets are optional dual-purpose packets which signal:

 - A context for all timestamps in all streams, using a field called `epoch`,
   which denotes an absolute basis in time, to allow for optional receiver
   synchronization.
 - A device clock counter (`ts_clock_seq`) and clock frequency (`ts_clock_freq`),
   to allow for accurate clock cycle synchronization of packet timestamps,
   stream synchronization, and to enable clock drift and jitter compensation.

The structure of the data in a `time_sync` packet is as follows:

| Data         | Name              | Fixed value | Description                                                                                                                           |
|:-------------|:------------------|------------:|:--------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)`      | `data_descriptor` |    0x03\*\* | Indicates this is a stream data packet. The lower 8 bits are used as the `ts_clock_id` field.                                         |
| `b(16)`      | `ts_clock_hz2`    |             | Value, in multiples of 1/65536 Hz, to be added to `ts_clock_hz` to make `ts_clock_freq`.                                              |
| `u(32)`      | `global_seq`      |             | Monotonically incrementing per-packet global sequence number.                                                                         |
| `u(64)`      | `epoch`           |             | Indicates absolute time, in nanoseconds since 00:00:00 UTC on 1 January 1970 ([Unix time](https://en.wikipedia.org/wiki/Unix_time)).  |
| `u(64)`      | `ts_clock_seq`    |             | Monotonically incrementing counter, incremented once for each cycle of the device clock, at a rate of `ts_clock_freq` per second.     |
| `u(32)`      | `ts_clock_hz`     |             | Hertz value of the device clock. MAY be set to zero to indicate the source is unclocked.                                              |
| `R(224, 64)` | `raptor`          |             | Raptor code to correct and verify the contents of the packet.                                                                         |

The `ts_clock_freq` field is equal to `ts_clock_freq = ts_clock_hz + ts_clock_hz2/65536`.
Implementations SHOULD prefer to use integer math, and instead have the `ts_clock_freq` in increments of 1/65536 Hz.

The field defines a strictly monotonic clock signal with a rate of `ts_clock_freq`, which atomically increments a counter,
`ts_clock_seq` on the **rising** edge of the waveform.

To interpret the clock, read the [jitter compensation](#jitter-compensation) section. Senders SHOULD send time synchronization
packets as often as necessary to prevent clock drift and jitter.

In case of a zero `ts_clock_hz`, the sender MUST be assumed to not provide a clock signal reference, and the timestamps
MUST be interpreted as being, in the receiver's understanding, realtime.

The `epoch` is a global, *optional* field that receivers MAY interpret.<br/>
The `epoch` value MUST NOT change between packets.<br/>
The `epoch` is an absolute starting point, for all timestamps in
all streams, in nanoseconds since 00:00:00 UTC on 1 January 1970
([Unix time](https://en.wikipedia.org/wiki/Unix_time)).

The possible applications of the `epoch` field are described in the [epoch](#epoch) section.

### Stream registration packets

This packet is used to signal stream registration.

The layout of the data is as follows:

| Data         | Name                | Fixed value | Description                                                                     |
|:-------------|:--------------------|------------:|:--------------------------------------------------------------------------------|
| `b(16)`      | `reg_descriptor`    |         0x2 | Indicates this is a stream registration packet.                                 |
| `b(16)`      | `stream_id`         |             | Indicates the stream ID for the new stream.                                     |
| `u(32)`      | `global_seq`        |             | Monotonically incrementing per-packet global sequence number.                   |
| `b(16)`      | `related_stream_id` |             | Indicates the stream ID for which this stream is related to.                    |
| `b(16)`      | `derived_stream_id` |             | Indicates the stream ID from which this stream is derived from.                 |
| `u(64)`      | `bandwidth`         |             | Average bitrate in bits per second. MAY be 0 to indicate VBR or unknown.        |
| `b(64)`      | `stream_flags`      |             | Flags to signal what sort of a stream this is.                                  |
| `R(224, 64)` | `raptor`            |             | Raptor code to correct and verify the first 7 symbols of the packet.            |
| `b(32)`      | `codec_id`          |             | Signals the codec ID for the data packets in this stream.                       |
| `r(64)`      | `timebase`          |             | Signals the timebase of the timestamps present in data packets.                 |
| `u(64)`      | `reserved`          |             | Reserved for future use. MUST be 0x0.                                           |
| `R(160, 64)` | `raptor_2`          |             | Raptor code to correct the leftover previous 5 words.                           |

This packet MAY BE sent for an already-initialized stream. The `bandwidth` field
and the `stream_flags` fields MAY change, however the `codec_id`, `timebase`
AND `related_stream_id` fields MUST remain the same. If the latter are to change
an [end of stream](#end-of-stream) packet MUST be sent first.

The `stream_flags` field may be interpreted as such:

| Bit set | Description                                                                                                             |
|--------:|:------------------------------------------------------------------------------------------------------------------------|
|     0x1 | Stream does not require any [init data packet](#init-data-packets).                                                     |
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
|  0x4000 | Stream contains timed metadata and is not intended to be directly presented to the user.                                |
|  0x8000 | Stream contains sparse thumbnails to the stream signalled in `related_stream_id`.                                       |

Several streams can be **chained** with the `0x10` bit set to indicate
progressively lower quality/bitrate versions of the same stream. The very first
stream in the chain MUST NOT have bit `0x10` set.

Sparse thumbnail streams MAY exactly match chapters from `related_stream_id`,
but could be sparser or more frequent.

If bits `0x8`, `0x20`, `0x40`, `0x80`, `0x100`, `0x200` are all unset,
`related_stream_id` MUST match `stream_id`, otherwise the stream with a related
*different* ID MUST exist.

Once registered, streams generally need an [init data packet](#init-data-packets),
unless the `stream_flags & 0x1` bit is set.

### Generic data packet structure

To ease parsing, the specification defines a common template for data that
requires no special treatment.

The following `generic_data_structure` template is used for generic data packets:

| Data          | Name         |    Fixed value | Description                                                             |
|:--------------|:------------ |---------------:|:------------------------------------------------------------------------|
| `b(16)`       | `descriptor` | *as specified* | Indicates the data packet type. Defined in later sections.              |
| `b(16)`       | `stream_id`  |                | Indicates the stream ID for which this packet is applicable.            |
| `u(32)`       | `global_seq` |                | Monotonically incrementing per-packet global sequence number.           |
| `u(32)`       | `length`     |                | The size of the data in this packet.                                    |
| `b(128)`      | `padding`    |                | Padding, reserved for future use. MUST be 0x0.                          |
| `R(224, 64)`  | `raptor`     |                | Raptor code to correct and verify the first 7 symbols of the packet.    |
| `b(length*8)` | `data`       |                | The packet data itself.                                                 |

In case the data needs to be segmented, the following `generic_segment_structure`
template has to be used for segments that follow the above:

| Data              | Name              |    Fixed value | Description                                                                              |
|:------------------|:------------------|---------------:|:-----------------------------------------------------------------------------------------|
| `b(16)`           | `seg_descriptor`  | *as specified* | Indicates the segment type. Defined in later sections along with the data descriptor.    |
| `b(16)`           | `stream_id`       |                | Indicates the stream ID for which this packet is applicable.                             |
| `u(32)`           | `global_seq`      |                | Monotonically incrementing per-packet global sequence number.                            |
| `b(32)`           | `target_seq`      |                | The sequence number of the starting packet.                                              |
| `u(32)`           | `pkt_total_data`  |                | Total number of data bytes, including the first data packet's, and ending segment's.     |
| `u(32)`           | `seg_offset`      |                | The offset since the start of the data where the segment starts.                         |
| `u(32)`           | `seg_length`      |                | The size of the data segment.                                                            |
| `b(32)`           | `header_7`        |                | A seventh of the main packet's header. The part taken is determined by `global_seq % 7`. |
| `R(224, 64)`      | `raptor`          |                | Raptor code to correct and verify the first 7 symbols of the packet.                     |
| `b(seg_length*8)` | `seg_data`        |                | The data for the segment.                                                                |

If the data in a `generic_data_structure` is to be segmented, it will have
a different descriptor. The descriptor in `generic_segment_structure` shall
be different for segments that finalize the data.

Finally, in case the data requires FEC, the following `generic_fec_structure`
is to be used:

| Data                   | Name              |    Fixed value | Description                                                                              |
|:-----------------------|:------------------|---------------:|:-----------------------------------------------------------------------------------------|
| `b(16)`                | `fec_descriptor`  | *as specified* | Indicates this is an FEC segment packet.                                                 |
| `b(16)`                | `stream_id`       |                | Indicates the stream ID for whose packets are being backed.                              |
| `u(32)`                | `global_seq`      |                | Monotonically incrementing per-packet global sequence number.                            |
| `b(32)`                | `target_seq`      |                | The sequence number of the starting packet.                                              |
| `b(32)`                | `fec_data_offset` |                | The byte offset for the FEC data for this FEC packet protects.                           |
| `u(32)`                | `fec_data_length` |                | The length of the FEC data in this packet.                                               |
| `u(32)`                | `fec_total`       |                | The total amount of bytes in the FEC data.                                               |
| `b(32)`                | `header_7`        |                | A seventh of the main packet's header. The part taken is determined by `global_seq % 7`. |
| `R(224, 64)`           | `raptor`          |                | Raptor code to correct and verify the first 7 symbols of the packet.                     |
| `b(fec_data_length*8)` | `fec_data`        |                | The FEC data that can be used to check or correct the previous data packet's payload.    |

The data in an FEC packet MUST be systematic RaptorQ, as per IETF RFC 6330.<br/>
Common FEC Object Transmission Information (OTI) format and Scheme-Specific FEC
Object Transmission Information as described in the document are *never* used.

The symbol size MUST be 32-bits.

The total number of bytes covered by this and previous FEC segments shall be
set in the `fec_covered` field.<br/>
This MUST always be *greater than zero*.

The `header_7` field can be used to reconstruct the header of the very first
packet in order to determine the timestamps and data type.

### Initialization data packets

Codecs generally require a one-off special piece of data needed to initialize them.<br/>
To provide this data to receivers, the templates defined in the
[generic data packet structure](#generic-data-packet-structure) MUST be used,
with the following descriptors:

| Descriptor |                   Structure | Type |
|-----------:|----------------------------:|-------------------------------------------------------------|
|        0x3 |    `generic_data_structure` | Complete codec initialization data.                         |
|        0x4 |    `generic_data_structure` | Incomplete codec initialization data.                       |
|        0x5 | `generic_segment_structure` | Non-final segment for incomplete codec initialization data. |
|        0x6 | `generic_segment_structure` | Final segment for incomplete codec initialization data.     |
|        0x7 |     `generic_fec_structure` | FEC segment for the codec initialization data.              |

For more information on the layout of the specific data, consult the
[codec-specific encapsulation](#codec-encapsulation) addenda.

However, in general, the data follows the same layout as what
[FFmpeg's](https://ffmpeg.org) `libavcodec` produces and requires.<br/>
An implementation MAY error out in case it cannot handle the data in the payload.
If so, when reading a file, it MUST stop, otherwise in a live scenario,
it MUST send an `unsupported` [control data](#control-data), if such a
connection is open.

### Stream data packets

The data packets indicate the start of a stream packet, which may be fragmented into
more [stream data segments](#stream-data-segmentation). It is laid out as follows:

| Data               | Name              | Fixed value | Description                                                                                                                                            |
|:-------------------|:------------------|------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)`            | `data_descriptor` |    0x01\*\* | Indicates this is a stream data packet. The lower 8 bits are used as the `pkt_flags` field.                                                            |
| `b(16)`            | `stream_id`       |             | Indicates the stream ID for this packet.                                                                                                               |
| `u(32)`            | `global_seq`      |             | Monotonically incrementing per-packet global sequence number.                                                                                          |
| `i(64)`            | `pts`             |             | Indicates the presentation timestamp for when this frame SHOULD be presented at. To interpret the value, read the [Timestamps](#timestamps) section.   |
| `u(64)`            | `duration`        |             | The duration of this packet in stream timebase unis.                                                                                                   |
| `u(32)`            | `data_length`     |             | The size of the data in this packet.                                                                                                                   |
| `R(224, 64)`       | `data_raptor`     |             | Raptor code to correct and verify the previous contents of the packet.                                                                                 |
| `b(data_length*8)` | `packet_data`     |             | The packet data itself.                                                                                                                                |

For information on the layout of the specific codec-specific packet data, consult
the [codec-specific encapsulation](#codec-encapsulation) addenda.

The `pkt_flags` field MUST be interpreted in the following way:

| Bit position set | Description                                                                                                                                                                                                  |
|-----------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|             0xE0 | Top 3 bits to be interpreted using the `frame_type` table below.                                                                                                                                             |
|             0x20 | Packet is incomplete and requires extra [data segments](#stream-data-segmentation) to be completed.                                                                                                          |
|              0x8 | User-defined flag. Implementations MUST ignore it, and MUST leave it as-is.                                                                                                                                  |
|              0x3 | Bottom 2 bits to be interpreted using the `data_compression` table below.                                                                                                                                    |

If the `0x40` flag is set, then the packet data is incomplete, and at least ONE
[data segment](#stream-data-segmentation) packet with an ID of `0xfe` MUST be present to
terminate the packet. A packet with an `0x40` flag MUST NOT have a `data_length` equal
to 0.

The `frame_type` table is as follows:
| Value | Name   | Description                                                   |
|------:|:-------|:--------------------------------------------------------------|
|   0x0 | `KEY`  | Packet data contains a keyframe, able to be decoded standalone.                                   |
|   0x1 | `S`    | Packet data contains a scalable frame, able to be decoded standalone with acceptable degradation. |

The `data_compression` table is as follows:
| Value | Name   | Description                                                   |
|------:|:-------|:--------------------------------------------------------------|
|   0x0 | `NONE` | Packet data is uncompressed.                                  |
|   0x1 | `ZSTD` | Packet data is compressed with Zstandard from IETF RFC 8878.  |

### Stream data segmentation

Packets can be split up into separate chunks that may be received out of order
and assembled. This allows transmission over switched networks with a limited MTU,
or prevents very large packets from one stream interfering with another stream.
The packet structure used for segments is the `generic_segment_structure` from
[generic data packet structure](#generic-data-packet-structure), with the
following descriptors:

| Descriptor |                   Structure |                                         Type |
|-----------:|----------------------------:|----------------------------------------------|
|       0xFF | `generic_segment_structure` | Non-final segment for incomplete stream data |
|       0xFE | `generic_segment_structure` |     Final segment for incomplete stream data |

The size of the final assembled packet is the sum of all `seg_length` fields,
plus the `data_length` field from the [stream data packet](#stream-data-packets).

Data segments and packets may arrive out of order and be duplicated.
Implementations MUST reorder them, deduplicate them and assemble them into
complete packets.

Implementations MAY try to decode incomplete data packets with missing segments
due to latency concerns.

Senders MAY send duplicate segments to compensate for packet loss,
but SHOULD use [FEC grouping](#fec-grouping) or
[FEC segments](#fec-segments) instead.

Implementations SHOULD discard any packets and segments that arrive after their
presentation time. Implementations SHOULD drop any packets and segments
that arrive with unrealistically far away presentation times.

### FEC grouping

Whilst it's possible to send uncontextualized FEC data backing individual
packets, for most applications, this is only feasible for very high bitrate
single streams, as modern FEC algorithms are highly optimized for packet erasure
recovery.

FEC grouping allows for multiple buffered packets and segments from multiple
streams to be FEC corrected in order to ensure no stream is starved of data.

FEC grouped streams MUST be registered first via a special packet:

| Data                      | Name                    | Fixed value | Description                                                                                                                                       |
|:--------------------------|:------------------------|------------:|:--------------------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)`                   | `fec_group_descriptor`  |        0x30 | Indicates this is an FEC grouping packet.                                                                                                         |
| `b(16)`                   | `fec_group_id`          |             | Indicates the stream ID for the FEC group.                                                                                                        |
| `u(32)`                   | `global_seq`            |             | Monotonically incrementing per-packet global sequence number.                                                                                     |
| `u(32)`                   | `fec_group_streams`     |             | Number of streams in the FEC group. MUST be less than or equal to 16.                                                                             |
| `b(128)`                  | `padding`               |             | Padding, reserved for future use. MUST be 0x0.                                                                                                    |
| `R(224, 64)`              | `raptor`                |             | Raptor code to correct and verify the first 7 symbols of the packet.                                                                              |
| `u(32*16)`                | `fec_nb_packets`        |             | Total number of packets for each stream in the FEC group.                                                                                         |
| `u(32*16)`                | `fec_seq_number`        |             | The sequence number of the first packet for the stream to be included in the group.                                                               |
| `u(32*16)`                | `fec_nb_data_size`      |             | The total number of bytes, including headers, in each FEC grouped stream included in the duration of the group.                                   |
| `R(1536, 768)`            | `raptor`                |             | Raptor code for the previous 48 symbols.                                                                                                          |

All streams in an FEC group **MUST** have timestamps that cover the same period
of time.

A stream may only be part of a single FEC group at any one time. Sending
a new grouping that includes an already grouped stream will destroy the
previous grouping.

To end a grouping, one can send an end of stream packet with the group's ID.

FEC groups use a different packet for the FEC data.

| Data                   | Name              |    Fixed value | Description                                                                           |
|:-----------------------|:------------------|---------------:|:--------------------------------------------------------------------------------------|
| `b(16)`                | `fec_descriptor`  |           0x31 | Indicates this is an FEC group data packet.                                           |
| `b(16)`                | `stream_id`       |                | Indicates the FEC group ID for whose packets are being backed.                        |
| `u(32)`                | `global_seq`      |                | Monotonically incrementing per-packet global sequence number.                         |
| `b(32)`                | `fec_data_offset` |                | The byte offset for the FEC data for this FEC packet protects.                        |
| `u(32)`                | `fec_data_length` |                | The length of the FEC data in this packet.                                            |
| `u(32)`                | `fec_total`       |                | The total amount of bytes in the FEC data.                                            |
| `b(64)`                | `padding`         |                | Padding, reserved for future use. MUST be 0x0.                                        |
| `R(224, 64)`           | `raptor`          |                | Raptor code to correct and verify the first 7 symbols of the packet.                  |
| `b(fec_data_length*8)` | `fec_data`        |                | The FEC data that can be used to check or correct the previous data packet's payload. |

To perform FEC on a group, first, append all packets, including their headers,
for all streams in incrementing order, removing packets which do not concern the
streams included, and then use the FEC data signalled.

### Stream FEC segments

Stream data packets and segments MAY be backed by FEC data packets.
The structure used for FEC segments follows the `generic_fec_structure` template
from the [generic data packet structure](#generic-data-packet-structure) section,
with the following descriptor:

| Descriptor |                   Structure |                                         Type |
|-----------:|----------------------------:|----------------------------------------------|
|       0xFD |     `generic_fec_structure` |   FEC data segment for the stream data packet|

Implementations MAY discard the FEC data, or MAY delay the previous packet's
decoding to correct it with the FEC data, or MAY attempt to decode the uncorrected
packet data, and if failed, retry with the corrected data packet.

The data in an FEC packet MUST be RaptorQ, as per IETF RFC 6330.<br/>
The symbol size MUST be 32-bits.

The total number of bytes covered by this and previous FEC segments shall be
set in the `fec_covered` field. This MUST be greater than zero, unless
`fec_data_length` is also zero.

The same lifetime and duplication rules apply for FEC segments as they do for
regular data segments.

### Index packets

The index packet contains available byte offsets of nearby keyframes and the
distance to the next index packet.

| Data               | Name               |  Fixed value | Description                                                                                                                                                                    |
|:-------------------|:-------------------|-------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)`            | `index_descriptor` |          0x9 | Indicates this is an index packet.                                                                                                                                             |
| `b(16)`            | `stream_id`        |              | Indicates the stream ID for the index. May be 0xFFFF, in which case, it applies to all streams.                                                                                |
| `u(32)`            | `global_seq`       |              | Monotonically incrementing per-packet global sequence number.                                                                                                                  |
| `u(32)`            | `prev_idx`         |              | Negative offset of the previous index packet, if any, in bytes, relative to the current position. If exactly 0, indicates no such index is available, or is out of scope.      |
| `u(32)`            | `next_idx`         |              | Positive offset of the next index packet, if any, in bytes, relative to the current position. May be inexact, specifying the minimum distance to one. Users may search for it. |
| `u(32)`            | `nb_indices`       |              | The total number of indices present in this packet.                                                                                                                            |
| `b(64)`            | `padding`          |              | Padding, reserved for future use. MUST be 0x0.                                                                                                                                 |
| `R(224, 64)`       | `raptor`           |              | Raptor code to correct and verify the first 7 symbols of the packet.                                                                                                           |
| `i(nb_indices*64)` | `pkt_pts`          |              | The presentation timestamp of the index.                                                                                                                                       |
| `u(nb_indices*32)` | `pkt_seq`          |              | The sequence number of the packet pointed to by `pkt_pos`. MUST be ignored if `pkt_pos` is 0.                                                                                  |
| `i(nb_indices*32)` | `pkt_pos`          |              | The offset of a decodable index relative to the current position in bytes. MAY be 0 if unavailable or not applicable.                                                          |
| `u(nb_indices*16)` | `pkt_chapter`      |              | If a value is greater than 0, demarks the start of a chapter with an index equal to the value.                                                                                 |

If valid, `prev_idx`, `next_idx` and `pkt_pos` offsets MUST point to the start of
a packet. In other words, the byte pointed to by the offsets MUST contain the
first, most significant byte of the descriptor of the pointed packet.

If `stream_id` is 0xFFFF, the timebase used for `idx_pts` MUST be assumed to be
**1 nanosecond**, numerator of `1`, denominator of `1000000000`.

If a packet starts before the value of `pkt_pts` but has a duration that
matches or exceeds the PTS, then it MUST be included. This is to permit
correct subtitle presentation, or long duration still pictures like slideshows.

When streaming, `idx_pos`, `prev_idx` and `next_idx` MUST be `0`.

### Metadata packets

The metadata packets can be sent for the overall session, or for a specific
`stream_id` substream. The data is contained in structures templated in the
[generic data packet structure](#generic-data-packet-structure),
with the following descriptors:

| Descriptor |                   Structure |                                      Type |
|-----------:|----------------------------:|------------------------------------------:|
|        0xA |    `generic_data_structure` |                  Complete metadata packet |
|        0xB |    `generic_data_structure` |                       Incomplete metadata |
|        0xC | `generic_segment_structure` | Non-final segment for incomplete metadata |
|        0xD | `generic_segment_structure` |     Final segment for incomplete metadata |
|        0xE |     `generic_fec_structure` |              FEC segment for the metadata |

The actual metadata MUST be stored using BARE, IETF `draft-devault-bare-07`.
The schema to use MUST be as follows:

```
type MetadataSet struct {
    title: optional<str>
    stream_id: optional<u16>
    language: optional<str>
    track: optional<i32>
    tracks: optional<i32>
    artist: optional<str>
    album_artist: optional<str>
    album: optional<str>
    date: optional<str>
    tags: list< struct {
        name: str
        type Value union {
            | str | bool | f32 | f64 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 | int | uint | data
        }
    }>
}

type Metadata list<MetadataSet>
```

Implementations can either send the data via a single `Metadata` struct,
or as an array via `MetadataSet`.
Each `stream_id` value in a `MetadataSet` MUST be unique.

Each key `name` CAN be encountered multiple times. Implementations MUST
discard the old `Value` associated with the key.
If `stream_id` is present, the metadata is a separate set of values that just
describe a single stream. Tags from other streams, or the general file
metadata MUST NOT overwrite each other.

If `stream_id` is omitted or equal to `0xFFFF`,
the metadata applies for the session as a whole.

Metadata can be updated by sending new metadata packets with new values.

The values listed outside of `tags` SHOULD be interpreted as recommendations.
If a `name` matches one of those values, the value outside of the `tags` field
MUST take priority.

The `language` field MUST be formatted as a **subtag**, according to the
[IETF BPC 47](https://www.iana.org/assignments/language-subtag-registry/language-subtag-registry).

The `date` field MUST be formatted according to the
[IETF RFC 3339](https://datatracker.ietf.org/doc/html/rfc3339).

### ICC profile packets

Embedding of ICC profiles for accurate color reproduction is supported.<br/>
The following structure MUST be followed:

| Data                   | Name              |    Fixed value | Description                                                                                          |
|:-----------------------|:------------------|---------------:|:-----------------------------------------------------------------------------------------------------|
| `b(16)`                | `icc_descriptor`  |  0x10 and 0x11 | Indicates this packet contains a complete ICC profile (0x10) or the start of a segmented one (0x11). |
| `u(16)`                | `stream_id`       |                | The stream ID for which to apply the ICC profile.                                                    |
| `u(32)`                | `global_seq`      |                | Monotonically incrementing per-packet global sequence number.                                        |
| `u(8)`                 | `icc_major_ver`   |                | Major version of the ICC profile. MAY be 0 if unknown.                                               |
| `u(8)`                 | `icc_minor_ver`   |                | Minor version of the ICC profile.                                                                    |
| `u(8)`                 | `icc_compression` |                | ICC profile compression, MUST be interpreted according to the `icc_compression` table below.         |
| `u(8)`                 | `icc_name_length` |                | The length of the ICC profile name.                                                                  |
| `u(32)`                | `icc_data_length` |                | The length of the ICC profile.                                                                       |
| `b(96)`                | `padding`         |                | Padding, reserved for future use. MUST be 0x0.                                                       |
| `R(224, 64)`           | `raptor`          |                | Raptor code to correct and verify the first 7 symbols of the packet.                                 |
| `b(icc_name_length*8)` | `icc_name`        |                | The full name of the ICC profile.                                                                    |
| `b(icc_data_length*8)` | `icc_data`        |                | The ICC profile itself.                                                                              |

Often, ICC profiles may be too large to fit in one MTU, hence they can be segmented
in the same way as data packets, as well as be error-corrected. The syntax for
ICC segments and FEC packets is via the following
[generic data packet structure](#generic-data-packet-structure) templates:

| Descriptor |                   Structure |                               Type |
|-----------:|----------------------------:|-----------------------------------:|
|       0x12 | `generic_segment_structure` | Non-final segment for ICC profiles |
|       0x13 | `generic_segment_structure` |     Final segment for ICC profiles |
|       0x14 |     `generic_fec_structure` |    FEC segment for the ICC Profile |

If the `icc_descriptor` is `0x11`, then at least one `0x13` segment MUST be received.

The `icc_compression` table is as follows:
| Value | Name   | Description                                |
|------:|:-------|:-------------------------------------------|
|   0x0 | `NONE` | ICC data is uncompressed.                  |
|   0x1 | `ZSTD` | ICC data is compressed with Zstandard.     |

**NOTE**: ICC profiles MUST take precedence over the primaries and transfer
characteristics values in [video info packets](#video-info-packets). The matrix
coefficients are still required for RGB conversion.

### Font data packets

Subtitles may often require custom fonts. Qproto supports embedding of fonts
for use by subtitles.<br/>
The following structure MUST be followed:

| Data                    | Name               |    Fixed value | Description                                                                                   |
|:------------------------|:-------------------|---------------:|:----------------------------------------------------------------------------------------------|
| `b(16)`                 | `font_descriptor`  |  0x20 and 0x21 | Indicates this packet contains a complete font (0x10) or the start of a segmented one (0x11). |
| `b(16)`                 | `stream_id`        |                | Unique stream ID to identify the font.                                                        |
| `u(32)`                 | `global_seq`       |                | Monotonically incrementing per-packet global sequence number.                                 |
| `u(16)`                 | `font_type`        |                | Font type. MUST be interpreted according to the `font_type` table below.                      |
| `u(8)`                  | `font_compression` |                | Font compression, MUST be interpreted according to the `font_compression` table below.        |
| `u(8)`                  | `font_name_length` |                | The length of the font name.                                                                  |
| `u(32)`                 | `font_data_length` |                | The length of the font data.                                                                  |
| `b(96)`                 | `padding`          |                | Padding, reserved for future use. MUST be 0x0.                                                |
| `R(224, 64)`            | `raptor`           |                | Raptor code to correct and verify the first 7 symbols of the packet.                          |
| `b(font_name_length*8)` | `font_name`        |                | The full name of the font file.                                                               |
| `b(font_data_length*8)` | `font_data`        |                | The font file itself.                                                                         |

The syntax for font segments and FEC packets is via the following
[generic data packet structure](#generic-data-packet-structure) templates:

| Descriptor |                   Structure | Type                        |
|-----------:|----------------------------:|:----------------------------|
|       0x22 | `generic_segment_structure` | Non-final segment for font. |
|       0x23 | `generic_segment_structure` | Final segment for font.     |
|       0x24 |     `generic_fec_structure` | FEC segment for the font.   |

If the `font_descriptor` is `0x21`, then at least one `0x23` segment MUST be received.

The `font_type` table is as follows:
| Value | Name    | Description                                 |
|------:|:--------|:--------------------------------------------|
|   0x0 | `OTF`   | Font data is an OpenType font.              |
|   0x1 | `TTF`   | Font data is a TrueType font.               |
|   0x2 | `WOFF2` | Font data contains Web Open Font Format 2.  |

The `font_compression` table is as follows:
| Value | Name   | Description                                 |
|------:|:-------|:--------------------------------------------|
|   0x0 | `NONE` | Font data is uncompressed.                  |
|   0x1 | `ZSTD` | Font data is compressed with Zstandard.     |

*WOFF2* fonts SHOULD NOT be compressed, as they're already compressed.

### Video info packets

Video info packets contain everything needed to correctly interpret a video
stream after decoding.

| Data            | Name                      | Fixed value  | Description                                                                                                                                   |
|:----------------|:--------------------------|-------------:|:----------------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)`         | `video_info_descriptor`   |          0x8 | Indicates this packet contains video information.                                                                                             |
| `u(16)`         | `stream_id`               |              | The stream ID for which to associate the video information with.                                                                              |
| `u(32)`         | `global_seq`              |              | Monotonically incrementing per-packet global sequence number.                                                                                 |
| `u(32)`         | `width`                   |              | Indicates the presentable video width in pixels.                                                                                              |
| `u(32)`         | `height`                  |              | Indicates the presentable video height in pixels.                                                                                             |
| `r(64)`         | `signal_aspect`           |              | Indicates the signal aspect ratio of the image.                                                                                               |
| `u(8)`          | `chroma_subsampling`      |              | Indicates the chroma subsampling being used. MUST be interpreted using the `subsampling` table below.                                         |
| `u(8)`          | `colorspace`              |              | Indicates the kind of colorspace the video is in. MUST be interpreted using the `colorspace` table below.                                     |
| `u(8)`          | `bit_depth`               |              | Number of bits per pixel value.                                                                                                               |
| `u(8)`          | `interlaced`              |              | Video data is interlaced. MUST be interpreted using the `interlacing` table below.                                                            |
| `R(224, 64)`    | `raptor`                  |              | Raptor code to correct and verify the first 7 symbols of the packet.                                                                          |
| `r(64)`         | `gamma`                   |              | Indicates the gamma power curve for the video pixel values.                                                                                   |
| `r(64)`         | `framerate`               |              | Indicates the framerate. If it's variable, MAY be used to indicate the average framerate. If video is interlaced, indicates the *field* rate. |
| `u(16)`         | `limited_range`           |              | Indicates the signal range. If `0x0`, means `full range`. If `0xFFFF` means `limited range`. Other values are reserved.                       |
| `u(8)`          | `chroma_pos`              |              | Chroma sample position for subsampled chroma. MUST be interpreted using the `chroma_pos_val` table below.                                     |
| `u(8)`          | `primaries`               |              | Video color primaries. MUST be interpreted according to ITU Standard H.273, `ColourPrimaries` field.                                          |
| `u(8)`          | `transfer`                |              | Video transfer characteristics. MUST be interpreted according to ITU Standard H.273, `TransferCharacteristics` field.                         |
| `u(8)`          | `matrix`                  |              | Video matrix coefficients. MUST be interpreted according to ITU Standard H.273, `MatrixCoefficients` field.                                   |
| `u(8)`          | `has_mastering_primaries` |              | If `1`, signals that the following `mastering_primaries` and `mastering_white_point` contain valid data.                                      |
| `u(8)`          | `has_luminance`           |              | If `1`, signals that the following `min_luminance` and `max_luminance` fields contain valid data.                                             |
| `16*r(64)`      | `custom_matrix`           |              | If `matrix` is equal to `0xFF`, use this custom matrix instead. Top, left, to bottom right, raster order.                                     |
| `6*r(64)`       | `mastering_primaries`     |              | CIE 1931 xy chromacity coordinates of the color primaries, `r`, `g`, `b` order.                                                               |
| `2*r(64)`       | `mastering_white_point`   |              | CIE 1931 xy chromacity coordinates of the white point.                                                                                        |
| `r(64)`         | `min_luminance`           |              | Minimal luminance of the mastering display, in cd/m<sup>2</sup>.                                                                              |
| `r(64)`         | `max_luminance`           |              | Maximum luminance of the mastering display, in cd/m<sup>2</sup>.                                                                              |
| `b(64)`         | `padding`                 |              | Padding, reserved for future use. MUST be 0x0.                                                                                                |
| `R(1920, 640)`  | `raptor`                  |              | Raptor code to correct and verify the data from `colorspace` to `max_luminance`.                                                              |

Note that `full range` has many synonyms used - `PC range`, `full swing` and `JPEG range`.<br/>
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
|   0x6 | `XYB`       | Video contains XYB color data. **NOTE**: `matrix` MUST be equal to 0xFF and the matrix in `custom_matrix` MUST be valid. |
|   0x7 | `ICTCP`     | Video contains ICtCp color data.                                                                                         |

The `subsampling` table is as follows:
| Value | Name        | Description                                                                                        |
|------:|:------------|:---------------------------------------------------------------------------------------------------|
|   0x0 | `444`       | Chromatic data is not subsampled, or subsampling does not apply.                                   |
|   0x1 | `420`       | Chromatic data is subsampled at half the horizontal and vertical resolution of the luminance data. |
|   0x2 | `422`       | Chromatic data is subsampled at half the vertical resolution of the luminance data.                |

The `interlacing` table is as follows:
| Value | Name        | Description                                                                                                                                                                                                      |
|------:|:------------|:-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|   0x0 | `PROG`      | Video contains progressive data, or interlacing does not apply.                                                                                                                                                  |
|   0x1 | `TFF`       | Video is interlaced. One [data packet](#stream-data-packets) per field. If the data packet's `pkt_flags & 0x20` bit is **unset**, indicates the field contained is the **top** field, otherwise it's the bottom. |
|   0x2 | `BFF`       | Same as `TFF`, with reversed polarity, such that packets with `pkt_flags & 0x20` bit **set** contain the **top** field, otherwise it's the bottom.                                                               |
|   0x3 | `TW`        | Video is interlaced. The [data packet](#stream-data-packets) contains **both** fields, weaved together, with the **top** field being on every even line.                                                         |
|   0x4 | `BW`        | Same as `TW`, but with reversed polarity, such that the **bottom** field is encountered first.                                                                                                                   |

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
| Luma line number |         Luma row 1 | Between rows |   Luma row 2 |
|-----------------:|-------------------:|-------------:|-------------:|
|                1 | `Luma pixel` **3** |        **4** | `Luma pixel` |
|    Between lines |              **1** |        **2** |              |
|                2 | `Luma pixel` **5** |        **6** | `Luma pixel` |

### Video orientation packets

A standardized way to transmit orientation information is as follows:

| Data                    | Name               | Fixed value  | Description                                                                                  |
|:------------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------|
| `b(16)`                 | `ori_descriptor`   |         0x40 | Indicates this is a video orientation packet.                                                |
| `b(16)`                 | `stream_id`        |              | The stream ID for which to associate the video information with.                             |
| `u(32)`                 | `global_seq`       |              | Monotonically incrementing per-packet global sequence number.                                |
| `r(64)`                 | `rotation`         |              | A fixed-point rational number to indicate rotation in radians once multiplied by `π`.        |
| `b(96)`                 | `padding`          |              | Padding, reserved for future use. MUST be 0x0.                                               |
| `R(224, 64)`            | `raptor`           |              | Raptor code to correct and verify the first 7 symbols of the packet.                         |

Video orientation packets SHOULD be taken into account and be applied upon
the next video packet in the bitstream, but not before.
The value MUST persist until a new orientation packet is sent, OR the stream
is reinitialized.

The actual rotation in radians is given by `π * (rotation.num/rotation.den)`.

The value MAY be quantized to a modulus of `π/2` (`rotation.num/rotation.den % 0.5 = 0`)
BY the receiver, in order to permit simple transposition for presentation rather
than rotation.

### User data packets

The user-specific data packet is laid out as follows:

| Data                    | Name               | Fixed value  | Description                                                                                  |
|:------------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------|
| `b(16)`                 | `user_descriptor`  |     0x40\*\* | Indicates this is an opaque user-specific data. The bottom byte is included and free to use. |
| `b(16)`                 | `user_field`       |              | A free to use field for user data.                                                           |
| `u(32)`                 | `global_seq`       |              | Monotonically incrementing per-packet global sequence number.                                |
| `u(32)`                 | `user_data_length` |              | The length of the user data.                                                                 |
| `b(128)`                | `padding`          |              | Padding, reserved for future use. MUST be 0x0.                                               |
| `R(224, 64)`            | `raptor`           |              | Raptor code to correct and verify the first 7 symbols of the packet.                         |
| `b(user_data_length*8)` | `user_data`        |              | The user data itself.                                                                        |

If the user data needs FEC and segmentation, users SHOULD instead use the
[custom codec packets](#custom-codec-encapsulation).

### Stream duration packets

If the session length is well-known, implementations can reserve space up-front
at the start of files to notify implementations of stream lengths.

| Data         | Name                  |  Fixed value | Description                                                                                     |
|:-------------|:----------------------|-------------:|:------------------------------------------------------------------------------------------------|
| `b(16)`      | `duration_descriptor` |       0xF000 | Indicates this is an index packet.                                                              |
| `b(16)`      | `stream_id`           |              | Indicates the stream ID for the index. May be 0xFFFF, in which case, it applies to all streams. |
| `u(32)`      | `global_seq`          |              | Monotonically incrementing per-packet global sequence number.                                   |
| `i(64)`      | `total_duration`      |              | The total duration of the stream(s).                                                            |
| `b(96)`      | `padding`             |              | Padding, reserved for future use. MUST be 0x0.                                                  |
| `R(224, 64)` | `raptor`              |              | Raptor code to correct and verify the first 7 symbols of the packet.                            |

If `stream_id` is 0xFFFF, the timebase used for `idx_pts` MUST be assumed to be
**1 nanosecond**, numerator of `1`, denominator of `1000000000`.

If the value of `total_duration` is 0, the entire packet MUST be ignored.
In such cases, implementations are free to attempt to measure stream duration
via other methods.<br/>
This makes it possible to write stream duration packets at the start of
streams, and amend them later.

The duration MUST be the total amount of time the stream will be presented.<br/>
Any negative duration MUST be excluded.

The duration MUST be treated as metadata rather than a hard limit.

### End of stream

The EOS packet is laid out as follows:

| Data         | Name             | Fixed value  | Description                                                                                             |
|:-------------|:-----------------|-------------:|:--------------------------------------------------------------------------------------------------------|
| `b(16)`      | `eos_descriptor` |       0xFFFF | Indicates this is an end-of-stream packet.                                                              |
| `b(16)`      | `stream_id`      |              | Indicates the stream ID for the end of stream. May be 0xFFFF, in which case, it applies to all streams. |
| `u(32)`      | `global_seq`     |              | Monotonically incrementing per-packet global sequence number.                                           |
| `b(160)`     | `padding`        |              | Padding, reserved for future use. MUST be 0x0.                                                          |
| `R(224, 64)` | `raptor`         |              | Raptor code to correct and verify the first 7 symbols of the packet.                                    |

The `stream_id` field may be used to indicate that a specific stream will no longer
receive any packets, and implementations are free to unload decoding and free up
any used resources.

The `stream_id` MAY be reused afterwards, but this is not recommended.

If not encountered in a stream, and the connection was cut, then the receiver is
allowed to gracefully wait for a reconnection.

If encountered in a file, the implementation MAY regard any data present afterwards
as padding and ignore it. Qproto files MUST NOT be concatenated.

## Timestamps

Qproto supports high resolution timestamps, with a maximum resolution of
465.66129 **picoseconds**. Furthermore, the reliability of the timestamps
can be assured through jitter compenstation.

**All** time-related fields (`pts`, `dts` and `duration`) have a corresponding
`timebase` field with which to interpret them.

The mathematical expression to calculate the time `t`, in seconds, of a timestamp
or duration `v` and a *rational* timebase `b` is the following:

```
t = v * b.num / b.den
```

The timebase of [stream data](#stream-data-packets), [stream duration](#stream-duration-packets),
or any other packets with an explicit `stream_id` field is given in the
[Stream registration](#stream-registration-packets) packets for the appropriate stream.

The time `t` of a `pts` field corresponds to the exact time when a packet must be
instantaneously released from a decoding buffer, and instantaneously presented.

The time `t` of a `dts` field corresponds to the exact time when a packet must be
input into a synchronous 1-in-1-out decoder.

The time `t` of a `duration` field corresponds to the exact time difference between
two **consecutive** frames of audio or video. If the field is non-zero, this assertion
MUST hold. If this condition is violated, the behavior is **UNSPECIFIED**.

For subtitle frames, the `duration` field's definition is different. It specifies the
time during which the contents of the current packet MUST be presented. It MAY not
match a video frame's duration.

### Jitter compensation

Implementations SHOULD use the derived `ts_clock_freq` field from
[Time synchronization](#time-synchronization-packets) packets
to perform jitter compensation of stream timestamps.

**NOTE**: The only valid targets to perform timestamp jitter compensation are
streams with the same timebase, which MUST BE equal to the inverse of `ts_clock_freq`.
Jitter compensation is otherwise undefined.

As the `ts_clock_freq` field defines a strictly monotonic clock signal with a rate of `ts_clock_freq`,
which atomically increments a counter, `ts_clock_seq` on the **rising** edge of the waveform, this
can be used to compensate the `pts`, `dts` and `duration` values of stream packets.

The following is a suggestion for implementations:
 - Initialize a phase-locked loop, with a frequency equal to `ts_clock_freq`.
 - Initialize a counter, `local_ts_clock_seq`, to be equal to `ts_clock_seq`.
 - Increment the `local_ts_clock_seq` on the **rising** edge of the local oscillator's waveform.

On every received packet:
 - Measure the difference `Δ` between `pts` and `local_ts_clock_seq`.
 - If `Δ` is small, assume `pts` has jitter, and replace it with `local_ts_clock_seq`
 - Otherwise, add `Δ` to `local_ts_clock_seq`.

This is a very simple example which depends on the local receiver oscillator simply being
*more precise* than the transmitter's oscillator.

### Negative times

The time `t` of a `pts` MAY be negative. Packets with a negative timestamps
**MUST** be decoded, but **NOT** presented.

For audio packets, this corresponds to the internal algorithmic delay between
the first output sample and the correct sample being available.</br>
For audio, the time between `t` and `0` MAY not be a multiple of the
audio data's `duration` of a packet. Implementations MUST nevertheless remove any
decoded samples with a negative time.

Negative `pts` values ARE allowed, and implementations MUST decode such frames,
however MUST NOT present any such frames unless `pts + duration` is greater than 0,
in which case they MUST present the data required for that duration.<br/>
This enables removal of extra samples added during audio compression, as well
as permitting video segments taken out of context from a stream to bundle
all dependencies (other frames) required for their presentation.

### Epoch

The `epoch` field of [time synchronization](#time-synchronization-packets) packets
is an optional field.

If non-zero, it MUST signal the exact time, in nanoseconds, since
00:00:00 UTC on 1 January 1970, according to the transmitter's wall clock time,
**of the stream starting**. If a start time is not known, it MUST be zero.
Once a stream has started, it MUST NOT be changed.

The user is allowed to handle the field in the following ways:

| Action          | Description                                                                                              |
|:----------------|:---------------------------------------------------------------------------------------------------------|
| Use as metadata | The `epoch` field is used to override [metadata](#metadata-packets)'s `date` field.                      |
| Ignore          | The field is completely ignored.                                                                         |
| Stream duration | The field is used to measure the total time the stream has been operational.                             |
| Stream latency  | The field is used to measure latency.                                                                    |
| Synchronization | The field is used to synchronize several unconnected receivers.                                          |

By default, implementations MUST use the field as metadata, if present. Otherwise, they SHOULD ignore it.

Users are free to use the field to measure the stream duration by converting the `t` of the `pts`
field of any stream's packet to nanoseconds (multiply by `1000000000`), rounding it, and subtracting
it from the `epoch` time (`t - epoch`).

Users are also free to measure the latency in a similar way, by measuring their current wallclock time
`t'`, and using `Δ = t' - t - epoch`. `Δ` will be the delay in nanoseconds.

Finally, if **explicitly requested**, implementations are allowed to *delay* presentation by interpreting
a negative latency value of `Δ` as a delay.</br>
**NOTE**: packets which had a negative timestamp before MUST still be dropped to permit for correct decoding
and presentation.

Note that `stream latency` and `synchronization` actions depend on all devices having accurately synchronized
clocks. This protocol does not guarantee, nor specify this, as this is outside its scope. Users can use the
Network Time Protocol (NTP), specified in IETF RFC 5905, or any other protocol to synchronize clocks, or
simply assume all device are already synchronized.

## Streaming

This section describes and suggests behavior for realtime Qproto streams.

In general, implementations **SHOULD** emit the following packet types
at the given frequencies.

|                                           Packet type | Suggested frequency  | Reason                                                                                          |
|------------------------------------------------------:|:---------------------|:------------------------------------------------------------------------------------------------|
|               [Session start](#session-start-packets) | Target startup delay | To identify a stream as Qproto without ambiguity.                                               |
| [Time synchronization](#time-synchronization-packets) | Target startup delay | Optional time synchronization field to establish an epoch and do timestamp jitter compensation. |
|   [Stream registration](#stream-registration-packets) | Target startup delay | Register streams to permit packet processing.                                                   |
|      [Stream initialization data](#init-data-packets) | Target startup delay | To initialize decoding of stream packets.                                                       |
|              [Video information](#video-info-packets) | Target startup delay | To correctly present any video packets.                                                         |
|                   [ICC profile](#icc-profile-packets) | Target startup delay | Optional ICC profile for correct video presentation.                                            |
|       [Video orientation](#video-orientation-packets) | Target startup delay | Video orientation.                                                                              |
|                         [Metadata](#metadata-packets) | Target startup delay | Stream metadata.                                                                                |
|                        Stream data or segment packets | Always               |                                                                                                 |
|                    [Stream FEC](#stream-fec-segments) | FEC length           | Optional FEC data for stream data.                                                              |
|                       [User data](#user-data-packets) | As necessary         | Optional user data packets.                                                                     |
|                       [End of stream](#end-of-stream) | Once                 | Finalizes a stream.                                                                             |

In particular, [time synchronization](#time-synchronization-packets) packets should
be sent as often as necessary if timestamp jitter avoidance is a requirement.

### UDP

To adapt Qproto for streaming over UDP is trivial - simply send the data packets
as-is specified, with no changes required. The sender implementation SHOULD
resent [session start](#session-start-packets),
[time synchronization](#time-synchronization-packets) packets, as well as
all stream registration, initialization and video info packets for all streams
at most as often as the stream with the lowest frequency of `keyframe`s in order
to permit for implementations that didn't catch on the start of the stream begin
decoding.

UDP mode is unidirectional, but the implementations are free to use the
[reverse signalling](#reverse-signalling) data if they negotiate it themselves.<br/>
Reverse signalling MUST NOT be used if the connection is insecure.

Implementations MUST segment the data such that the network MTU is never
exceeded and no packet fragmentation occurs.<br/>
The **minimum** network MTU required for the protocol is **384 bytes**,
as to allow [video information packets](#video-info-packets) or any future large
packets to be sent without fragmentation.

Jumbograms MAY be used where supported to reduce overhead and increase efficiency.

Data packets MAY be padded by appending random data or zeroes after the `packet_data`
field up to the maximum MTU size. This permits constant bitrate operation,
as well as preventing metadata leakage in the form of a packet size.

### UDP-Lite

UDP-Lite (IETF RFC 3828) SHOULD be preferred to UDP, if support for it is
available throughout the network.<br/>
When using UDP-Lite, the same considerations as [UDP](#udp) apply.

As UDP-Lite allows for variable checksum coverage, the **minimum** checksum
coverage MUST be used, where only the header (8 bytes) is checksummed.

Implementations SHOULD insert adequate FEC information, and receivers SHOULD
correct data with it, as packet integrity guarantees are off.

### QUIC

Qproto tries to use as much of the modern conveniences of QUIC (IETF RFC 9000)
as possible. As such, it uses both reliable and unreliable streams, as well
as bidirectionality features of the transport mechanism.

All data packets with descriptors `0x01**`, `0xFF`, `0xFE`, `0xFD` and `0xFC`
MUST be sent over in an *unreliable* QUIC DATAGRAM stream, as per
IETF RFC 9221.<br/>
All other packets MUST be sent over a reliable steam. FEC data for those packets
SHOULD NOT be signalled.

Implementations MUST segment the data such that the network MTU is never
exceeded and no packet fragmentation occurs.<br/>
The **minimum** network MTU required for the protocol is **384 bytes**,
as to allow [video information packets](#video-info-packets) or any future large
packets to be sent without fragmentation.

Jumbograms MAY be used where supported to reduce overhead and increase efficiency.

## Reverse signalling

The following section is a specification on how reverse signalling, where
receiver(s) communicates with the sender, should be formatted.

The same port an method MUST be used for reverse signalling as the sender's.

### Control data

The receiver can use this type to return errors and more to the sender in a
one-to-one transmission. The following syntax is used:

| Data     | Name               | Fixed value  | Description                                                                                              |
|:---------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------------------|
| `b(16)`  | `ctrl_descriptor`  |       0x8001 | Indicates this is a control data packet.                                                                 |
| `b(8)`   | `cease`            |              | If not equal to `0x0`, indicates a fatal error, and senders MUST NOT sent any more data.                 |
| `b(8)`   | `resend_init`      |              | If nonzero, asks the sender to resend `time_sync` and all stream `init_data` packets.                    |
| `u(32)`  | `error`            |              | Indicates an error code, if not equal to `0x0`.                                                          |
| `b(128)` | `uplink_ip`        |              | Reports the upstream address to stream to.                                                               |
| `b(16)`  | `uplink_port`      |              | Reports the upstream port to stream on to the `uplink_ip`.                                               |
| `b(8)`   | `seek`             |              | If `1`, Asks the sender to seek to the position given by `seek_pts` and/or `seek_seq`.                   |
| `i(64)`  | `seek_pts`         |              | The PTS value to seek to, as given by [index_packets](#index-packets).                                   |
| `u(32)`  | `seek_seq`         |              | The sequence number of the packet to seek to, as given by [index_packets](#index-packets).               |

If the sender gets such a packet, and either its `uplink_ip` or its `uplink_port`
do not match, the sender **MUST** cease this connection, reopen a new connection
with the given `uplink_ip`, and resend all packets needed to begin decoding
to the new destination.

The `seek` request asks the sender to begin sending old data that is still
available. The sender MAY not comply if that data suddenly becomes unavailable.
If the value of `seek` is equal to `0`, then the receiver MUST comply
and always start sending the newest data.

When seeking, BOTH `seek_pts` and `seek_seq` MUST be valid, and MUST have come
from an index packet.

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

### Feedback

The following packet MAY be sent from the receiver to the sender.

| Data    | Name               | Fixed value  | Description                                                                                                                                                                                                                                         |
|:--------|:-------------------|-------------:|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `b(16)` | `stats_descriptor` |       0x8002 | Indicates this is a statistics packet.                                                                                                                                                                                                              |
| `b(16)` | `stream_id`        |              | Indicates the stream ID for which this packet is relevant to. MAY be `0xFFFF` to indicate all streams.                                                                                                                                              |
| `b(64)` | `epoch_offset`     |              | Time since `epoch`. MAY be 0. Can be used to estimate the latency.                                                                                                                                                                                  |
| `b(64)` | `bandwidth`        |              | Hint that indicates the available receiver bandwith, in bits per second. MAY be 0, in which case *infinite* MUST be assumed. Senders SHOULD respect it. The figure should include all headers and associated overhead.                              |
| `u(32)` | `fec_corrections`  |              | A counter that indicates the total amount of repaired packets (packets with errors that FEC was able to correct).                                                                                                                                   |
| `u(32)` | `corrupt_packets`  |              | Indicates the total number of corrupt packets. If FEC was enabled for the stream, this MUST be set to the total number of packets which FEC was not able to repair.                                                                                 |
| `u(32)` | `dropped_packets`  |              | Indicates the total number of dropped [packets](#data-packets).                                                                                                                                                                                     |

Receivers SHOULD send out a new statistics packet every time a count was updated.

If the descriptor of the dropped packed is known, receivers SHOULD set it
in `dropped_type`, and senders SHOULD resend it as soon as possible.

### Resend

The following packet MAY be sent to ask the client to resend a recent packet
that was likely dropped.

| Data    | Name                | Fixed value  | Description                                                           |
|:--------|:--------------------|-------------:|:----------------------------------------------------------------------|
| `b(16)` | `resend_descriptor` |       0x8003 | Indicates this is a stream control data packet.                       |
| `b(16)` | `reserved`          |              | Reserved for future use.                                              |
| `u(32)` | `global_seq`        |              | The sequence number of the packet that is missing.                    |

### Stream control

The receiver can use this type to subscribe or unsubscribe from streams.

| Data    | Name               | Fixed value  | Description                                                                      |
|:--------|:-------------------|-------------:|:---------------------------------------------------------------------------------|
| `b(16)` | `strm_descriptor`  |       0x8004 | Indicates this is a stream control data packet.                                  |
| `b(16)` | `stream_id`        |              | The stream ID for which this packet applies to. MUST NOT be `0xFFFF`.            |
| `b(8)`  | `disable`          |              | If `1`, asks the sender to not send any packets relating to `stream_id` streams. |

This can be used to save bandwidth. If previously `disabled` and then `enabled`,
all packets necessary to initialize the stream MUST be resent.

### Reverse user data

This is identical to the [user data packets](#user-data-packets), but with a different ID.

| Data                    | Name               | Fixed value  | Description                                                                                  |
|:------------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------|
| `b(16)`                 | `user_descriptor`  |     0x50\*\* | Indicates this is an opaque user-specific data. The bottom byte is included and free to use. |
| `u(32)`                 | `user_data_length` |              | The length of the user data.                                                                 |
| `b(user_data_length*8)` | `user_data`        |              | The user data itself.                                                                        |

## Addendum

### Codec encapsulation
The following section lists the supported codecs, along with their encapsulation
definitions.

 - [Opus](#opus-encapsulation)
 - [AAC](#aac-encapsulation)
 - [AV1](#av1-encapsulation)
 - [H.264](#h264-encapsulation)
 - [Dirac/VC-2](#diracvc-2)
 - [ASS](#ass-encapsulation)
 - [Raw audio](#raw-audio-encapsulation)
 - [Raw video](#raw-video-encapsulation)
 - [Custom](#custom-codec-encapsulation)

### Opus encapsulation

For Opus encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x4F707573 (`Opus`).

The payload in the [stream initialization data](#init-data-packets) MUST be
laid out in the following way:

| Data    | Name              | Fixed value                     | Description                                                                           |
|:--------|:------------------|--------------------------------:|:--------------------------------------------------------------------------------------|
| `b(64)` | `opus_descriptor` | 0x4F70757348656164 (`OpusHead`) | Opus magic string.                                                                    |
| `b(8)`  | `opus_init_ver`   |                             0x1 | Version of the extradata. MUST be 0x1.                                                |
| `u(8)`  | `opus_channels`   |                                 | Number of audio channels.                                                             |
| `u(16)` | `opus_prepad`     |                                 | Number of samples to discard from the start of decoding (encoder delay).              |
| `b(32)` | `opus_rate`       |                                 | Samplerate of the data. MUST be 48000.                                                |
| `i(16)` | `opus_gain`       |                                 | Volume adjustment of the stream. May be 0 to preserve the volume.                     |
| `u(32)` | `opus_ch_family`  |                                 | Opus channel mapping family. Consult IETF RFC 7845 and RFC 8486.                      |

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
 - A single `b(64)` element with the `dts`, the time, which indicates when a
   frame should be input into a synchronous 1-in-1-out decoder.
 - Raw `NAL` elements, concatenated.

`Annex-B` formatted packets MUST NOT be used.

### Dirac/VC-2

For Dirac or VC-2 encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x42424344 (`BBCD`).

Dirac streams require no [stream initialization data](#init-data-packets)
packets, hence [Stream registration](#stream-registration-packets) `stream_flags`
MUST have bit `0x1` set.

The `packet_data` MUST contain raw sequences,
with one sequence being a picture.

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

First, all data MUST be converted to UTF-8.<br/>

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
| `b(16)`            | `ra_channels`    | The number of channels contained OR ambisonics data if `ra_ambi == 0x1`.               |
| `b(8)`             | `ra_ambi`        | Boolean flag indicating that samples are ambisonics (`0x1`) or plain channels (`0x0`). |
| `b(8)`             | `ra_bits`        | The number of bits for each sample.                                                    |
| `b(8)`             | `ra_float`       | The data contained is floats (`0x1`) or integers (`0x0`).                              |
| `b(ra_channels*8)` | `ra_pos`         | The coding position for each channel.                                                  |

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
| `u(32)`               | `rv_width`         | The number of horizontal pixels the video stream contains.                                                                              |
| `u(32)`               | `rv_height`        | The number of vertical pixels the video stream contains.                                                                                |
| `u(8)`                | `rv_components`    | The number of components the video stream contains.                                                                                     |
| `u(8)`                | `rv_planes`        | The number of planes the video components are placed in.                                                                                |
| `u(8)`                | `rv_bpp`           | The number of bits for each **individual** component pixel.                                                                             |
| `u(8)`                | `rv_ver_subsample` | Vertical subsampling factor. Indicates how many bits to shift from `rv_height` to get the plane height. MUST be 0 if video is RGB.      |
| `u(8)`                | `rv_hor_subsample` | Horizontal subsampling factor. Indicates how many bits to shift from `rv_width` to get the plane width. MUST be 0 if video is RGB.      |
| `b(32)`               | `rv_flags`         | Flags for the video stream.                                                                                                             |
| `u(rc_planes*32)`     | `rv_plane_stride`  | For each plane, the total number of bytes **per horizontal line**, including any padding.                                               |
| `u(rv_components*8)`  | `rc_plane`         | Specifies the plane index that the component belongs in.                                                                                |
| `u(rv_components*8)`  | `rc_stride`        | Specifies the distance between 2 horizontally consequtive pixels of this component, in bits for bitpacked video, otherwise bytes.       |
| `u(rv_components*8)`  | `rc_offset`        | Specifies the number of elements before the component, in bits for bitpacked video, otherwise bytes.                                    |
| `i(rv_components*8)`  | `rc_shift`         | Specifies the number of bits to shift right (if negative) or shift left (is positive) to get the final value.                           |
| `u(rv_components*8)`  | `rc_bits`          | Specifies the total number of bits the component's value will contain.                                                                  |
|                       |                    | Additionally, if `rv_flags & 0x2` is *UNSET*, e.g. video doesn't contain floats, the following additional fields **MUST** be present.   |
| `i(rv_components*32)` | `rc_range_low`     | Specifies the lowest possible value for the component. MUST be less than `rc_range_high`.                                               |
| `i(rv_components*32)` | `rc_range_high`    | Specifies the highest posssible value for the component. MUST be less than or equal to `(1 << rc_bits) - 1`.                            |

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
an addendum to this document to formalize such containerization. This field MUST NOT be
used if the codec being contained already has a formal definition in this spec.

For custom encapsulation, the `codec_id` in
[stream registration packets](#stream-registration-packets)
MUST be 0x433f\*\*\*\* (`C?**`), where the bottom 2 bytes can be any value between
0x30 to 0x39 (`0` to `9` in ASCII) and 0x61 to 0x7a (`a` to `z` in ASCII).

The [stream initialization data](#init-data-packets) packet can be any length
and contain any sequence of data.

The `packet_data` field can be any length and contain any sequence of data.
