# libavtransport

The reference code for the implementation of the AVTransport protocol can be found here.

As well as being clean, segmented and commented, the code is a fully usable and optimized library,
rather than only being a reference.
The library is meant for embedding into different projects.

The output flow graph is as follows:

```
 - output (high-level API)
       output_packet (high-level packetizer)
 - (user calls avt_connection_process)
 - connection (high-level management + buffering)
       scheduler (segmenter + interleaver + actual packet byte encoding)
 - protocol (encapsulation)
 - io (low-level reader/writer)
```

In certain IO cases where io_uring or mmapping can be used, the packet data path is done
entirely with zero copies.

The input flow graph is as follows:
```
 - (user calls avt_connection_process)
 - connection (calls protocol to get a packet)
 - protocol
 - io
 - protocol
       (optional header FEC)
       packet reorder + assembler
       (regular payload FEC)
 - connection (buffering)
 - input
       gets assembled packets, handles stream management
       (dispatch user callbacks)
```
