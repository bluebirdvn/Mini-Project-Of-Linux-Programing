#ifndef _TLS_H
#define _TLS_H

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "logger.h"

/**
 * @brief init ssl
 * 
 */
void init_openssl(void);

/**
 * @brief Create a server ctx object
 * 
 * @param cert_path path to certificate file
 * @param key_path path to key file
 * @return SSL_CTX* 
 */

SSL_CTX *create_server_ctx(const char* cert_path, const char* key_path);


/**
 * @brief destroy ssl_ctx instance
 * 
 * @param ctx pointer to ssl_ctx instance
 */
void tls_ctx_destroy(SSL_CTX* ctx);


#endif