#include "tls.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "logger.h"


void init_openssl(void)
{
    if (!OPENSSL_init_ssl(0, NULL)) {
        LOG_ERROR("OPENSSL_init_ssl failed");
        exit(EXIT_FAILURE);
    }
}


SSL_CTX *create_server_ctx(const char* cert_path, const char* key_path)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == NULL) {
        LOG_ERROR("ssl ctx new");
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("can't load cert file");
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("can't load key file");
        exit(EXIT_FAILURE);
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        LOG_ERROR("key not match");
        exit(EXIT_FAILURE);
    }

    return ctx;
}


void tls_ctx_destroy(SSL_CTX *ctx)
{
    if (ctx) {
        SSL_CTX_free(ctx);
    }
    return;
}
