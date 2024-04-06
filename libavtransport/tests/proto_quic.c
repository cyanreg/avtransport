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

#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>

#include <avtransport/avtransport.h>

#include "address.h"
#include "protocol_common.h"
#include "io_common.h"

extern const AVTIO avt_io_dcb;
extern const AVTProtocol avt_protocol_quic;

static void add_x509_ext(X509 *cert, X509V3_CTX *ctx, int ext_nid, const char *value)
{
    X509_EXTENSION *ex = X509V3_EXT_conf_nid(NULL, ctx, ext_nid, value);
    X509_add_ext(cert, ex, -1);
    X509_EXTENSION_free(ex);
}

static int create_cert(AVTContext *avt, const char *certfile, const char *keyfile)
{
    EVP_PKEY *p_key = NULL;
    EVP_PKEY_CTX *p_key_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    EVP_PKEY_keygen_init(p_key_ctx);

    if (EVP_PKEY_keygen(p_key_ctx, &p_key) <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error generating key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Write the private key file */
    FILE *f_key = fopen(keyfile, "wb");
    int ret = PEM_write_PrivateKey(f_key, p_key, NULL, NULL, 0, NULL, NULL);
    fclose(f_key);
    if (ret <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error writing private key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Create the certificate object */
    X509 *cert = X509_new();
    X509_set_version(cert, 3);

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60L); /* 60 seconds */
    X509_set_pubkey(cert, p_key);

    X509_NAME *cert_name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(cert_name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name,  "C", MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name,  "O", MBSTRING_ASC, (unsigned char *)"HANMA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "OU", MBSTRING_ASC, (unsigned char *)"KANMA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "ST", MBSTRING_ASC, (unsigned char *)"GOOOS", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "SN", MBSTRING_ASC, (unsigned char *)"DESANE", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "GN", MBSTRING_ASC, (unsigned char *)"HAAH", -1, -1, 0);
    X509_set_issuer_name(cert, cert_name);

    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, cert, cert, NULL, NULL, 0);

    add_x509_ext(cert, &v3ctx, NID_authority_key_identifier, "keyid");
    add_x509_ext(cert, &v3ctx, NID_basic_constraints, "critical,CA:TRUE");

    if (X509_sign(cert, p_key, NULL /* No digest for Ed25519 keys */) <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error signing X509 certificate with key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Write the certificate file */
    FILE *f_cert = fopen(certfile, "wb");
    PEM_write_X509(f_cert, cert);
    fclose(f_cert);

    EVP_PKEY_free(p_key);
    EVP_PKEY_CTX_free(p_key_ctx);
    X509_free(cert);

    return 0;
}

int main(void)
{
    int ret;
    AVTContext *avt;
    AVTAddress addr_server;
    AVTAddress addr_client;

    const AVTIO *io = &avt_io_dcb;
    AVTIOCtx *io_server_ctx = NULL;
    AVTIOCtx *io_client_ctx = NULL;

    const AVTProtocol *p = &avt_protocol_quic;
    AVTProtocolCtx *p_server_ctx = NULL;
    AVTProtocolCtx *p_client_ctx = NULL;

    ret = avt_init(&avt, NULL);
    if (ret < 0)
        return AVT_ERROR(ret);

    ret = create_cert(avt, "test_cert.pem", "test_key.pem");
    if (ret < 0)
        goto end;

    { /* Server */
        ret = avt_addr_from_url(avt, &addr_server, 1, "quic://localhost/#certfile=test_cert.pem&keyfile=test_key.pem");
        if (ret < 0)
            goto end;
        ret = io->init(avt, &io_server_ctx, &addr_server);
        if (ret < 0)
            goto end;
        ret = p->init(avt, &p_server_ctx, &addr_server, io, io_server_ctx);
        if (ret < 0)
            goto end;
    }

    { /* Client */
        ret = avt_addr_from_url(avt, &addr_client, 0, "quic://localhost/#certfile=test_cert.pem");
        if (ret < 0)
            goto end;
        ret = io->init(avt, &io_client_ctx, &addr_client);
        if (ret < 0)
            goto end;
        ret = p->init(avt, &p_client_ctx, &addr_client, io, io_client_ctx);
        if (ret < 0)
            goto end;
    }




end:
    if (p_client_ctx)
        p->close(&p_client_ctx);
    if (io_client_ctx)
        io->close(&io_client_ctx);

    if (p_server_ctx)
        p->close(&p_server_ctx);
    if (io_server_ctx)
        io->close(&io_server_ctx);

    avt_addr_free(&addr_client);
    avt_addr_free(&addr_server);
    avt_close(&avt);

    return 0;
}
