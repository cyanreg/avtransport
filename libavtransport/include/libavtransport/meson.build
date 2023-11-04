inc_dir = get_option('includedir') + '/libavtransport'

conv_spec_headers = custom_target('packet*',
                                  install: true,
                                  install_dir: [ inc_dir, inc_dir ],
                                  input: spec_file,
                                  output: ['packet_enums.h', 'packet_data.h'],
                                  command: [python_exe, spec2c, 'packet_enums,packet_data', '@INPUT@', '@OUTPUT0@', '@OUTPUT1@'])
