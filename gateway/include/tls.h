#include <cstddef>
#include <cstdlib>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <stdlib.h>

void init_openssl(void)
{
    if (!OPENSSL_init_ssl(0, NULL)) {
        fprintf(stderr, "OPENSSL_init_ssl failed");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}


void opensll_fatal(const char* msg)
{
    fprintf(stderr, "%s\n", msg);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}


// tls server

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#define PORT     8443
#define CERTFILE "server.crt"
#define KEYFILE  "server.key"

SSL_CTX *create_server_ctx(void)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == NULL) {
        opensll_fatal("ssl ctx new error");
    }

    if (SSL_CTX_use_certificate_file(ctx, CERTFILE, SSL_FILETYPE_PEM) <= 0) {
        opensll_fatal("can't load cert file");
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, KEYFILE, SSL_FILETYPE_PEM) <= 0) {
        opensll_fatal("can't load private key");
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        opensll_fatal("cert not match key");
    }

    return ctx;
}


int main(void)
{
    init_openssl();
    SSL_CTX* ctx = create_server_ctx();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    int ret = bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        return -1;
    }

    listen(server_fd, 10);
    printf("TLS server listening on port %d\n", PORT);

    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            continue;
        }

        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) < 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            close(client_fd);
            continue;
        }

        printf("ssl handshake ok %s\n", SSL_get_cipher(ssl));

        char buf[2048];
        int n = SSL_read(ssl, buf, sizeof(buf) - 1);
        

    }


}