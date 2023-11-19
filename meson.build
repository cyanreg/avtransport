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

project('avtransport', 'c',
    license: 'BSD-2-Clause',
    license_files: ['LICENSE.md'],
    default_options: [ 'buildtype=debugoptimized', 'c_std=c2x', 'warning_level=1' ],
    version: '0.0.3',
    meson_version: '>=1.1.0',
)

# library version
avtransport_version_split = meson.project_version().split('.')
avtransport_ver_major = avtransport_version_split[0]
avtransport_ver_minor = avtransport_version_split[1]
avtransport_ver_micro = avtransport_version_split[2]

# api version
avtransport_soname_version    = '0.0.3'
avtransport_api_version_split = avtransport_soname_version.split('.')
avtransport_api_version_major = avtransport_api_version_split[0]
avtransport_api_version_minor = avtransport_api_version_split[1]
avtransport_api_version_micro = avtransport_api_version_split[2]

# Compiler
#============================================================================
build_args = [
    '-D_ISOC11_SOURCE', '-D_XOPEN_SOURCE=700', '-U__STRICT_ANSI__',

    # Warnings
    '-Wundef', '-Wmissing-prototypes', '-Wshadow', '-Wparentheses',
    '-Wpointer-arith', '-Wno-pointer-sign',

    # Warnings to treat as errors
    '-Werror=implicit-function-declaration', '-Werror=missing-prototypes',
    '-Werror=vla',
]

cc = meson.get_compiler('c')

# Check for wmain support (Windows/MinGW)
#============================================================================
if cc.links('int wmain() { return 0; }', args: '-municode')
     conf.set('HAVE_WMAIN', 1)
     build_args += '-municode'
endif

# Hide all untagged API functions
#============================================================================
if cc.has_argument('-fvisibility=hidden')
    build_args += '-fvisibility=hidden'
else
    warning('Compiler does not support -fvisibility=hidden, all symbols will be public!')
endif

# Add default build ops
#============================================================================
add_project_arguments(build_args, language: 'c')

# libFuzzer stuff
#============================================================================
fuzzing_engine = get_option('fuzzing_engine')
if fuzzing_engine == 'libfuzzer'
    if not cc.has_argument('-fsanitize=fuzzer')
        error('fuzzing_engine libfuzzer requires "-fsanitize=fuzzer"')
    endif
    fuzzer_args = ['-fsanitize=fuzzer-no-link', '-fsanitize=fuzzer']
    add_project_arguments(cc.first_supported_argument(fuzzer_args), language : 'c')
endif

# Dependencies
#============================================================================
threads_dep = dependency('threads', required: true)
uring_dep = dependency('liburing', required: false)
zstd_dep = dependency('libzstd', required: false)
lib_deps = [
    cc.find_library('m', required: true),
    threads_dep,

    uring_dep,
    zstd_dep,
]

# Configure file
#============================================================================
conf = configuration_data()
conf.set_quoted('PROJECT_NAME', meson.project_name())
conf.set_quoted('PROJECT_VERSION_STRING', meson.project_version())

conf.set('PROJECT_VERSION_MAJOR', avtransport_ver_major)
conf.set('PROJECT_VERSION_MINOR', avtransport_ver_minor)
conf.set('PROJECT_VERSION_MICRO', avtransport_ver_micro)

conf.set('CONFIG_BIG_ENDIAN', host_machine.endian() == 'big')
conf.set('CONFIG_INPUT', get_option('input').auto())
conf.set('CONFIG_OUTPUT', get_option('output').auto()) 

conf.set('CONFIG_HAVE_LIBURING', uring_dep.found())
conf.set('CONFIG_HAVE_LIBZSTD', zstd_dep.found())

if get_option('assert') > -1
    conf.set('CONFIG_ASSERT_LEVEL', get_option('assert'))
else
    if get_option('buildtype') == 'release'
        conf.set('CONFIG_ASSERT_LEVEL', 0)
    elif get_option('buildtype') == 'debugoptimized'
        conf.set('CONFIG_ASSERT_LEVEL', 1)
    else
        conf.set('CONFIG_ASSERT_LEVEL', 2)
    endif
endif

# Check for pthread_setname_np
#============================================================================
if cc.has_function('pthread_setname_np', prefix: '#include <pthread.h>',
                   args: [ '-D_GNU_SOURCE' ], dependencies: threads_dep)
    conf.set('HAVE_PTHREAD_SETNAME_NP', 1)
    add_project_arguments('-D_GNU_SOURCE', language: 'c')
endif

conf.set('CONFIG_DEFAULT_PORT', '8212')
conf.set('CONFIG_DEFAULT_PORT_STR', '"8212"')
conf.set('CONFIG_DEFAULT_TYPE', 'AVT_PROTOCOL_UDP')

# spec2py
#============================================================================
pymod = import('python')
python_exe = pymod.find_installation()
spec2c = files('tools/spec2c.py')
spec_file = files('draft-avtransport-spec.bs')

conv_spec = custom_target(
    'packet encode/decode',
    input: spec_file,
    output: ['packet_encode.h', 'packet_decode.h'],
    command: [python_exe, spec2c, 'packet_encode,packet_decode', '@INPUT@', '@OUTPUT0@', '@OUTPUT1@']
)

# Build
#============================================================================
subdir('libavtransport')

if get_option('tools').auto()
    subdir('tools')
endif

configure_file(
    output: 'config.h',
    configuration: conf,
)
