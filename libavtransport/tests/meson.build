# Copyright © 2024, Lynne
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

ldpc_encode_test = executable('ldpc_encode',
    sources : [ 'ldpc_encode.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ avtransport_spec_pkt_headers ]) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('LDPC encoding', ldpc_encode_test)

io_file_test = executable('io_file',
    sources : [ 'file_io_common.c', 'io_file.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ 'buffer.c', 'io_file.c' ]) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('File I/O', io_file_test)

if host_machine.system() != 'windows'
    io_fd_test = executable('io_fd',
        sources : [ 'file_io_common.c', 'io_fd.c' ],
        include_directories : [ '../' ],
        objects : [ avtransport_lib.extract_objects([ 'buffer.c', 'io_fd.c' ]) ],
        dependencies : [ avtransport_deps_list, avtransport_dep ],
    )
    test('FD I/O', io_fd_test)

    io_mmap_test = executable('io_mmap',
        sources : [ 'file_io_common.c', 'io_mmap.c' ],
        include_directories : [ '../' ],
        objects : [ avtransport_lib.extract_objects([ 'buffer.c', 'io_mmap.c' ]) ],
        dependencies : [ avtransport_deps_list, avtransport_dep ],
    )
    test('mmap I/O', io_mmap_test)
endif

io_udp_test = executable('io_udp',
    sources : [ 'net_common.c', 'io_udp.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ 'address.c', 'io_udp.c', 'io_socket_common.c', 'buffer.c']) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('UDP I/O', io_udp_test)

io_udp_lite_test = executable('io_udp_lite',
    sources : [ 'net_common.c', 'io_udp_lite.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ 'address.c', 'io_udp.c', 'io_socket_common.c', 'buffer.c']) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('UDP-Lite I/O', io_udp_lite_test)

address_test = executable('address',
    sources : [ 'address.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ 'address.c' ]) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('Address parsing', address_test)

packet_encode_decode_test = executable('packet_encode_decode',
    sources : [ 'packet_encode_decode.c' ],
    include_directories : [ '../' ],
    objects : [ avtransport_lib.extract_objects([ avtransport_spec_pkt_headers, 'ldpc_encode.c' ]) ],
    dependencies : [ avtransport_deps_list, avtransport_dep ],
)
test('Packet encode/decode', packet_encode_decode_test)
