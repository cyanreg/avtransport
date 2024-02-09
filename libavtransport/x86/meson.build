conf_nasm = conf

conf_nasm.set10('ARCH_X86', host_machine.cpu_family().startswith('x86'))
conf_nasm.set10('ARCH_X86_64', host_machine.cpu_family() == 'x86_64')
conf_nasm.set10('ARCH_X86_32', host_machine.cpu_family() == 'x86')
conf_nasm.set('private_prefix', 'avt')

# Convert SSE asm into (128-bit) AVX when compiler flags are set to use AVX instructions
conf_nasm.set10('FORCE_VEX_ENCODING', cc.get_define('__AVX__').strip() != '')

nasm_config = configure_file(
    output: 'config.asm',
    configuration: conf_nasm,
    output_format: 'nasm',
)

if get_option('output').auto()
#    sources = sources.without([
#        'ldpc_encode.c'
#    ])
    sources += [
        'x86/ldpc_encode.asm',
    ]
endif
