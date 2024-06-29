# libavtransport

The reference code for the implementation of the AVTransport protocol can be found here.

As well as being clean, segmented and commented, the code is a fully usable and optimized library,
rather than only being a reference.
The library is meant for embedding into different projects.

# API

The external interface of the library is C89-compliant.

To start with, users first have to allocate a shared context from which all other
contexts can be created. This is done by calling `avt_init()`.

Unlike most other APIs, libavtransport manages connections for you, though you still have
the option of doing this yourself.
Call `avt_connection_create()` with a suitable connection parameters.

Connections are the central object to which inputs and outputs are attached to.
A connection may have at most a single sender attached, but can have multiple receivers
active at the same time.

To create a sender context, simply call `avt_send_open()` on a connection and begin
scheduling data to be sent.
`avt_send_open()` can be called with a valid, non-NULL `AVTSender` object,
in which case, the output will be linked to multiple connections.

To create a receiver context, call `avt_receive_open()`. The callbacks will
automatically be called.

Once everything is initialized, if the `async` option was not set during
`avvt_connection_create()`, users MUST call `avt_connection_process()` to
both send and receive scheduled packets.

# Internals

The output flow graph is as follows:

```
 - output.c (high-level API)
   * output_packet.c (high-level packetizer)
 - avt_connection_process (user must call this)
 - connection.c (high-level management + buffering)
   * scheduler.c (segmenter + interleaver + actual packet byte encoding)
 - protocol_*.c (encapsulation)
 - io_*.c (low-level reader/writer)
```

In certain IO cases where io_uring or mmapping can be used, the packet data path is done
entirely with zero copies.

The input flow graph is as follows:
```
 - avt_connection_process
 - connection.c
 - protocol_*.c
 - io_*.c
   * Read bytes or packets
 - protocol_*.c
   * (optional header FEC)
 - connection
   * reordering, merging, and buffering
 - input
   * high-level stream management
   * calls users's callbacks
```
