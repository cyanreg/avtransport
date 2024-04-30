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

# Structs and enums generated from the specs
inc_dir = get_option('includedir') + '/avtransport'

avtransport_spec_pub_headers = custom_target('packet*',
    install: true,
    install_dir: [ inc_dir, inc_dir ],
    input: spec_file,
    output: ['packet_enums.h', 'packet_data.h'],
    command: [python_exe, spec2c, 'packet_enums,packet_data', '@INPUT@', '@OUTPUT0@', '@OUTPUT1@']
)

# Version header
version_h_data = configuration_data()
version_h_data.set('AVTRANSPORT_API_VERSION_MAJOR', avtransport_api_version_major)
version_h_data.set('AVTRANSPORT_API_VERSION_MINOR', avtransport_api_version_minor)
version_h_data.set('AVTRANSPORT_API_VERSION_MICRO', avtransport_api_version_micro)
version_h_target = configure_file(input: 'version.h.in',
    output: 'version.h',
    configuration: version_h_data
)

# Headers
lib_headers = [
    'avtransport.h',
    'connection.h',
    'send.h',
    'receive.h',
    'stream.h',
    'rational.h',
    'utils.h',

    version_h_target,
]

install_headers(lib_headers, subdir: meson.project_name())
