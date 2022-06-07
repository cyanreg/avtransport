Qproto protocol
================
The Qproto protocol is a new standardized mechanism for unidirectional and bidirectional
multimedia transport and storage. This protocol aims to be a simple, reliable and robust
low-overhead method that borrows design decisions from other protocols and aims to be
generic, rather than specialized for certain use-cases. With minimal changes, it can be
processed as a stream or saved to a file as an intermediate step or for post-processing.

Rather than just being a new container and protocol filling a niche, this also attempts
to solve issues with current other containers, like timestamp rounding, lack of DTS,
inflexible metadata, inconvenient index positions, lack of context, inextensible formats,
and rigid overseeing organizations.

This specifications provides support for streaming over 2 protocols: [UDP](#udp) and [QUIC/HTTP3](#quichttp3).
See the [streaming](#streaming) section for information on how to adapt Qproto for such
use-cases.

Specification conventions
-------------------------
Throughout all of this document, bit sequences are always big-endian, and numbers are always two's complement.
Keywords given in all caps are to be interpreted as stated in IETF RFC 2119.

A special notation is used to describe sequences of bits:
 - `u(bits)`: specifies the data that follows is an unsigned integer of N bits.
 - `i(bits)`: the same, but the data describes a signed integer is signed.
 - `b(bits)`: the data is an opaque string of bits that clients MUST NOT interpret as anything else.
 - `r(bits)`: the data is a rational number, with a numerator of `u(bits/2)` and following that, a denumerator of `u(bits/2)`. The denumerator MUST be greater than `0`.

All floating point samples and pixels are always **normalized** to fit within the interval `[-1.0, 1.0]`.

Protocol overview
-----------------
On a high-level, Qproto is a thin wrapper around codec, metadata and user packets that
provides context and error resilience, and defines a standardized way to transmit such
data between clients.

The structure of data, when saved as a file, is:

| Data   | Name              | Fixed value       | Description                                                                                                                                 |
|:-------|:------------------|------------------:|:--------------------------------------------------------------------------------------------------------------------------------------------|
| b(32)  | `fid`             |        0x5170726f | Unique file identifier that identifiers the following data is a Qproto stream.                                                              |
| i(64)  | `time_sync`       |                   | Time synchronization field, see the section on [time synchronization](#time-synchronization).                                               |
|        | `init_data`       |                   | Special data packet used to initialize a decoder on the receiving side. See [init packets](#init-packets).                                  |
|        | `data_packet`     |                   | Stream of concatenated packets of variable size, see the section on [data packets](#data-packets).                                          |
|        | `data_segment`    |                   | Segment of a fragmented data packet. See the section on [data segmentation](#data-segmentation).                                            |
|        | `fec_segment`     |                   | Optional. May be used to error-detect/correct the payload. See [FEC segments](#fec-segments).                                               |
|        | `index_packet`    |                   | Index packet, used to provide fast seeking support. Optional. See [index packets](#index-packets).                                          |
|        | `metadata_packet` |                   | Metadata packet. Optional. See [metadata packets](#metadata-packets).                                                                       |
|        | `user_data`       |                   | User-specific data. Optional. See [user data packets](#user-data-packets).                                                                  |
|        | `padding_packet`  |                   | Padding data. Optional. See [padding](#padding).                                                                                            |
| b(32)  | `eos`             |        0xf6270715 | Used to signal end of a stream. See the [end of stream](#end-of-stream) section. Optional.                                                  |

All fields are implicitly padded to the nearest byte. The padding SHOULD be all zeroes.

The order in which the fields may appear **first** in a stream is as follows:
| Field             | Order                                                                                                                         |
|:------------------|:------------------------------------------------------------------------------------------------------------------------------|
| `fid`             | MUST always be first. MAY be present multiple times, as long as it's before `eos` with a `stream_id` of `0xffffffff`.         |
| `time_sync`       | MUST be present before any and all `data_packet`s, `index_packet`s and `metadata_packet`s.                                    |
| `init_data`       | MUST be present before any and all `data_packet`s.                                                                            |
| `index_packet`    | MAY be present anywhere allowed, but it's recommended for it to be present either at the start, or at the very end in a file. |
| `metadata_packet` | MUST be present before any `init_data` packets.                                                                               |
| `fec_segment`     | MAY be present after a `data_packet` or `data_segment` to perform error correction.                                           |
| `user_data`       | MAY be present anywhere otherwise allowed.                                                                                    |
| `padding_packet`  | MAY be preseny anywhere otherwise allowed.                                                                                    |
| `eos`             | MUST be present last if its `stream_id` is `0xffffffff`, if at all.                                                           |

Error resilience
----------------
Common FEC Object Transmission Information (OTI) format and Scheme-Specific FEC Object Transmission Information as described in the document are *never* used.
Instead, the symbol size is fixed to **32 bits**, unless stated otherwise. After writing the encoded data chunk, the stream MUST be padded to the nearest multiple
of 8 since the start of the header with zero bits, at which point what follows is either unprotected bits or the encoded data of the next packet.

Time synchronization
--------------------
In order to optionally make sense of timestamps present in the stream, or provide a context for synchronization, the `time_sync` packet is sent through.
This signals the `epoch` of the timestamps, in nanoseconds since 00:00:00 UTC on 1 January 1970 ([Unix time](https://en.wikipedia.org/wiki/Unix_time)).
The layout of the data in a `time_sync` packet is as follows:

| Data   | Name              | Fixed value  | Description                                      |
|:-------|:------------------|-------------:|:-------------------------------------------------|
| b(32)  | `time_descriptor` |         0x70 | Indicates this is a time synchronization packet. |
| b(64)  | `epoch`           |              | Indicates the time epoch.                        |

This field MAY be zero, in which case the stream has no real-world time-relative context.

If this field is non-zero, senders SHOULD ensure the `epoch` value is actual.

If the stream is a file, receivers SHOULD ignore this, but MAY expose the offset as a metadata `date` field.</br>
Also, when the stream is a file, receivers CAN replace the field by a user-defined value in order to permit synchronized
presentation between multiple unconnected receivers.

If the stream is live, receivers are permitted to ignore the value and present as soon as new data is received.</br>
Receivers are also free to trust the field to be valid, and interpret all timestamps as a positive offset of the
`epoch` value, and present at that time, such that synchronized presentation between unconnected receivers is possible.</br>
Receivers are also free to consider the field trustworthy, and use its value to estimate the sender start time,
as well as the end-to-end latency.

Init packets
------------
This packet is used to signal stream registration.

Initializing a decoder requires additional one-off data, usually referred as `extradata`. This is codec-specific, and implementation-specific.
The layout of the data is as follows:

| Data               | Name                | Fixed value | Description                                                               |
|:-------------------|:--------------------|------------:|:--------------------------------------------------------------------------|
| b(32)              | `init_descriptor`   |         0x1 | Indicates this is an initialization packet.                               |
| b(32)              | `stream_id`         |             | Indicates the stream ID for which to attach this.                         |
| b(32)              | `derived_stream_id` |             | Indicates the stream ID for which this stream is be derived from.         |
| b(32)              | `stream_flags`      |             | Flags to signal what sort of a stream this is.                            |
| b(32)              | `codec_id`          |             | Signals the codec ID for the data packets in this stream.                 |
| r(64)              | `timebase`          |             | Signals the timebase of the timestamps present in data packets.           |
| u(32)              | `init_length`       |             | Indicates the length of the initialization data in bytes. May be zero.    |
| b(`init_length`*8) | `init_data`         |             | Actual codec-specific initialization data.                                |

For more information on the layout of the specific data, consult the [codec-specific encapsulation](#codec-encapsulation) addenda.

However, in general, the data follows the same layout as what [FFmpeg's](https://ffmpeg.org) `libavcodec` produces and requires.
An implementation MAY error out in case it cannot handle the parameters specified in the `init_data`. If so, when reading a file, it MUST stop, otherwise in a live scenario, it MUST return an `unsupported` [control data](#control-data).
If this packet is sent for an already-initialized stream AND its byte-wise contents do not match the old contents, implementations MUST flush and reinitialize the decoder before attempting to decoder more `data_packet`s.
Otherwise, implementations may expose this as an alternative stream the user may choose to switch to.

The `stream_flags` field may be interpreted as such:

| Bit set | Description                                                                         |
|--------:|-------------------------------------------------------------------------------------|
|     0x1 | Indicates stream is a still picture and only a single decodable frame will be sent. |

The `derived_stream_id` denotes the stream ID of which this stream is a variant of. It MAY be used to signal streams which carry the same content, but with a different codec or resolution.
If the stream is standalone, or it's meant to be the default variant, `derived_stream_id` MUST match `stream_id`.

Data packets
------------
The data packets indicate the start of a packet, which may be fragmented into more [data segments](#data-segmentation). It is laid out as follows:

| Data               | Name              | Fixed value | Description                                                                                                                                |
|:-------------------|:------------------|------------:|:-------------------------------------------------------------------------------------------------------------------------------------------|
| b(32)              | `data_descriptor` |        0x10 | Indicates this is a data packet.                                                                                                           |
| b(32)              | `seq_number`      |             | A per-stream monotonically incrementing number.                                                                                            |
| b(32)              | `stream_id`       |             | Indicates the stream ID for this packet.                                                                                                   |
| b(8)               | `pkt_flags`       |             | Bitmask with flags to specift the packet's properties.                                                                                     |
| i(64)              | `pts`             |             | Indicates the presentation timestamp offset that when combined with the `epoch` field signals when this frame SHOULD be presented at.      |
| i(64)              | `dts`             |             | The time, which when combined with the `epoch` field that indicates when a frame should be input into a synchronous 1-in-1-out decoder.    |
| i(64)              | `duration`        |             | The duration of this packet.                                                                                                               |
| b(32)              | `data_length`     |             | The size of the data in this packet.                                                                                                       |
| b(`data_length`*8) | `packet_data`     |             | The packet data itself.                                                                                                                    |

For information on the layout of the specific codec-specific packet data, consult the [codec-specific encapsulation](#codec-encapsulation) addenda.

The final timestamp in nanoseconds is given by the following formula: `timebase.num*pts*1000000000/timebase.den`. Users MAY ensure that this overflows gracefully after ~260 years.

The same formula is also valid for the `dts` field.

Implementations MUST feed the packets to the decoder in an incrementing order according to the `dts` field.

Negative `pts` values ARE allowed, and implementations **MUST** decode such frames, however **MUST NOT** present any such frames unless `pts + duration` is greater than 0, in which case they MUST present the data
required for that duration.</br>
In other words, if the data is for an audio stream, implementations MUST start presenting from the given offset.

The `pkt_flags` field MUST be interpreted in the following way:

| Bit position set | Description                                                                                                                                                                                                  |
|-----------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|              0x1 | Packet is incomplete and requires extra [data segments](#data-segmentation) to be completed.                                                                                                                 |
|              0x2 | Packet contains a keyframe which may be decoded standalone.                                                                                                                                                  |
|              0x3 | Packet contains an S-frame, which may be used in stead of a keyframe to begin decoding from, with graceful presentation degradation, or may be used to switch between different variants of the same stream. |

If the `0x1` flag is set, then the packet data is incomplete, and at least ONE [data segment](#data-segmentation) packet with an ID of `0x12` MUST be present to terminate the packet.

Data segmentation
-----------------
Packets can be split up into separate chunks that may be received out of order and assembled. This allows transmission over switched networks with a limited MTU, or prevents very large packets from one stream interfering with another stream.
The syntax to allow for this is as follows:

| Data               | Name              | Fixed value   | Description                                                                                 |
|:-------------------|:------------------|--------------:|:--------------------------------------------------------------------------------------------|
| b(32)              | `seg_descriptor`  | 0x11 and 0x12 | Indicates this is a data segment packet (0x11) or a data segment terminating packet (0x12). |
| b(32)              | `stream_id`       |               | Indicates the stream ID for this packet.                                                    |
| i(32)              | `seq_number`      |               | Indicates the packet for which this segment is a part of.                                   |
| b(32)              | `data_length`     |               | The size of the data segment.                                                               |
| b(32)              | `data_offset`     |               | The byte offset for the data segment.                                                       |
| b(`data_length`*8) | `packet_data`     |               | The data for the segment.                                                                   |

The `stream_id` and `seq_number` fields MUST match those sent over [data packets](#data-packets).

The size of the final assembled packet is the sum of all `data_length` fields.

Data segments and packets may arrive out of order and be duplicated. Implementations SHOULD assemble them into complete packets, but MAY skip segments, packets or even frames due to latency concerns, and MAY try to decode and present incomplete or damaged data.

Implementations MAY send duplicate segments to compensate for packet loss, should bandwidth permit.

Implementations SHOULD discard any packets and segments that arrive after their presentation time. Implementations SHOULD garbage collect any packets and segments that arrive with unrealistically far away presentation times.

FEC segments
------------
The data in an Error Detection and Correction packet is laid out as follows:

| Data              | Name              | Fixed value  | Description                                                                           |
|:------------------|:------------------|-------------:|:--------------------------------------------------------------------------------------|
| b(32)             | `fec_descriptor`  |         0x20 | Indicates this is an FEC packet for data sent.                                        |
| b(32)             | `stream_id`       |              | Indicates the stream ID for whose packets are being backed.                           |
| i(32)             | `seq_number`      |              | Indicates the packet which this FEC data is backing.                                  |
| b(32)             | `data_offset`     |              | The byte offset for the data this FEC packet protects.                                |
| u(32)             | `data_length`     |              | The length of the data protected by this FEC packet in bytes.                         |
| b(32)             | `fec_symbol_size` |              | The symbol size for the following sequence of error correction data.                  |
| u(32)             | `fec_length`      |              | The length of the FEC data.                                                           |
| b(`fec_length`*8) | `fec_data`        |              | The FEC data that can be used to check or correct the previous data packet's payload. |

The `stream_id` and `seq_number` fields MUST match those sent over [data packets](#data-packets).

Implementations MAY discard the FEC data, or MAY delay the previous packet's decoding to correct it with this data, or MAY attempt to decode the previous data, and if failed, retry with the corrected data packet.

The same lifetime and duplication rules apply for FEC segments as they do for regular data segments.

Index packets
-------------
The index packet contains available byte offsets of nearby keyframes and the distance to the next index packet.

| Data               | Name               | Fixed value  | Description                                                                                                                                                                                |
|:-------------------|:-------------------|-------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| b(32)              | `index_descriptor` |         0x30 | Indicates this is an index packet.                                                                                                                                                         |
| b(32)              | `stream_id`        |              | Indicates the stream ID for the index. May be 0xffffffff, in which case, it applies to all streams.                                                                                        |
| u(32)              | `prev_idx`         |              | Position of the previous index packet, if any, in bytes, relative to the current position. Must be exact. If exactly 0xffffffff, indicates no such index is available, or is out of scope. |
| i(32)              | `next_idx`         |              | Position of the next index packet, if any, in bytes, relative to the current position. May be inexact, specifying the minimum distance to one. Users may search for it.                    |
| u(32)              | `nb_indices`       |              | The total number of indices present in this packet.                                                                                                                                        |
| i(`nb_indices`*32) | `idx_pos`          |              | The position of a decodable index relative to the current position in bytes.                                                                                                               |
| i(`nb_indices`*64) | `idx_pts`          |              | The presentation timestamp of the index.                                                                                                                                                   |
| u(`nb_indices`*8)  | `idx_chapter`      |              | If a value is greater than 0, demarks the start of a chapter with an index equal to the value.                                                                                             |

Metadata packets
----------------
The metadata packets can be sent for the overall stream, or for a specific `stream_id` substream. The syntax is as follows:

| Data                | Name              | Fixed value | Description                                                                                            |
|:--------------------|:------------------|------------:|:-------------------------------------------------------------------------------------------------------|
| b(32)               | `meta_descriptor` |        0x40 | Indicates this is a metadata packet.                                                                   |
| b(32)               | `stream_id`       |             | Indicates the stream ID for the metadata. May be 0xffffffff, in which case, it applies to all streams. |
| u(32)               | `metadata_len`    |             | Indicates the metadata length in bytes.                                                                |
| b(`metadata_len`*8) | `ebml_metadata`   |             | The actual metadata. EBML, as described in IETF RFC 8794.                                              |

User data packets
-----------------
The user-specific data packet is laid out as follows:

| Data                    | Name               | Fixed value  | Description                                                                           |
|:------------------------|:-------------------|-------------:|:--------------------------------------------------------------------------------------|
| b(32)                   | `user_descriptor`  |         0x50 | Indicates this is an opaque user-specific data.                                       |
| u(32)                   | `user_data_length` |              | The length of the user data.                                                          |
| b(`user_data_length`*8) | `user_data`        |              | The user data itself.                                                                 |

Padding
-------
The padding packet is laid out as follows:

| Data                   | Name                 | Fixed value  | Description                                                                           |
|:-----------------------|----------------------|-------------:|---------------------------------------------------------------------------------------|
| b(32)                  | `padding_descriptor` |         0x60 | Indicates this is a padding packet.                                                   |
| u(32)                  | `padding_length`     |              | The length of the padding data.                                                       |
| b(`padding_length`*8)  | `padding_data`       |              | The padding data itself. May be all zeroes or random numbers.                         |

The purpose of the padding data is to keep a specific exact overall constant bitrate, and to prevent metadata leakage for encrypted contents (the packet length).

End of stream
-------------
The EOS packet is laid out as follows:

| Data                   | Name                 | Fixed value  | Description                                                                                                 |
|:-----------------------|:---------------------|-------------:|:------------------------------------------------------------------------------------------------------------|
| b(32)                  | `eos_descriptor`     |         0x70 | Indicates this is an end-of-stream packet.                                                                  |
| b(32)                  | `stream_id`          |              | Indicates the stream ID for the end of stream. May be 0xffffffff, in which case, it applies to all streams. |

The `stream_id` field may be used to indicate that a specific stream will no longer receive any packets, and implementations are free to unload decoding and free up any used resources.

The `stream_id` MAY be reused afterwards, but this is not recommended.

If not encountered in a stream, and the connection was cut, then the receiver is allowed to gracefully wait for a reconnection.

If encountered in a file, the implementation MAY regard any data present afterwards as padding and ignore it. Qproto files **MAY NOT** be concatenated.

Streaming
=========

UDP
---
To adapt Qproto for streaming over UDP is trivial - simply supply the data as-is specified for a file, with no changes required. The sender implementation
SHOULD resend `fid`, `time_sync` and `init_data` packets for all streams at most as often as the stream with the lowest frequency of `keyframe`s in order
to permit for implementations that didn't catch on the start of the stream begin decoding.
UDP mode is unidirectional, but the implementations are free to use the [reverse signalling](#reverse-signalling) data if they negotiate it themselves.

Implementations SHOULD segment the data such that the MTU is never exceeded and no packet splitting occurs.

QUIC/HTTP3
----------
Qproto tries to use as much of the modern conveniences of QUIC as possible. As such, it uses both reliable and unreliable streams, as well as bidirectionality
features of the transport mechanism.

Any data marked as error-corrected (`Tu`, `Tb`, etc.) MUST be sent over in a *reliable* stream, as per [draft-schinazi-masque-h3-datagram](https://datatracker.ietf.org/doc/html/draft-ietf-masque-h3-datagram),
with **no error correction**, but rather as-is, a raw stream of bits.
The rest of the data MUST be sent over an *unreliable* stream.

Reverse signalling
==================
The following section is a specification on how reverse signalling, where receiver(s) communicates with the sender, should be formatted.

Control data
------------
The receiver can use this type to return errors and more to the sender in a one-to-one transmission. The following syntax is used:

| Data                | Name               | Fixed value  | Description                                                                                              |
|:--------------------|:-------------------|-------------:|:---------------------------------------------------------------------------------------------------------|
| b(32)               | `ctrl_descriptor`  |   0xffff0009 | Indicates this is a control data packet.                                                                 |
| b(8)                | `cease`            |              | If not equal to `0x0`, indicates a fatal error, and senders MUST NOT sent any more data.                 |
| u(32)               | `error`            |              | Indicates an error code, if not equal to `0x0`.                                                          |
| b(8)                | `resend_init`      |              | If nonzero, asks the sender to resend all `fid`, `time_sync` and `init_data` packets.                    |
| b(128)              | `uplink_ip`        |              | Reports the upstream address to stream to.                                                               |
| b(16)               | `uplink_port`      |              | Reports the upstream port to stream on to the `uplink_ip`.                                               |
| i(64)               | `seek`             |              | Asks the sender to seek to a specific relative byte offset, as given by [index_packets](#index-packets). |

If the sender gets such a packet, and either its `uplink_ip` or its `uplink_port` do not match, the sender **MUST** cease this connection, reopen a new connection
with the given `uplink_ip`, and resend all `fid`, `time_sync` and `init_data` packets to the destination.

The `seek` request asks the sender to resend old data that's still available. The sender MAY not comply if that data suddently becomes unavailable.
If the value of `seek` is equal to `(1 << 63) - 1`, then the receiver MUST comply and start sending the newest data.

If the `resent_init` flag is set to a non-zero value, senders MAY flush all encoders such that the receiver can begin decoding as soon as possible.

If operating over [QUIC/HTTP3](#quichttp3), then any old data **MUST** be served over a *reliable* stream, as latency isn't critical. If the receiver asks again
for the newsest available data, that data's payload is once again sent over an *unreliable* stream.

The following error values are allowed:
| Value | Description                                                                                                                                                                                                                                                                      |
|------:|:---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|   0x1 | Generic error.                                                                                                                                                                                                                                                                   |
|   0x2 | Unsupported data. May be sent after the sender sends an [init packet](#init-packets) to indicate that the receiver does not support this codec. The sender MAY send another packet of this type with the same `stream_id` to attempt reinitialization with different parameters. |

Statistics
----------
The following packet MAY be sent from the receiver to the sender.

| Data                | Name               | Fixed value  | Description                                                                                                                                                                           |
|:--------------------|:-------------------|-------------:|:--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| b(32)               | `stats_descriptor` |   0xffff0001 | Indicates this is a statistics packet.                                                                                                                                                |
| b(32)               | `corrupt_packets`  |              | Indicates the total number of corrupt packets, where either CRC or FEC in `fec_packets` detected errors, or if such were missing, decoding was impossible or had errors.              |
| u(32)               | `dropped_packets`  |              | Indicates the total number of dropped [data packets](#data-packets). A dropped packet is when there's a discontinuity in the timestamps of the stream (`pts + duration != next_pts`). |

Reverse user data
-----------------
| Data                    | Name               | Fixed value  | Description                                                                           |
|:------------------------|:-------------------|-------------:|:--------------------------------------------------------------------------------------|
| b(32)                   | `user_descriptor`  |   0xffff0006 | Indicates this is an opaque user-specific data.                                       |
| u(32)                   | `user_data_length` |              | The length of the user data.                                                          |
| b(`user_data_length`*8) | `user_data`        |              | The user data itself.                                                                 |

This is identical to the [user data packets](#user-data-packets), but with a different ID.

Addendum
========

Codec encapsulation
-------------------
The following section lists the supported codecs, along with their encapsulation definitions.

 - [Opus](#opus-encapsulation)
 - [AAC](#aac-encapsulation)
 - [AV1](#av1-encapsulation)
 - [Raw audio](#raw-audio-encapsulation)
 - [Raw video](#raw-video-encapsulation)

### Opus encapsulation

For Opus encapsulation, the `codec_id` in the [data packets](#data-packets) MUST be 0x4f707573 (`Opus`).

The `init_data` field MUST be laid out as follows order:

| Data               | Name              | Fixed value                     | Description                                                                           |
|:-------------------|:------------------|--------------------------------:|:--------------------------------------------------------------------------------------|
| b(64)              | `opus_descriptor` | 0x4f70757348656164 (`OpusHead`) | Opus magic string.                                                                    |
| b(8)               | `opus_init_ver`   |                             0x1 | Version of the extradata. MUST be 0x1.                                                |
| b(8)               | `opus_channels`   |                                 | Number of audio channels.                                                             |
| b(16)              | `opus_prepad`     |                                 | Number of samples to discard from the start of decoding (encoder delay).              |
| b(32)              | `opus_rate`       |                                 | Samplerate of the data. MUST be 48000.                                                |
| b(16)              | `opus_gain`       |                                 | Volume adjustment of the stream. May be 0 to preserve the volume.                     |
| b(32)              | `opus_ch_family`  |                                 | Opus channel mapping family. Consult IETF RFC 7845 and RFC 8486.                      |

The `packet_data` MUST contain regular Opus packets with their front uncompressed header intact.

In case of multiple channels, the packets MUST contain the concatenated contents in coding order of all channels' packets.

In case the Opus bitstream contains native Opus FEC data, the FEC data MUST be appended to the packet as-is, and no [FEC packets](#fec-packets) must be present in regards to this stream.

Implementations **MUST NOT** use the `opus_prepad` field, but **MUST** set the first stream packet's `pts` value to a negative value as defined in [data packets](#data-packets) to remove the required number of prepended samples.

### AAC encapsulation

For AAC encapsulation, the `codec_id` in the [data packets](#data-packets) MUST be 0x41414300 (`AAC\0`).

The `init_data` MUST be the codec's `AudioSpecificConfig`, as defined in MPEG-4.

The `packet_data` MUST contain regular AAC ADTS packtes. Note that `LATM` is explicitly unsupported.

Implementations **MUST** set the first stream packet's `pts` value to a negative value as defined in [data packets](#data-packets) to remove the required number of prepended samples.

### AV1 encapsulation

For AV1 encapsulation, the `codec_id` in the [data packets](#data-packets) MUST be 0x41563031 (`AV01`).

The `init_data` MUST be the codec's so-called `uncompressed header`. For information on its syntax, consult the specifications, section `5.9.2. Uncompressed header syntax`.

The `packet_data` MUST contain raw, separated `OBU`s.

### Raw audio encapsulation

For raw audio encapsulation, the `codec_id` in the [data packets](#data-packets) MUST be 0x52414141 (`RAAA`).

The `init_data` field MUST be laid out as follows order:

| Data               | Name             | Fixed value | Description                                                                            |
|:-------------------|:-----------------|------------:|:---------------------------------------------------------------------------------------|
| b(16)              | `ra_channels`    |             | The number of channels contained OR ambisonics data if `ra_ambi == 0x1`.               |
| b(8)               | `ra_ambi`        |             | Boolean flag indicating that samples are ambisonics (`0x1`) or plain channels (`0x0`). |
| b(8)               | `ra_bits`        |             | The number of bits for each sample.                                                    |
| b(8)               | `ra_float`       |             | The data contained is floats (`0x1`) or integers (`0x0`).                              |
| b(`ra_channels`*8) | `ra_pos`         |             | The coding position for each channel.                                                  |

The position for each channel is determined by the value of the integers `ra_pos`, and may be interpreted as:
| Value | Position    |
|------:|:------------|
|     1 | Left        |
|     2 | Right       |
|     3 | Center      |
|     4 | Side left   |
|     5 | Side right  |
|     6 | Rear left   |
|     7 | Rear right  |
|     8 | Rear center |
|     9 | LFE         |

If the integer value is not on this list, assume it's unknown. Users are invited to interpret the channel as they please.

Multiple channels with the same lable MAY be present, and it's up to users to interpret them.

The `packet_data` MUST contain the concatenated stream of **interleaved** samples for all channels.

The samples MUST be **normalized** between **[-1.0, 1.0]** if they're float, and full-range *signed* if they're integers.

The size of each sample MUST be `ra_bits`, and MUST be aligned to the nearest **power of two**, with the padding in the **least significant bits**. That means that 24 bit samples are coded as 32 bits,
with the data contained in the topmost 24 bits.

### Raw video encapsulation

For raw video encapsulation, the `codec_id` in the [data packets](#data-packets) MUST be 0x52415656 (`RAVV`).

The `init_data` field MUST be laid out in the following way:

| Data                  | Name               | Fixed value | Description                                                                                                                             |
|:----------------------|:-------------------|------------:|:----------------------------------------------------------------------------------------------------------------------------------------|
| b(32)                 | `rv_width`         |             | The number of horizontal pixels the video stream contains.                                                                              |
| b(32)                 | `rv_height`        |             | The number of vertical pixels the video stream contains.                                                                                |
| b(8)                  | `rv_components`    |             | The number of components the video stream contains.                                                                                     |
| b(8)                  | `rv_planes`        |             | The number of planes the video components are placed in.                                                                                |
| b(8)                  | `rv_bpp`           |             | The number of bits for each **individual** component pixel.                                                                             |
| b(8)                  | `rv_ver_subsample` |             | Vertical subsampling factor. Indicates how many bits to shift from `rv_height` to get the plane height. MUST be 0 if video is RGB.      |
| b(8)                  | `rv_hor_subsample` |             | Horizontal subsampling factor. Indicates how many bits to shift from `rv_width` to get the plane width. MUST be 0 if video is RGB.      |
| b(32)                 | `rv_flags`         |             | Flags for the video stream.                                                                                                             |
| b(`rv_components`*8)  | `rc_plane`         |             | Specifies the plane index that the component belongs in.                                                                                |
| b(`rv_components`*8)  | `rc_stride`        |             | Specifies the distance between 2 horizontally consequtive pixels of this component, in bits for bitpacked video, otherwise bytes.       |
| b(`rv_components`*8)  | `rc_offset`        |             | Specifies the number of elements before the component, in bits for bitpacked video, otherwise bytes.                                    |
| b(`rv_components`*8)  | `rc_shift`         |             | Specifies the number of bits to shift right (if negative) or shift left (is positive) to get the final value.                           |
| b(`rv_components`*8)  | `rc_bits`          |             | Specifies the total number of bits the component's value will contain.                                                                  |
|                       |                    |             | Additionally, if `rv_flags & 0x2` is *UNSET*, e.g. video doesn't contain floats, the following additional fields **MUST** be present.   |
| i(`rv_components`*32) | `rc_range_low`     |             | Specifies the lowest possible value for the component. MUST be less than `rc_range_high`.                                               |
| i(`rv_components`*32) | `rc_range_high`    |             | Specifies the highest posssible value for the component. MUST be less than or equal to `(1 << rc_bits) - 1`.                            |

The purpose of the `rc_offset` field is to allow differentiation between different orderings of pixels in an RGB video, e.g. `rgb`'s `rc_offset`s will be `[0, 1, 2]`, whilst `bgr`'s will be `[2, 1, 0]`.

The flags field MUST be interpreted in the following way:

| Bit position set | Description                                                                                                  |
|-----------------:|:-------------------------------------------------------------------------------------------------------------|
|              0x1 | Video is RGB                                                                                                 |
|              0x2 | Video contains IEEE-754 **normalized** floating point values. Precision is determined by the `rv_bpp` value. |
|              0x4 | Video contains a straight, non-premultiplied alpha channel. Alpha is always the last component.              |
|              0x8 | Video contains a premultiplied alpha channel. Alpha is always the last component.                            |
|             0x16 | At least one pixel component is not sharing a plane, e.g. video is *planar*.                                 |
|             0x32 | Video's components are packed, e.g. video is *bitpacked*.                                                    |
|             0x64 | Video's values are **big-endian**. If unset, values are *little-endian*.                                     |
