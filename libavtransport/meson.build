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

spec2c = files('./tools/spec2c.py')

avtransport_spec_pkt_headers = custom_target(
    'packet_encode_decode',
    input: spec_file,
    output: ['packet_encode.h', 'packet_decode.h', 'ldpc_tables.h', 'ldpc_tables.c'],
    command: [python_exe, spec2c, 'packet_encode,packet_decode,ldpc_tables_h,ldpc_tables_c', '@INPUT@', '@OUTPUT0@', '@OUTPUT1@', '@OUTPUT2@', '@OUTPUT3@']
)

# Generate additional headers from spec and install them
subdir('include/avtransport')

# Base files
sources = [
    'avtransport.c',
    'buffer.c',
    'utils.c',
    'rational.c',

    'ldpc.c',
    'raptor.c',

    'address.c',
    'connection.c',

    'protocol_common.c',
    'protocol_stream.c',
    'protocol_datagram.c',

    'io_common.c',
    'io_null.c',
    'io_file.c',
    'io_unix.c',
    'io_dcb.c',

    'io_udp.c',
    'io_socket_common.c',

    avtransport_spec_pkt_headers,

    # Version
    vcs_tag(command: ['git', 'rev-parse', '--short', 'HEAD'],
                      input: 'version.c.in', output: 'version.c',
                      fallback: 'release')
]

if openssl_dep.found()
   sources += 'protocol_quic.c'
endif

if host_machine.system() != 'windows'
    sources += 'io_fd.c'
    sources += 'io_mmap.c'
endif

if get_option('output').auto()
    sources += 'output.c'
    sources += 'output_packet.c'
    sources += 'scheduler.c'
    sources += 'ldpc_encode.c'
endif

if get_option('input').auto()
    sources += 'input.c'
    sources += 'decode.c'
    sources += 'reorder.c'
    sources += 'ldpc_decode.c'
endif

if get_option('enable_asm').enabled()
    if host_machine.cpu_family().startswith('x86')
        if add_languages('nasm', required: false, native: false)
            subdir('x86')
        else
            warning('nasm not found! libavtransport will be compiled without assembly optimizations!')
        endif
    endif
endif

if host_machine.system() == 'windows'
    avtransport_soversion = ''
else
    avtransport_soversion = avtransport_api_version_major
endif

avtransport_inc = include_directories('./include')

# Generate local configure file
configure_file(
    output: 'config.h',
    configuration: conf,
)

# Deps
avtransport_deps_list = [
    xxh_dep,
    cbor_dep,
    openssl_dep,
    uring_dep,
    zstd_dep,
    brotlienc_dep,
    brotlidec_dep,
]

avtransport_lib = library('avtransport',
    sources,
    install: true,

    sources: avtransport_spec_pub_headers,
    include_directories: avtransport_inc,
    dependencies: avtransport_deps_list,
    version: avtransport_soname_version,
    soversion: avtransport_soversion,
)

avtransport_dep = declare_dependency(
    link_with: avtransport_lib,
    include_directories: avtransport_inc,
    sources: avtransport_spec_pub_headers,
)

# Do tests if enabled
if get_option('tools').auto()
    subdir('tests')
endif

# Allows projects to build libavtransport by cloning into ./subprojects/libavtransport
meson.override_dependency('libavtransport', avtransport_dep)

pkg = import('pkgconfig')
pkg.generate(
    name: meson.project_name(),
    description: 'AVTransport reference library',
    libraries: avtransport_lib,
    version: meson.project_version(),
)

summary({
    'output': get_option('output').auto(),
    'input': get_option('input').auto(),
    'xxhash': xxh_dep.found(),
    'cbor': cbor_dep.found(),
    'openssl': openssl_dep.found(),
    'uring': uring_dep.found(),
    'zstd': zstd_dep.found(),
    'brotli (enc)':  brotlienc_dep.found(),
    'brotli (dec)':  brotlidec_dep.found(),
}, section: 'libavtransport', bool_yn: true)
