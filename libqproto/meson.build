build_opts = [
    '-D_ISOC11_SOURCE', '-D_XOPEN_SOURCE=700', '-U__STRICT_ANSI__',

    # Warnings
    '-Wundef', '-Wmissing-prototypes', '-Wshadow', '-Wparentheses',
    '-Wpointer-arith', '-Wno-pointer-sign',

    # Warnings to treat as errors
    '-Werror=implicit-function-declaration',
]

# Required dependencies
dependencies = [
    # misc
    dependency('threads'),
    cc.find_library('m', required : true),
]

# Headers
headers = [
    'common.h',
    'input.h',
    'output.h'
]

# Base files
sources = [
    'common.c',
    'buffer.c',

    # Version
    vcs_tag(command: ['git', 'rev-parse', '--short', 'HEAD'],
                      input: 'version.c.in', output: 'version.c',
                      fallback: 'release')
]

if get_option('output').auto()
    sources += 'output.c'
    sources += 'output_file.c'
endif

# Check for wmain support (Windows/MinGW)
if cc.links('int wmain() { return 0; }', args: '-municode')
     conf.set('HAVE_WMAIN', 1)
     build_opts += '-municode'
endif

add_global_arguments(build_opts, language: 'c')

inc = include_directories('./include')
lib = library('qproto', sources,
    install: true,
    dependencies: dependencies,
    soversion: ver_major,
    include_directories: inc,
)

libqproto = declare_dependency(
    link_with: lib,
    include_directories: inc,
)

# Allows projects to build libplacebo by cloning into ./subprojects/libqproto
meson.override_dependency('libplacebo', libqproto)

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
    libraries: lib,
    version: meson.project_version(),
)
