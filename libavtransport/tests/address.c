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

#include "../address.h"
#include "../../config.h"

int main(void)
{
    AVTAddress addr = { };

    /* IP */
    {
        if (avt_addr_from_url(NULL, &addr, "udp://192.168.1.1") < 0)
            return EINVAL;

        if (addr.port != CONFIG_DEFAULT_PORT || addr.mode != AVT_MODE_DEFAULT ||
            addr.proto != AVT_PROTOCOL_UDP)
            return EINVAL;

        static const uint8_t ip_test_1[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x1 };
        if (memcmp(addr.ip, ip_test_1, 16))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* IP + port */
    {
        if (avt_addr_from_url(NULL, &addr, "udp://192.168.1.2:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.proto != AVT_PROTOCOL_UDP)
            return EINVAL;

        static const uint8_t ip_test_2[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x2 };
        if (memcmp(addr.ip, ip_test_2, 16))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* quic + IPv6 + port */
    {
        if (avt_addr_from_url(NULL, &addr, "quic://[2001:db8::1]:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.proto != AVT_PROTOCOL_QUIC)
            return EINVAL;

        static const uint8_t ip_test_3[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x1 };
        if (memcmp(addr.ip, ip_test_3, 16))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* udplite + IPv6 + port */
    {
        if (avt_addr_from_url(NULL, &addr, "avt://udplite@[2001:db8::2]:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.proto != AVT_PROTOCOL_UDP_LITE)
            return EINVAL;

        static const uint8_t ip_test_4[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x2 };
        if (memcmp(addr.ip, ip_test_4, 16))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* udplite + mode + IPv6 + port */
    {
        if (avt_addr_from_url(NULL, &addr, "avt://udplite:passive@[2001:db8::3]:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_PASSIVE ||
            addr.proto != AVT_PROTOCOL_UDP_LITE)
            return EINVAL;

        static const uint8_t ip_test_5[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x3 };
        if (memcmp(addr.ip, ip_test_5, 16))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* udplite + mode + IPv6 + port + device */
    {
        if (avt_addr_from_url(NULL, &addr, "avt://udplite:active@[2001:db8::4%eth0]:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_ACTIVE ||
            addr.proto != AVT_PROTOCOL_UDP_LITE)
            return EINVAL;

        static const uint8_t ip_test_6[16] = { 0x20, 0x1, 0xd, 0xb8, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x4 };
        if (memcmp(addr.ip, ip_test_6, 16))
            return EINVAL;

        if (strcmp(addr.interface, "eth0"))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* udplite + mode + IPv4 + port + device */
    {
        if (avt_addr_from_url(NULL, &addr, "avt://udplite:default@192.168.1.3%eth0:9999") < 0)
            return EINVAL;

        if (addr.port != 9999 || addr.mode != AVT_MODE_DEFAULT ||
            addr.proto != AVT_PROTOCOL_UDP_LITE)
            return EINVAL;

        static const uint8_t ip_test_7[16] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff, 0xc0, 0xa8, 0x1, 0x3 };
        if (memcmp(addr.ip, ip_test_7, 16))
            return EINVAL;

        if (strcmp(addr.interface, "eth0"))
            return EINVAL;

        avt_addr_free(&addr);
    }

    /* path */
    {
        if (avt_addr_from_url(NULL, &addr, "file://test.avt") < 0)
            return EINVAL;

        if (addr.proto != AVT_PROTOCOL_FILE)
            return EINVAL;

        if (strcmp(addr.path, "test.avt"))
            return EINVAL;

        avt_addr_free(&addr);
    }

    return 0;
}
