#!/usr/bin/env python3
#
# Copyright © 2023 Lynne
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the “Software”), to deal in the Software without
# restriction, including without limitation the rights to use,
# copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following
# conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
# OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.

import itertools
import re
import sys
import datetime
import math
from bs4 import BeautifulSoup

f_input = None
f_packet_enums = None
f_packet_data = None
f_packet_encode = None
f_packet_decode = None

# Convert '/', '$' and '.' to '_',
# or if this is stdin just use "stdin" as the name.
argc = len(sys.argv)
if argc < 4:
    print("Invalid arguments: need <list of headers> <input> <output file names, comma separated>", file=sys.stderr)
    exit(1)

f_input = sys.argv[2]

mode = sys.argv[1].split(",")
for i,  m in enumerate(mode):
    if (3 + i) >= len(sys.argv):
        print("Invalid arguments: need <list of headers> <input> <space separated outputs>!", file=sys.stderr)
        exit(22)
    if m == "packet_enums":
        f_packet_enums = sys.argv[3 + i]
    elif m == "packet_data":
        f_packet_data = sys.argv[3 + i]
    elif m == "packet_encode":
        f_packet_encode = sys.argv[3 + i]
    elif m == "packet_decode":
        f_packet_decode = sys.argv[3 + i]
    else:
        print("Invalid list: expected 'packet_enums', 'packet_data', 'packet_encode', 'packet_decode'")
        exit(22)

# Parse spec
with open(f_input) as text:
   soup = BeautifulSoup(text, "html.parser")

# Get the list of descriptor ranges
desc_list_tab = soup.find(id="descriptors_list")
desc_pairs = desc_list_tab.find_all('tr')

MAX_BITFIELD_LEN = 32

# Struct/function prefix
data_prefix = "AVT"
fn_prefix = "avt_"
bsw = {
    "pad":  "avt_bsw_zpad",
    "buf":  "avt_bsw_sbuf",
    "rat":  "avt_bsw_rtbe",
    "int":  "avt_bsw_",
    "fstr":  "avt_bsw_fstr",
    "len":  "avt_bs_offs",
    "ldpc": "avt_bsw_ldpc_",
    "skip": "avt_bs_skip",
}

bsr = {
    "buf":  "avt_bsr_sbuf",
    "rat":  "avt_bsr_rtbe",
    "int":  "avt_bsr_",
    "fstr":  "avt_bsr_fstr",
    "len":  "avt_bs_offs",
    "ldpc": "avt_bsr_ldpc_",
    "skip": "avt_bs_skip",
}

enums = { }
descriptors = { "FLAG_LSB_BITMASK": (1 << 31) }
packet_structs = { }
templated_structs = { }
templates = { }
orig_desc_names = { }
struct_sizes = { }
substructs = [ ]
anchor_name_remap = { }

extra_values = {
    "AVTStreamData": [
        "dts", {
            "fixed": None,
            "ldpc": None,
            "enum": None,
            "size_bits": 64,
            "bytestream": 8,
            "datatype": "int64_t",
            "struct": None,
            "array_len": 0,
            "string": False,
            "payload": False,
        }
    ],
}

def iter_enum_fields(target_id):
    if target_id in enums:
        return

    enum = soup.find(id=target_id)
    if enum == None:
        print("Unknown enum:", target_id)
        exit(22)

    enum_vals = { }
    dfns = enum.find_all('dfn')
    for name in dfns:
        enum_vals[name.string] = int(name.find_next("i").string, 16)

    print("Parsed enum", target_id)
    enums[target_id] = enum_vals

# Iterate over each field of a data structure
def iter_data_fields(target_id, struct_name, anchor_name):
    total_bits = 0
    original_desc_name = None
    original_struct_name = struct_name
    struct_fields = { }
    bits_since_last_ldpc = 0
    for i in itertools.count():
        field_tab = soup.find(id=target_id + "+" + str(i))
        if field_tab == None:
            break

        fields = field_tab.find_all('td')

        if i == 0 and struct_name == None:
            original_desc_name = fields[1].string[:-11]
            original_struct_name = ''.join(original_desc_name.title().split("_"))
            struct_name = data_prefix + original_struct_name
        elif i == 0:
            original_desc_name = fields[1].string[:-11]
            original_struct_name = struct_name
            struct_name = data_prefix + original_struct_name

        if fields[2].string and fields[2].string.startswith("[[#generic"):
            template_descriptor = int(fields[0].string.strip("'"), 16)
            descriptors[fields[1].string[:-11]] = template_descriptor

            print("Parsed templated struct", original_struct_name)
            templated_structs[struct_name] = dict({
                "name": fields[1].string[:-11],
                "descriptor": template_descriptor,
                "descriptor_name": original_desc_name,
                "template": anchor_name_remap[fields[2].string.strip("[]")],
            })
            return templated_structs[struct_name]

        fixed_number = None
        ldpc = None
        enum = None
        template = None
        struct = None
        string = False
        payload = False

        if len(fields[2]) > 0:
            data = fields[2].string.strip("'[=]")
            if data.startswith("0x"):
                fixed_number = int(data, 16);
            elif data == "payload":
                payload = True
            elif data.startswith("LDPC("):
                ldpc = [int(x) for x in data[5:-1].split(',')]
                if bits_since_last_ldpc != ldpc[1]:
                    print("Mismatching #bits between LDPC:", bits_since_last_ldpc, "vs", ldpc[1])
                    exit(22)
                bits_since_last_ldpc = 0
            elif data.startswith("enum"):
                enum = data[5:].strip("{}")
                iter_enum_fields(enum)
            elif data.startswith("struct"):
                struct = data[7:].strip("{}")
                if struct.startswith("[[#"):
                    struct = struct.split("|")[1]
                if iter_data_fields(struct, struct, None) == None:
                    print("Unknown struct:", struct)
                    exit(22)
                else:
                    substructs.append(data_prefix + struct)
            elif 'string' in data:
                string = True

        datatype = None
        size_bits = 0
        bytestream = 0
        array_len = 0
        type_str = fields[0].string
        type_mult = type_str[0:-1].split('*')
        if len(type_mult) > 1:
            if type_mult[0].startswith("[="):
                array_len = type_mult[0][2:-2]
            elif type_mult[0][0].isdigit():
                array_len = int(re.search('[0-9]+', type_mult[0]).group())
            else:
                print("Unknown multiplier in:", fields[0])
                exit(22)

        datapart = type_mult[-1].split("(")
        size_bits = int(datapart[1])

        if all(x in "uibR" for x in datapart[0]) == False:
            print("Unknown data type in:", fields[0])
            exit(22)
        elif struct != None:
            datatype = data_prefix + struct
            bytestream = size_bits // 8
        elif ((size_bits & (size_bits - 1) != 0) and ((datapart[0] != "b") or (array_len > 0))):
            print("Invalid data type in:", fields[0])
            exit(22)
        elif string:
            datatype = "char8_t"
            bytestream = 1
        elif enum != None:
            datatype = "enum " + data_prefix + enum
            bytestream = size_bits // 8
        elif size_bits <= 8:
            datatype = "uint8_t"
            if size_bits < 8:
                bytestream = 0
            else:
                bytestream = 1
        elif (size_bits & (size_bits - 1) != 0) and array_len > 0:
            datatype = "uint8_t"
            array_len = size_bits // 8
            bytestream = size_bits // 8
        elif datapart[0] == "R" and size_bits == 64:
            datatype = data_prefix + "Rational"
            bytestream = 64 // 8
        elif datapart[0] == "b" or datapart[0] == "u" and \
             ((size_bits == 8 and array_len == 0) or \
              (size_bits >= 8 and (size_bits & (size_bits - 1) == 0))):
            datatype = "uint" + str(size_bits) + "_t"
            bytestream = size_bits // 8
        elif datapart[0] == "i" and \
              ((size_bits == 8 and array_len == 0) or \
               (size_bits >= 8 and (size_bits & (size_bits - 1) == 0))):
            datatype = "int" + str(size_bits) + "_t"
            bytestream = size_bits // 8
        else:
            print("Unknown data type in:", fields[0])
            exit(22)

        if i == 0 and fixed_number != None: # Descriptor
            if size_bits == 16:
                descriptors[fields[1].string[:-11]] = fixed_number
            elif size_bits == 8:
                descriptors[fields[1].string[:-11]] = (fixed_number << 8) | descriptors["FLAG_LSB_BITMASK"]
            elif target_id[0].isdigit():
                print("Unknown descriptor in:", fields[0])
                exit(22)

        field_name = fields[1].string.strip("[=]")
        if field_name in struct_fields:
            field_name += "_2"

        if field_name.endswith("descriptor"):
            datatype = "enum AVTPktDescriptors"
            if size_bits < 16 and fixed_number != None:
                fixed_number <<= 8;
        elif "seq" in field_name:
            datatype = "uint64_t"

        struct_fields[field_name] = dict({
            "fixed": fixed_number,
            "ldpc": ldpc,
            "enum": enum,
            "size_bits": size_bits,
            "bytestream": bytestream,
            "datatype": datatype,
            "struct": struct,
            "array_len": array_len,
            "string": string,
            "payload": payload,
        })

        if type(array_len) == int and array_len > 1:
            size_bits *= array_len

        if payload == False and struct == None and string == False or (string == True and type(array_len) == int):
            total_bits += size_bits

        if ldpc == None:
            bits_since_last_ldpc += size_bits

    if total_bits == 0:
        return None

    print("Parsed struct", original_struct_name, total_bits)

    if struct_name.startswith("generic") == False:
        packet_structs[struct_name] = struct_fields
        orig_desc_names[struct_name] = original_desc_name
        struct_sizes[struct_name] = total_bits
        anchor_name_remap[anchor_name] = struct_name

    return struct_fields

# Load all templates
templates = {
    "#generic-data-packets": iter_data_fields("GenericData", None, "#generic-data-packets"),
    "#generic-data-segment-packets": iter_data_fields("GenericDataSegment", None, "#generic-data-segment-packets"),
    "#generic-data-parity-packets": iter_data_fields("GenericDataParity", None, "#generic-data-parity-packets"),
}

for desc_pair in desc_pairs[1:]: # Skip table header
    pair = desc_pair.find_all('td')
    val = pair[0].string.split(":")
    pkt = pair[1].string.strip("[#]").split('-')
    pkt = map(str.title, pkt)
    desc_start = None
    desc_end = None

    if len(val) == 1:
        desc_end = desc_start = int(val[0].strip("'"), 16)
    elif len(val) == 2:
        desc_start = int(val[0].strip("[']"), 16)
        desc_end = int(val[1].strip("[']"), 16)

    for i in range(desc_start, desc_end + 1):
        if iter_data_fields("0x" + format(i, '04X'), None, pair[1]) == None:
            break;

copyright_header = "/*\n * Copyright © " + datetime.datetime.now().date().strftime("%Y") + ''', Lynne
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
'''

autogenerate_note = "/* This file is automatically generated from " + f_input + ", do not edit */\n"

if f_packet_enums != None:
    file_enums = open(f_packet_enums, "w+")
    file_enums.write(copyright_header + "\n")
    file_enums.write(autogenerate_note)
    file_enums.write("#ifndef AVTRANSPORT_PACKET_ENUMS_H\n")
    file_enums.write("#define AVTRANSPORT_PACKET_ENUMS_H\n" + "\n")

    file_enums.write("#define AVT_MAX_HEADER_LEN " + str(384) + "\n\n")

    file_enums.write("#define AVT_SESSION_ID " + "((0x" + format(descriptors["session_start"], "X") + ") << 16)")
    file_enums.write(") | 0x" + format(packet_structs["AVTSessionStart"]["session_version"]["fixed"], "X") + ")\n\n")

    file_enums.write("enum " + data_prefix + "PktDescriptors {\n")
    for name, desc in descriptors.items():
        add_flag = False
        if desc > descriptors["FLAG_LSB_BITMASK"]:
            desc &= ~descriptors["FLAG_LSB_BITMASK"]
            add_flag = True;
        file_enums.write("    " + data_prefix + "_PKT_" + name.upper() + " = " + "0x" + format(desc, "X"))
        if add_flag:
            file_enums.write(" | AVT_PKT_FLAG_LSB_BITMASK")
        file_enums.write(",\n")
    file_enums.write("};\n")

    for enum, dfns in enums.items():
        file_enums.write("\nenum " + data_prefix + enum + " {\n")
        for name, val in dfns.items():
            file_enums.write("    " + data_prefix + "_" + name + " = 0x" + format(val, "X") + ",\n")
        file_enums.write("};\n")
    file_enums.write("\n#endif /* AVTRANSPORT_PACKET_ENUMS_H */\n")
    file_enums.close()

if f_packet_data != None:
    file_structs = open(f_packet_data, "w+")
    file_structs.write(copyright_header + "\n")
    file_structs.write("#ifndef AVTRANSPORT_PACKET_DATA\n")
    file_structs.write("#define AVTRANSPORT_PACKET_DATA\n" + "\n")
    file_structs.write(autogenerate_note + "\n")
    file_structs.write("#include <stdint.h>\n")
    file_structs.write("#include <uchar.h>\n")
    file_structs.write("#include <stddef.h>\n\n")
    file_structs.write("#include <avtransport/packet_enums.h>\n")
    file_structs.write("#include <avtransport/rational.h>\n")

    def write_struct_fields(name, field, newline_carryover):
        if name.startswith("padding") or field["payload"] or name == "global_seq" or name.endswith("descriptor"):
            return
        if newline_carryover:
            file_structs.write("\n")
            newline_carryover = False;
        if field["ldpc"] != None:
            newline_carryover = True
            return
        file_structs.write("    " + field["datatype"] + " ")
        if type(field["array_len"]) != int:
            file_structs.write("*")
        file_structs.write(name)
        if type(field["array_len"]) == int:
            if type(field["array_len"]) == str:
                file_structs.write("[" + "" + "]")
            elif field["array_len"] > 0:
                file_structs.write("[" + str(field["array_len"]) + "]")
        file_structs.write(";\n")

    for struct, fields in packet_structs.items():
        file_structs.write("\ntypedef struct " + struct + " {\n")

        # To enable type punning via unions
        name_desc = None
        name_seq = None
        for name, field in fields.items():
            if name.endswith("descriptor") and name_desc == None:
                name_desc = name
            elif name == "global_seq" and name_seq == None:
                name_seq = name

        if name_desc != None:
            field = fields[name_desc]
            file_structs.write("    " + field["datatype"] + " " + name_desc + ";\n")
        if name_seq != None:
            field = fields[name_seq]
            file_structs.write("    " + field["datatype"] + " " + name_seq + ";\n")

        newline_carryover = False;
        for name, field in fields.items():
            write_struct_fields(name, field, newline_carryover)

        for estruct, data in extra_values.items():
            if struct == estruct:
                write_struct_fields(data[0], data[1], newline_carryover)

        file_structs.write("} " + struct + ";\n")

    file_structs.write("\nunion " + data_prefix + "PacketData {\n")
    file_structs.write("    struct {\n")
    file_structs.write("    enum AVTPktDescriptors desc;\n")
    file_structs.write("        uint64_t seq;\n")
    file_structs.write("    };\n")
    for struct, fields in packet_structs.items():
        if struct not in substructs:
            file_structs.write("    " + struct + " " + orig_desc_names[struct] + ";\n")
    file_structs.write("};\n")

    file_structs.write("\nstatic inline size_t avt_pkt_hdr_size(union AVTPacketData pkt)\n")
    file_structs.write("{\n")
    file_structs.write("    switch (pkt.desc) {\n")

    for name, size in struct_sizes.items():
        if name in substructs or orig_desc_names[name].startswith("generic"):
            continue
        file_structs.write("    case " + (data_prefix + "_PKT_" + orig_desc_names[name]).upper() + ":\n")
        file_structs.write("        return " + str(size >> 3) + ";\n")
    for name, tstruct in templated_structs.items():
        file_structs.write("    case " + (data_prefix + "_PKT_" + tstruct["descriptor_name"]).upper() + ":\n")
        file_structs.write("        return " + str(struct_sizes[tstruct["template"]] >> 3) + ";\n")

    file_structs.write("    default:\n")
    file_structs.write("        break;\n")
    file_structs.write("    }\n")
    file_structs.write("    return 0;\n")
    file_structs.write("}\n")

    file_structs.write("\n#endif /* AVTRANSPORT_PACKET_DATA_H */\n")
    file_structs.close()

if f_packet_encode != None:
    file_encode = open(f_packet_encode, "w+")
    file_encode.write(copyright_header + "\n")
    file_encode.write(autogenerate_note + "\n")
    file_encode.write("#ifndef AVTRANSPORT_ENCODE_H\n")
    file_encode.write("#define AVTRANSPORT_ENCODE_H\n\n")
    file_encode.write("#include <avtransport/packet_data.h>\n\n")
    file_encode.write("#include \"bytestream.h\"\n")
    file_encode.write("#include \"utils_internal.h\"\n")
    file_encode.write("#include \"ldpc_encode.h\"\n")
    for struct, fields in packet_structs.items():
        had_bitfield = False
        bitfield = False
        newline_carryover = False;
        bitfield_bit = 0
        indent = "    "
        file_encode.write("\nstatic void inline " + fn_prefix + "encode_" + orig_desc_names[struct] + "(" + data_prefix + "Bytestream *bs, const " + struct + " p)" + "\n{\n")
        def wsym(indent, sym, name, field):
            # Start
            if field["struct"] != None:
                file_encode.write(indent + fn_prefix + "encode_" + orig_desc_names[data_prefix + field["struct"]])
            elif sym == bsw["int"]:
                file_encode.write(indent + sym)
                if field["datatype"][0] == 'i':
                    file_encode.write("i" + str(field["bytestream"]*8) + "b")
                else:
                    file_encode.write("u" + str(field["bytestream"]*8) + "b")
            elif sym == bsw["ldpc"]:
                file_encode.write(indent + sym + str(field["ldpc"][0]) + "_" + str(field["ldpc"][1]))
            else:
                file_encode.write(indent + sym)

            if field["bytestream"] == 1 and sym == bsw["int"]:
                file_encode.write(" ")
            file_encode.write("(bs")

            # Length or name
            if sym == bsw["buf"]:
                file_encode.write(", p." + name)
            elif sym == bsw["pad"]:
                file_encode.write(", " + str(field["bytestream"]))

            # Name or length
            if sym == bsw["buf"]:
                if field["array_len"]:
                    file_encode.write(", p." + str(field["array_len"]))
                else:
                    file_encode.write(", " + str(field["bytestream"]))
            elif sym != bsw["pad"] and sym != bsw["ldpc"]:
                if field["struct"] != None:
                    file_encode.write(", p." + name)
                else:
                    file_encode.write(", p." + name)

            if name == "global_seq" or name == "target_seq":
                file_encode.write(" & UINT32_MAX")
            elif name.endswith("descriptor"):
                file_encode.write(" & UINT16_MAX")
                if field["size_bits"] < 16:
                    file_encode.write(" >> 8")

            # Array index
            if ((type(field["array_len"]) == int and field["array_len"] > 1) or \
                (type(field["array_len"]) == str and field["bytestream"] > 1)) and \
               sym != bsw["fstr"]:
                file_encode.write("[i]")

            if sym == bsw["fstr"]:
                file_encode.write(", " + str(field["array_len"]))

            file_encode.write(");\n")

        for name, field in fields.items():
            indent = "    "
            write_sym = bsw["int"]
            if field["payload"]:
                continue;
            elif field["datatype"] == data_prefix + "Rational":
                write_sym = bsw["rat"]
            elif field["ldpc"] != None:
                write_sym = bsw["ldpc"]
            elif name.startswith("padding") and field["bytestream"] > 0:
                write_sym = bsw["pad"]
            elif (type(field["array_len"]) == str and field["bytestream"] == 1) or field["ldpc"] != None:
                write_sym = bsw["buf"]
            elif field["string"]:
                write_sym = bsw["fstr"]

            if newline_carryover:
                file_encode.write("\n")
            newline_carryover = False;
            if field["ldpc"] != None:
                newline_carryover = True

            if field["bytestream"] == 0 and bitfield == False: # Setup bitfield writing
                if had_bitfield == False:
                    file_encode.write(indent + "uint" + str(MAX_BITFIELD_LEN) + "_t bitfield;\n");
                bitfield = True
                had_bitfield = True
                bitfield_bit = (MAX_BITFIELD_LEN - 1)

            if bitfield:
                if bitfield_bit == (MAX_BITFIELD_LEN - 1):
                    file_encode.write(indent + "bitfield  = ")
                else:
                    file_encode.write(indent + "bitfield |= ")
                file_encode.write("p." + name + " << (" + str(bitfield_bit + 1) + " - " + str(field["size_bits"]) + ");\n")
                bitfield_bit -= field["size_bits"]
            else:
                if write_sym != bsw["fstr"] and (type(field["array_len"]) == int and field["array_len"] > 1):
                    file_encode.write(indent + "for (int i = 0; i < " + str(field["array_len"]) + "; i++)\n")
                    indent = indent + "    "
                if write_sym != bsw["fstr"] and (type(field["array_len"]) == str and field["bytestream"] > 1):
                    file_encode.write(indent + "for (int i = 0; i < p." + field["array_len"] + "; i++)\n")
                    indent = indent + "    "
                wsym(indent, write_sym, name, field)

            if bitfield and (MAX_BITFIELD_LEN - bitfield_bit - 1) >= 8 and \
               math.log2(MAX_BITFIELD_LEN - bitfield_bit - 1).is_integer(): # Terminate bitfield
                file_encode.write(indent + bsw["int"] + "u" + str(MAX_BITFIELD_LEN - bitfield_bit - 1) + "b (bs, bitfield);\n")
                bitfield = False
        file_encode.write("}\n")
    file_encode.write("\n#endif /* AVTRANSPORT_ENCODE_H */\n")
    file_encode.close()

if f_packet_decode != None:
    file_decode = open(f_packet_decode, "w+")
    file_decode.write(copyright_header + "\n")
    file_decode.write(autogenerate_note + "\n")
    file_decode.write("#ifndef AVTRANSPORT_DECODE_H\n")
    file_decode.write("#define AVTRANSPORT_DECODE_H\n\n")
    file_decode.write("#include <avtransport/packet_data.h>\n\n")
    file_decode.write("#include \"bytestream.h\"\n")
    file_decode.write("#include \"buffer.h\"\n")
    file_decode.write("#include \"utils_internal.h\"\n")
    file_decode.write("#include \"ldpc_decode.h\"\n")
    for struct, fields in packet_structs.items():
        had_bitfield = False
        bitfield = False
        newline_carryover = False;
        bitfield_bit = 0
        indent = "    "
        substruct = False

        if struct in substructs:
            substruct = True

        has_payload = False
        for name, field in fields.items():
            if field["payload"] == True:
                has_payload = True
                break

        if substruct:
            file_decode.write("\nstatic inline void " + fn_prefix + "decode_" + orig_desc_names[struct] + "(" + data_prefix + "Bytestream *bs, " + struct + " *p")
        else:
            file_decode.write("\nstatic inline int64_t " + fn_prefix + "decode_" + orig_desc_names[struct] + "(" + data_prefix + "Buffer *buf, " + struct + " *p")
            if has_payload:
                file_decode.write(", " + data_prefix + "Buffer *pl")
        file_decode.write(")\n{\n")
        if substruct == False:
            file_decode.write("    size_t len;\n")
            file_decode.write("    uint8_t *data = avt_buffer_get_data(buf, &len);\n")
            file_decode.write("    AVTBytestream bss = avt_bs_init(data, len), *bs = &bss;\n\n")
        def rsym(indent, sym, name, field):
            # Start
            if (sym == bsr["int"] or sym == bsr["rat"]) and field["struct"] == None:
                file_decode.write(indent + "p->" + name)
                # Array index
                if ((type(field["array_len"]) == int and field["array_len"] > 1) or \
                    (type(field["array_len"]) == str and field["bytestream"] > 1)) and \
                    sym != bsr["fstr"]:
                    file_decode.write("[i]")
                file_decode.write(" = ")
            else:
                file_decode.write(indent)

            if field["struct"] != None:
                file_decode.write(fn_prefix + "decode_" + orig_desc_names[data_prefix + field["struct"]])
            elif sym == bsr["int"]:
                file_decode.write(sym)
                if field["datatype"][0] == 'i':
                    file_decode.write("i" + str(field["bytestream"]*8) + "b")
                else:
                    file_decode.write("u" + str(field["bytestream"]*8) + "b")
            elif sym == bsr["ldpc"]:
                file_decode.write(sym + str(field["ldpc"][0]) + "_" + str(field["ldpc"][1]))
            else:
                file_decode.write(sym)

            file_decode.write("(bs")
            if field["struct"] != None:
                file_decode.write(", p->" + name)

            # Length
            if sym == bsr["buf"]:
                if field["array_len"]:
                    file_decode.write(", p->" + name)
                else:
                    file_decode.write(", " + str(field["bytestream"]))
            if sym == bsr["skip"]:
                file_decode.write(", " + str(field["bytestream"]))

            if sym == bsr["fstr"]:
                file_decode.write(", p->" + name + ", " + str(field["array_len"]))

            if sym == bsr["buf"]:
                file_decode.write(", p->" + str(field["array_len"]))

            file_decode.write(")")

            if name.endswith("descriptor") and field["size_bits"] < 16:
                file_decode.write(" << 8");

            file_decode.write(";\n")
            if field["fixed"] != None:
                file_decode.write(indent + "if ((p->" + name + ")")
                file_decode.write(" ^ " + "0x" + format(field["fixed"], "04X") + ")\n")
                file_decode.write(indent + indent + "return AVT_ERROR(EINVAL);\n")

        for name, field in fields.items():
            indent = "    "
            read_sym = bsr["int"]
            if field["datatype"] == data_prefix + "Rational":
                read_sym = bsr["rat"]
            elif (field["ldpc"] != None) or (name.startswith("padding") and field["bytestream"] > 0):
                read_sym = bsr["skip"]
            elif (type(field["array_len"]) == str and field["bytestream"] == 1) or field["ldpc"] != None:
                read_sym = bsr["buf"]
            elif field["string"]:
                read_sym = bsr["fstr"]

            if newline_carryover:
                file_decode.write("\n")
                newline_carryover = False;
            if field["ldpc"] != None:
                newline_carryover = True

            if field["bytestream"] == 0 and bitfield == False: # Setup bitfield writing
                file_decode.write("\n")
                if had_bitfield == False:
                    file_decode.write(indent + "uint32_t ");
                file_decode.write("bitfield = " + bsr["int"] + "u8b(bs);\n")
                bitfield = True
                had_bitfield = True
                bitfield_bit = (MAX_BITFIELD_LEN - 1)

            if bitfield:
                bitfield_bit -= field["size_bits"]
                file_decode.write(indent + "p->" + name + " = (bitfield >> " + str(bitfield_bit + 1) + ") & ((1 << " + str(field["size_bits"]) + ") - 1);\n")
            else:
                if ((type(field["array_len"]) == int and field["array_len"] > 1) or \
                    (type(field["array_len"]) == str and field["bytestream"] > 1)) and \
                   read_sym != bsr["fstr"]:
                    file_decode.write(indent + "for (int i = 0; i < ")
                    if type(field["array_len"]) == str:
                        file_decode.write("p->")
                    file_decode.write(str(field["array_len"]) + "; i++)\n")
                    indent = indent + "    "
                if field["payload"]:
                    file_decode.write(indent + "avt_buffer_quick_ref(pl, buf, avt_bs_offs(bs), " + "p->" + str(field["array_len"]) + ");\n")
                    file_decode.write(indent + bsr["skip"] + "(bs, p->" + str(field["array_len"]) + ");\n")
                else:
                    rsym(indent, read_sym, name, field)

            if bitfield and (MAX_BITFIELD_LEN - bitfield_bit - 1) >= 8 and \
               math.log2(MAX_BITFIELD_LEN - bitfield_bit - 1).is_integer(): # Terminate bitfield
                bitfield = False
                file_decode.write("\n")
        if substruct == False:
            file_decode.write("\n    return avt_bs_offs(bs);\n")
        file_decode.write("}\n")
    file_decode.write("\n#endif /* AVTRANSPORT_DECODE_H */\n")
    file_decode.close()
