# libavtransport

The reference code for the implementation of the AVTransport protocol can be found here.

The code is a fully usable and optimized library, rather than simply being a reference,
and contains tools for manipulating AVTransport streams and files.
The library is also embeddable into different projects.

The output graph is as follows:

External API -> output.c -> encode.c -> avtransport.c -> connection.c -> protocol_noop.c -> io_file.c
