project('libavtransport', 'c',
    license: 'BSD-2-Clause',
    license_files: ['LICENSE.md'],
    default_options: [ 'buildtype=debugoptimized', 'c_std=c2x', 'warning_level=1' ],
    version: '0.0.2',
    meson_version: '>=1.1.0',
)

conf = configuration_data()

conf.set_quoted('PROJECT_NAME', meson.project_name())
conf.set_quoted('PROJECT_VERSION_STRING', meson.project_version())

version_split = meson.project_version().split('.')
ver_major = version_split[0]
ver_minor = version_split[1]
ver_micro = version_split[2]

conf.set('PROJECT_VERSION_MAJOR', ver_major)
conf.set('PROJECT_VERSION_MINOR', ver_minor)
conf.set('PROJECT_VERSION_MICRO', ver_micro)
if host_machine.endian() == 'big'
    conf.set('CONFIG_BIG_ENDIAN', 1)
else
    conf.set('CONFIG_BIG_ENDIAN', 0)
endif

if get_option('input').auto()
    conf.set('CONFIG_INPUT', 1)
else
    conf.set('CONFIG_INPUT', 0)
endif

if get_option('output').auto()
    conf.set('CONFIG_OUTPUT', 1)
else
    conf.set('CONFIG_OUTPUT', 0)
endif

cc = meson.get_compiler('c')
if cc.has_argument('-fvisibility=hidden')
    add_project_arguments('-fvisibility=hidden', language: 'c')
else
    warning('Compiler does not support -fvisibility=hidden, all symbols will be public!')
endif

pymod = import('python')
python_exe = pymod.find_installation()
spec2c = files('tools/spec2c.py')
spec_file = files('draft-avtransport-spec.bs')

conv_spec = custom_target('packet encode/decode',
                          input: spec_file,
                          output: ['packet_encode.h', 'packet_decode.h'],
                          command: [python_exe, spec2c, 'packet_encode,packet_decode', '@INPUT@', '@OUTPUT0@', '@OUTPUT1@'])

subdir('libavtransport')

if get_option('tools').auto()
    subdir('tools')
endif

configure_file(
    output: 'config.h',
    configuration: conf,
)
