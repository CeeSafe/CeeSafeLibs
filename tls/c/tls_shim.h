/* tls_shim.h: a small, socket-free C API over OpenSSL 3 for the CeeSafe `tls` package.
 *
 * Connections use two memory BIOs, so OpenSSL never touches a socket: CeeSafe feeds received bytes in with
 * cstls_conn_feed, drains bytes to send with cstls_conn_drain, and does all I/O itself on its async runtime.
 *
 * Handles are opaque pointers. Every function is safe to call from any thread for distinct connections; a context
 * (SSL_CTX) may be shared by connections on many threads once it is configured.
 */
#ifndef CSTLS_SHIM_H
#define CSTLS_SHIM_H

#include <stddef.h>
#include <stdint.h>

/* Result codes of the configuration functions. */
#define CSTLS_OK 0
#define CSTLS_ERR_CERT (-1)
#define CSTLS_ERR_KEY (-2)
#define CSTLS_ERR_KEY_MISMATCH (-3)
#define CSTLS_ERR_NOMEM (-4)
#define CSTLS_ERR_TRUST (-5)
#define CSTLS_ERR_CONFIG (-6)

/* Contexts. */
void *cstls_ctx_new(int32_t server);
void cstls_ctx_up_ref(void *ctx);
void cstls_ctx_free(void *ctx);
int32_t cstls_ctx_use_pem(void *ctx, const uint8_t *chain, size_t chain_len, const uint8_t *key, size_t key_len);
int32_t cstls_ctx_trust_system(void *ctx);
int32_t cstls_ctx_trust_pem(void *ctx, const uint8_t *pem, size_t len);
void cstls_ctx_verify_peer(void *ctx, int32_t on);
int32_t cstls_ctx_min_version(void *ctx, int32_t version);
int32_t cstls_ctx_set_alpn(void *ctx, const uint8_t *wire, size_t len);
int32_t cstls_ctx_add_sni(void *ctx, const uint8_t *name, size_t len, void *child);

/* A replaceable context, for certificate hot reload. */
void *cstls_holder_new(void *ctx);
void *cstls_holder_get(void *holder);
void cstls_holder_set(void *holder, void *ctx);
void cstls_holder_free(void *holder);

/* Connections. */
void *cstls_conn_new(void *ctx, int32_t server, const uint8_t *host, size_t host_len, int32_t verify_host);
void cstls_conn_free(void *conn);
int32_t cstls_conn_handshake(void *conn);
int64_t cstls_conn_feed(void *conn, const uint8_t *bytes, size_t len);
size_t cstls_conn_pending_out(void *conn);
int64_t cstls_conn_drain(void *conn, uint8_t *out, size_t cap);
int64_t cstls_conn_read(void *conn, uint8_t *out, size_t cap);
int64_t cstls_conn_write(void *conn, const uint8_t *bytes, size_t len);
int32_t cstls_conn_shutdown(void *conn);
int32_t cstls_conn_version(void *conn);
size_t cstls_conn_alpn(void *conn, uint8_t *out, size_t cap);
size_t cstls_conn_server_name(void *conn, uint8_t *out, size_t cap);
int64_t cstls_conn_verify_result(void *conn);
int32_t cstls_conn_has_peer_certificate(void *conn);

/* DANE. */
int32_t cstls_ctx_dane_enable(void *ctx);
int32_t cstls_conn_dane_enable(void *conn, const uint8_t *base, size_t len);
int32_t cstls_conn_dane_add(void *conn, int32_t usage, int32_t selector, int32_t matching, const uint8_t *data,
                            size_t len);
int32_t cstls_conn_dane_depth(void *conn);

/* Errors. */
uint64_t cstls_take_error(void);
size_t cstls_error_text(uint64_t code, uint8_t *out, size_t cap);
size_t cstls_verify_text(int64_t code, uint8_t *out, size_t cap);

#endif
