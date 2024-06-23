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

project('avtransport',
    [ 'c' ],
    license: 'BSD-2-Clause',
    license_files: [ 'LICENSE.md' ],
    default_options: [
        'buildtype=debugoptimized',
        'c_std=c23',
        'warning_level=1'
    ],
    version: '0.0.4',
    meson_version: '>=1.4.0',
)

spec_file = files('draft-avtransport-spec.bs')

# library version
avtransport_version_split = meson.project_version().split('.')
avtransport_ver_major = avtransport_version_split[0]
avtransport_ver_minor = avtransport_version_split[1]
avtransport_ver_micro = avtransport_version_split[2]

# api version
avtransport_soname_version    = '0.0.4'
avtransport_api_version_split = avtransport_soname_version.split('.')
avtransport_api_version_major = avtransport_api_version_split[0]
avtransport_api_version_minor = avtransport_api_version_split[1]
avtransport_api_version_micro = avtransport_api_version_split[2]

# Python
#============================================================================
pymod = import('python')
python_exe = pymod.find_installation()

# Compiler
#============================================================================
build_args = [
    '-D_ISOC11_SOURCE',
    '-D_ISOC2X_SOURCE',
    '-D_ISOC23_SOURCE',
    '-U__STRICT_ANSI__',

    # Warnings
    '-Wundef', '-Wmissing-prototypes', '-Wshadow', '-Wparentheses',
    '-Wpointer-arith', '-Wno-pointer-sign',

    # Warnings to treat as errors
    '-Werror=implicit-function-declaration', '-Werror=missing-prototypes',
    '-Werror=vla',
]

cc = meson.get_compiler('c')

# Check C23 headers
if cc.has_header('stdckdint.h') == false
    error('Compiler lacks stdckdint.h, cannot compile!')
endif
#if cc.has_header('stdbit.h') == false
#    error('System lacks stdbit.h, cannot compile!')
#endif

# Check for 128-bit integer support (required for timestamp handling)
if cc.has_type('_BitInt(128)') == false and cc.has_type('__int128') == false
    if cc.has_type('__int128', args: [ '-D__SIZEOF_INT128__' ]) == false
        error('Compiler lacks support for 128-bit integers!')
    else
        build_args += '-D__SIZEOF_INT128__'
    endif
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
xxh_dep = dependency('libxxhash', required: false)
cbor_dep = dependency('libcbor', required: false)

xxh_dep = dependency('libxxhash', required: false)
uring_dep = dependency('liburing', required: false)
zstd_dep = dependency('libzstd', required: false)
openssl_dep = dependency('openssl', required: false, version : '>3.4.0')
brotlienc_dep = dependency('libbrotlienc', required: false)
brotlidec_dep = dependency('libbrotlienc', required: false)

# External dep fallback
#======================
cmake = import('cmake')

if cbor_dep.found() == false
    cbor_ext_opts = cmake.subproject_options()
    cbor_ext_opts.add_cmake_defines({'CMAKE_POSITION_INDEPENDENT_CODE': true})

    cbor_ext = cmake.subproject('libcbor', options: cbor_ext_opts, required: true)
    cbor_dep = cbor_ext.dependency('cbor')
endif

if brotlienc_dep.found() == false or brotlidec_dep.found() == false
    brotli_ext_opts = cmake.subproject_options()
    brotli_ext_opts.add_cmake_defines({'CMAKE_POSITION_INDEPENDENT_CODE': true})
    brotli_ext_opts.add_cmake_defines({'BROTLI_BUILD_TOOLS': false})

    brotli_ext = cmake.subproject('brotli', options: brotli_ext_opts, required: false)
    if brotli_ext.found()
        brotlienc_dep = brotli_ext.dependency('brotlienc')
        brotlidec_dep = brotli_ext.dependency('brotlidec')
    endif
endif

# Configure file
#============================================================================
conf = configuration_data()
conf.set_quoted('PROJECT_NAME', meson.project_name())
conf.set_quoted('PROJECT_VERSION_STRING', meson.project_version())

conf.set('PROJECT_VERSION_MAJOR', avtransport_ver_major)
conf.set('PROJECT_VERSION_MINOR', avtransport_ver_minor)
conf.set('PROJECT_VERSION_MICRO', avtransport_ver_micro)

conf.set('CONFIG_DEFAULT_PORT', '8212')
conf.set('CONFIG_DEFAULT_PORT_STR', '"8212"')
conf.set('CONFIG_DEFAULT_TYPE', 'AVT_PROTOCOL_UDP')

# Configuration settings
#============================================================================
conf.set('CONFIG_BIG_ENDIAN', host_machine.endian() == 'big')
conf.set('CONFIG_HAVE_LIBBROTLIENC', brotlienc_dep.found())
conf.set('CONFIG_HAVE_LIBBROTLIDEC', brotlidec_dep.found())
conf.set('CONFIG_HAVE_LIBURING', uring_dep.found())
conf.set('CONFIG_HAVE_LIBZSTD', zstd_dep.found())
conf.set('CONFIG_HAVE_OPENSSL', openssl_dep.found())
conf.set('CONFIG_HAVE_LIBXXH', xxh_dep.found())

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

if cc.has_function('fallocate', prefix: '#include <fcntl.h>', args: '-D_GNU_SOURCE')
    conf.set('CONFIG_HAVE_FALLOCATE', 1)
endif

if cc.has_function('posix_fallocate', prefix: '#include <fcntl.h>', args: '-D_POSIX_C_SOURCE=200112L')
    conf.set('CONFIG_HAVE_POSIX_FALLOCATE', 1)
endif

if cc.has_function('mremap', prefix: '#include <sys/mman.h>', args: '-D_GNU_SOURCE')
    conf.set('CONFIG_HAVE_MREMAP', 1)
endif

# Opt-in into 64-bit time if not already defined on 32-bit platforms
if (cc.sizeof('void *') == 4
    and cc.has_header_symbol('time.h', '__GLIBC__')
    and not cc.has_header_symbol('time.h', '_TIME_BITS'))
    conf.set('_TIME_BITS', '64')
endif

# Check for wendys
#============================================================================
if host_machine.system() == 'windows'

endif

# Build
#============================================================================
subdir('libavtransport')

if get_option('tools').auto()
    subdir('tools')
endif
