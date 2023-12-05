# libavtransport

The reference code for the implementation of the AVTransport protocol can be found here.

As well as being clean, segmented and commented, the code is a fully usable and optimized library,
rather than only being a reference.
The library is meant for embedding into different projects.

The output graph is as follows:

`output (high-level API) -> output_packet (high-level packetizer) -> connection (segmenter/interleaver/buffer) -> protocol (low-level bytestream generation/encapsulation generation) -> io (low-level reader/writer)`

`protocol` may additionally chain another `protocol` before `io` (such as QUIC -> UDP).
