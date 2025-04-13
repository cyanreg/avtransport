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

lavc_dep = dependency('', required: false)
lavf_dep = dependency('', required: false)
lavu_dep = dependency('', required: false)

if get_option('ffmpeg').auto()
    lavc_dep = dependency('libavcodec', version: '>= 59.4.100', required: false)
    lavf_dep = dependency('libavformat', version: '>= 58.42.100', required: false)
    lavu_dep = dependency('libavutil', version: '>= 56.43.100', required: false)
endif

# Generate local configure file
conf_tools = conf

configure_file(
    output: 'config.h',
    configuration: conf_tools,
)

# avcat
#============================================================================
avcat_sources = files(
    'avcat.c',
    'avcat/io.c',
)

avcat_deps = [
    lavc_dep,
    lavf_dep,
    lavu_dep,
]

avcat_cflags = [ ]
if lavc_dep.found() and lavf_dep.found() and lavu_dep.found()
    avcat_cflags += '-DHAVE_FFMPEG'
endif

executable('avcat',
    install: true,
    sources: avcat_sources + avtransport_spec_pub_headers,
    dependencies: avcat_deps,
    c_args: avcat_cflags,

    include_directories: avtransport_inc,
    link_with: [ avtransport_lib ]
)

summary({
    'ffmpeg libs': lavc_dep.found() and lavf_dep.found() and lavu_dep.found(),
}, section: 'avcat', bool_yn: true)

# avtdump
#============================================================================
avtdump_sources = files(
    'avtdump.c',
)

executable('avtdump',
    install: true,
    sources: avtdump_sources + avtransport_spec_pub_headers,

    include_directories: avtransport_inc,
    link_with: [ avtransport_lib ]
)
