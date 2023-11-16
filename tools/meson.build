avcat_sources = files(
    'avcat.c',
)

cli_deps = [ ]
if get_option('ffmpeg').auto()
    cli_deps += dependency('libavcodec', version: '>= 59.4.100')
    cli_deps += dependency('libavformat', version: '>= 58.42.100')
    cli_deps += dependency('libavutil', version: '>= 56.43.100')
endif

executable('avcat',
           install: true,
           sources: avcat_sources + conv_spec_headers,
           dependencies: cli_deps + libavtransport_dep,
           link_with : [ libavtransport ])
