avcat_sources = files(
    'avcat.c',
)

avcat_cflags = [ ]
avcat_deps = [ ]

lavc_dep = dependency('', required: false)
lavf_dep = dependency('', required: false)
lavu_dep = dependency('', required: false)

if get_option('ffmpeg').auto()
    lavc_dep = dependency('libavcodec', version: '>= 59.4.100', required: false)
    lavf_dep = dependency('libavformat', version: '>= 58.42.100', required: false)
    lavu_dep = dependency('libavutil', version: '>= 56.43.100', required: false)
    if lavc_dep.found() and lavf_dep.found() and lavu_dep.found()
        avcat_cflags += '-DHAVE_FFMPEG'
        avcat_deps += [ lavc_dep, lavf_dep, lavu_dep ]
    endif
endif

executable('avcat',
           install: true,
           sources: avcat_sources + conv_spec_headers,
           dependencies: avcat_deps + libavtransport_dep,
           c_args: avcat_cflags,
           link_with : [ libavtransport ])

summary({
    'libavcodec': lavc_dep.found(),
    'libavformat': lavf_dep.found(),
    'libavutil': lavu_dep.found(),
}, section: 'avcat', bool_yn: true)
