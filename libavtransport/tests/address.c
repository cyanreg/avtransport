/*
 * Copyright © 2024, Lynne
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

#include <stdio.h>
#include <string.h>

#include "address.h"
#include "config.h"

#define FAIL(err)                \
    do {                         \
        ret = AVT_ERROR(EINVAL); \
        goto end;                \
    } while (0)

int main(void)
{
    int ret;
    AVTAddress addr = { };

    /* path */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "file://test.avt")) < 0)
            goto end;

        if (addr.type != AVT_ADDRESS_FILE || addr.proto != AVT_PROTOCOL_FILE)
            FAIL(EINVAL);

        if (strcmp(addr.path, "test.avt"))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* FD */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "fd://231")) < 0)
            goto end;

        if (addr.type != AVT_ADDRESS_FD || addr.proto != AVT_PROTOCOL_FILE)
            FAIL(EINVAL);

        if (addr.fd != 231)
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* Socket */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "socket:///tmp/avt.sock")) < 0)
            goto end;

        if (addr.type != AVT_ADDRESS_UNIX || addr.proto != AVT_PROTOCOL_STREAM)
            FAIL(EINVAL);

        if (strcmp(addr.path, "/tmp/avt.sock"))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* Socket plus options */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "socket:///tmp/avt.sock#rx_buf=65536")) < 0)
            goto end;

        if (addr.type != AVT_ADDRESS_UNIX || addr.proto != AVT_PROTOCOL_STREAM)
            FAIL(EINVAL);

        if (strcmp(addr.path, "/tmp/avt.sock"))
            FAIL(EINVAL);

        if (addr.opts.rx_buf != 65536)
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* IP */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "udp://192.168.1.1")) < 0)
            goto end;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_1[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x1 };
        if (memcmp(addr.ip, ip_test_1, 16))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* IP + port */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, true, "udp://192.168.1.2:9999")) < 0)
            goto end;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_2[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x2 };
        if (memcmp(addr.ip, ip_test_2, 16))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* quic + IPv6 + port */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "quic://[2001:db8::1]:9999")) < 0)
            goto end;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_QUIC)
            FAIL(EINVAL);

        static const uint8_t ip_test_3[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 };
        if (memcmp(addr.ip, ip_test_3, 16))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* quic + mode + IPv6 + port + device */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, true, "avt://quic:active@[2001:db8::4%lo]:9999")) < 0)
            goto end;

        if (addr.port != 9999 || addr.mode != AVT_MODE_ACTIVE ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_QUIC)
            FAIL(EINVAL);

        static const uint8_t ip_test_6[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4 };
        if (memcmp(addr.ip, ip_test_6, 16))
            FAIL(EINVAL);

        if (strcmp(addr.interface, "lo"))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* udp + mode + IPv4 + port + device */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "avt://udp:default@192.168.1.3%lo:9999")) < 0)
            goto end;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_7[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x3 };
        if (memcmp(addr.ip, ip_test_7, 16))
            FAIL(EINVAL);

        if (strcmp(addr.interface, "lo"))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* Stream ID */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, true, "udp://192.168.1.4/#default=0,65534")) < 0)
            goto end;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_8[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x4 };
        if (memcmp(addr.ip, ip_test_8, 16))
            FAIL(EINVAL);

        if (addr.opts.nb_default_sid != 2)
            FAIL(EINVAL);

        if (addr.opts.default_sid[0] != 0 || addr.opts.default_sid[1] != 65534)
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* Options */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "udp://192.168.1.5/#rx_buf=65536")) < 0)
            goto end;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_9[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x5 };
        if (memcmp(addr.ip, ip_test_9, 16))
            FAIL(EINVAL);

        if (addr.opts.rx_buf != 65536)
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* Stream ID + options */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, true, "quic://192.168.1.6/#default=0,65534&rx_buf=65536&tx_buf=32768")) < 0)
            goto end;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_QUIC)
            FAIL(EINVAL);

        static const uint8_t ip_test_10[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x6 };
        if (memcmp(addr.ip, ip_test_10, 16))
            FAIL(EINVAL);

        if (addr.opts.nb_default_sid != 2)
            FAIL(EINVAL);

        if (addr.opts.default_sid[0] != 0 || addr.opts.default_sid[1] != 65534)
            FAIL(EINVAL);

        if (addr.opts.rx_buf != 65536 || addr.opts.tx_buf != 32768)
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

    /* UUID */
    {
        if ((ret = avt_addr_from_url(NULL, &addr, false, "udp://192.168.1.6/123e4567-e89b-12d3-a456-426614174000")) < 0)
            goto end;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.type != AVT_ADDRESS_URL || addr.proto != AVT_PROTOCOL_UDP)
            FAIL(EINVAL);

        static const uint8_t ip_test_11[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x6 };
        if (memcmp(addr.ip, ip_test_11, 16))
            FAIL(EINVAL);

        static const uint8_t uuid_test_11[16] = { 0x12, 0x3e, 0x45, 0x67, 0xe8, 0x9b, 0x12, 0xd3, 0xa4, 0x56, 0x42, 0x66, 0x14, 0x17, 0x40, 0x00 };
        if (memcmp(addr.uuid, uuid_test_11, 16))
            FAIL(EINVAL);

        avt_addr_free(&addr);
    }

end:
    avt_addr_free(&addr);
    return AVT_ERROR(ret);
}
