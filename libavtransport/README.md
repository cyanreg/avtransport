# libavtransport

The reference code for the implementation of the AVTransport protocol can be found here.

As well as being clean, segmented and commented, the code is a fully usable and optimized library,
rather than only being a reference.
The library is meant for embedding into different projects.

The output graph is as follows:

```
send (high-level API) ->
send_packet (high-level packetizer) ->
connection (high-level management + buffering) ->
scheduler (segmenter + interleaver + low-level bytestream) ->
protocol (encapsulation) ->
io (low-level reader/writer)
```

`protocol` may additionally chain another `protocol` before `io` (such as QUIC -> UDP).

In certain IO cases where io_uring or mmapping can be used, the packet data path is done
entirely with zero copies.
