# Copyright © 2023, Lynne
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

# Headers
headers = [
    'avtransport.h',
    'connection.h',
    'output.h',
    'input.h',
    'stream.h',
    'rational.h',
    'utils.h'
]

# Generate additional headers from spec and install them
subdir('include/avtransport')

# Base files
sources = [
    'avtransport.c',
    'buffer.c',
    'utils.c',

    'ldpc.c',
    'raptor.c',

    'address.c',
    'connection.c',

    'protocol_common.c',
    'protocol_noop.c',

    'io_common.c',
    'io_null.c',
    'io_file.c',

    conv_spec,
    conv_spec_headers,

    # Version
    vcs_tag(command: ['git', 'rev-parse', '--short', 'HEAD'],
                      input: 'version.c.in', output: 'version.c',
                      fallback: 'release')
]

if get_option('output').auto()
    sources += 'output.c'
    sources += 'output_packet.c'
    sources += 'encode.c'
    sources += 'connection_scheduler.c'
    sources += 'ldpc_encode.c'
endif

if get_option('input').auto()
    sources += 'decode.c'
    sources += 'reorder.c'
    sources += 'ldpc_decode.c'
endif

if host_machine.system() == 'windows'
    avtransport_soversion = ''
else
    avtransport_soversion = avtransport_api_version_major
endif

inc = include_directories('./include')
libavtransport = library('avtransport',
    sources,
    install: true,

    include_directories: inc,
    dependencies: lib_deps,
    version: avtransport_soname_version,
    soversion: avtransport_soversion,
)

libavtransport_dep = declare_dependency(
    link_with: libavtransport,
    include_directories: inc,
)

# Allows projects to build libavtransport by cloning into ./subprojects/libavtransport
meson.override_dependency('libavtransport', libavtransport_dep)

# Install headers
foreach h : headers
    parts = h.split('/')
    path = meson.project_name()
    foreach p : parts
        if p != parts[-1]
            path = path / p
        endif
    endforeach

    install_headers('include' / meson.project_name() / h, subdir: path)
endforeach

pkg = import('pkgconfig')
pkg.generate(
    name: meson.project_name(),
    description: 'Multimedia container and protocol',
    libraries: libavtransport,
    version: meson.project_version(),
)
