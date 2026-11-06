/* tls_shim.c: see tls_shim.h. */
#include "tls_shim.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include "ceesafe/foreign.h"

/* ── Per-context extras: ALPN preferences and SNI children, kept in the SSL_CTX's ex_data ─────────────────────── */

typedef struct {
    char *name; /* lowercase; "*.example.com" for a wildcard */
    SSL_CTX *ctx;
} sni_entry;

typedef struct {
    uint8_t *alpn;
    size_t alpn_len;
    sni_entry *sni;
    size_t sni_len;
    size_t sni_cap;
} ctx_extra;

static int extra_index = -1;
static pthread_once_t extra_once = PTHREAD_ONCE_INIT;

static void extra_free(void *parent, void *ptr, CRYPTO_EX_DATA *ad, int idx, long argl, void *argp) {
    (void)parent;
    (void)ad;
    (void)idx;
    (void)argl;
    (void)argp;
    ctx_extra *x = ptr;
    if (x == NULL) {
        return;
    }
    for (size_t i = 0; i < x->sni_len; i++) {
        ceesafe_free(x->sni[i].name, CEE_ALLOC_FOREIGN);
        SSL_CTX_free(x->sni[i].ctx);
    }
    ceesafe_free(x->sni, CEE_ALLOC_FOREIGN);
    ceesafe_free(x->alpn, CEE_ALLOC_FOREIGN);
    ceesafe_free(x, CEE_ALLOC_FOREIGN);
}

static void extra_init(void) {
    extra_index = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, extra_free);
}

static ctx_extra *extra_of(SSL_CTX *ctx, int create) {
    pthread_once(&extra_once, extra_init);
    if (extra_index < 0) {
        return NULL;
    }
    ctx_extra *x = SSL_CTX_get_ex_data(ctx, extra_index);
    if (x == NULL && create) {
        x = ceesafe_alloc(sizeof *x, CEE_ALLOC_FOREIGN);
        if (x == NULL) {
            return NULL;
        }
        memset(x, 0, sizeof *x);
        if (!SSL_CTX_set_ex_data(ctx, extra_index, x)) {
            ceesafe_free(x, CEE_ALLOC_FOREIGN);
            return NULL;
        }
    }
    return x;
}

/* ── Contexts ──────────────────────────────────────────────────────────────────────────────────────────────── */

void *cstls_ctx_new(int32_t server) {
    if (!OPENSSL_init_ssl(0, NULL)) {
        return NULL;
    }
    SSL_CTX *ctx = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
    if (ctx == NULL) {
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    /* Idle connections give their record buffers back: a mail server holds many idle IMAP sessions. */
    SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
    if (server) {
        SSL_CTX_set_options(ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
        static const unsigned char sid[] = "cstls";
        SSL_CTX_set_session_id_context(ctx, sid, sizeof sid - 1);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    }
    return ctx;
}

void cstls_ctx_up_ref(void *ctx) {
    SSL_CTX_up_ref((SSL_CTX *)ctx);
}

void cstls_ctx_free(void *ctx) {
    SSL_CTX_free((SSL_CTX *)ctx);
}

/* Clears the "no more PEM blocks" error a read loop leaves at the end of its input. */
static void clear_pem_eof(void) {
    unsigned long e = ERR_peek_last_error();
    if (ERR_GET_LIB(e) == ERR_LIB_PEM && ERR_GET_REASON(e) == PEM_R_NO_START_LINE) {
        ERR_clear_error();
    }
}

int32_t cstls_ctx_use_pem(void *p, const uint8_t *chain, size_t chain_len, const uint8_t *key, size_t key_len) {
    SSL_CTX *ctx = p;
    ERR_clear_error();
    if (chain_len > INT32_MAX || key_len > INT32_MAX) {
        return CSTLS_ERR_CONFIG;
    }
    BIO *cb = BIO_new_mem_buf(chain, (int)chain_len);
    if (cb == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    X509 *leaf = PEM_read_bio_X509_AUX(cb, NULL, NULL, NULL);
    if (leaf == NULL || SSL_CTX_use_certificate(ctx, leaf) != 1) {
        X509_free(leaf);
        BIO_free(cb);
        return CSTLS_ERR_CERT;
    }
    X509_free(leaf);
    SSL_CTX_clear_chain_certs(ctx);
    X509 *extra;
    while ((extra = PEM_read_bio_X509(cb, NULL, NULL, NULL)) != NULL) {
        if (SSL_CTX_add0_chain_cert(ctx, extra) != 1) { /* takes ownership on success */
            X509_free(extra);
            BIO_free(cb);
            return CSTLS_ERR_CERT;
        }
    }
    clear_pem_eof();
    BIO_free(cb);

    BIO *kb = BIO_new_mem_buf(key, (int)key_len);
    if (kb == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(kb, NULL, NULL, NULL);
    BIO_free(kb);
    if (pkey == NULL) {
        return CSTLS_ERR_KEY;
    }
    /* Checked first: SSL_CTX_use_PrivateKey refuses a mismatched key with the same error as an unusable one. */
    X509 *cert = SSL_CTX_get0_certificate(ctx);
    if (cert == NULL || X509_check_private_key(cert, pkey) != 1) {
        EVP_PKEY_free(pkey);
        ERR_clear_error();
        return CSTLS_ERR_KEY_MISMATCH;
    }
    int used = SSL_CTX_use_PrivateKey(ctx, pkey);
    EVP_PKEY_free(pkey);
    return used == 1 ? CSTLS_OK : CSTLS_ERR_KEY;
}

int32_t cstls_ctx_trust_system(void *ctx) {
    ERR_clear_error();
    return SSL_CTX_set_default_verify_paths((SSL_CTX *)ctx) == 1 ? CSTLS_OK : CSTLS_ERR_TRUST;
}

int32_t cstls_ctx_trust_pem(void *p, const uint8_t *pem, size_t len) {
    SSL_CTX *ctx = p;
    ERR_clear_error();
    if (len > INT32_MAX) {
        return CSTLS_ERR_CONFIG;
    }
    BIO *b = BIO_new_mem_buf(pem, (int)len);
    if (b == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    X509_STORE *store = SSL_CTX_get_cert_store(ctx);
    int added = 0;
    X509 *x;
    while ((x = PEM_read_bio_X509(b, NULL, NULL, NULL)) != NULL) {
        int ok = X509_STORE_add_cert(store, x);
        X509_free(x);
        if (ok != 1) {
            BIO_free(b);
            return CSTLS_ERR_TRUST;
        }
        added++;
    }
    clear_pem_eof();
    BIO_free(b);
    return added > 0 ? CSTLS_OK : CSTLS_ERR_TRUST;
}

void cstls_ctx_verify_peer(void *p, int32_t on) {
    SSL_CTX *ctx = p;
    int server = SSL_CTX_get_ssl_method(ctx) == TLS_server_method();
    int mode = SSL_VERIFY_NONE;
    if (on) {
        mode = server ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT) : SSL_VERIFY_PEER;
    }
    SSL_CTX_set_verify(ctx, mode, NULL);
}

int32_t cstls_ctx_min_version(void *ctx, int32_t version) {
    int v = version == 13 ? TLS1_3_VERSION : TLS1_2_VERSION;
    return SSL_CTX_set_min_proto_version((SSL_CTX *)ctx, v) == 1 ? CSTLS_OK : CSTLS_ERR_CONFIG;
}

static int alpn_select(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                       unsigned int inlen, void *arg) {
    (void)ssl;
    ctx_extra *x = arg;
    unsigned char *selected = NULL;
    if (SSL_select_next_proto(&selected, outlen, x->alpn, (unsigned int)x->alpn_len, in, inlen) ==
        OPENSSL_NPN_NEGOTIATED) {
        *out = selected;
        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_ALERT_FATAL; /* RFC 7301 §3.2: no_application_protocol */
}

int32_t cstls_ctx_set_alpn(void *p, const uint8_t *wire, size_t len) {
    SSL_CTX *ctx = p;
    if (len == 0 || len > 0xFFFF) {
        return CSTLS_ERR_CONFIG;
    }
    if (SSL_CTX_get_ssl_method(ctx) != TLS_server_method()) {
        return SSL_CTX_set_alpn_protos(ctx, wire, (unsigned int)len) == 0 ? CSTLS_OK : CSTLS_ERR_NOMEM;
    }
    ctx_extra *x = extra_of(ctx, 1);
    if (x == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    uint8_t *copy = ceesafe_alloc(len, CEE_ALLOC_FOREIGN);
    if (copy == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    memcpy(copy, wire, len);
    ceesafe_free(x->alpn, CEE_ALLOC_FOREIGN);
    x->alpn = copy;
    x->alpn_len = len;
    SSL_CTX_set_alpn_select_cb(ctx, alpn_select, x);
    return CSTLS_OK;
}

static int name_matches(const char *pattern, const char *name) {
    if (pattern[0] == '*' && pattern[1] == '.') {
        const char *dot = strchr(name, '.'); /* a wildcard covers exactly one leftmost label */
        return dot != NULL && dot != name && strcasecmp(dot + 1, pattern + 2) == 0;
    }
    return strcasecmp(pattern, name) == 0;
}

static int sni_select(SSL *ssl, int *alert, void *arg) {
    (void)alert;
    ctx_extra *x = arg;
    const char *name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (name == NULL) {
        return SSL_TLSEXT_ERR_NOACK;
    }
    for (size_t i = 0; i < x->sni_len; i++) { /* exact names first, then wildcards */
        if (x->sni[i].name[0] != '*' && name_matches(x->sni[i].name, name)) {
            SSL_set_SSL_CTX(ssl, x->sni[i].ctx);
            return SSL_TLSEXT_ERR_OK;
        }
    }
    for (size_t i = 0; i < x->sni_len; i++) {
        if (x->sni[i].name[0] == '*' && name_matches(x->sni[i].name, name)) {
            SSL_set_SSL_CTX(ssl, x->sni[i].ctx);
            return SSL_TLSEXT_ERR_OK;
        }
    }
    return SSL_TLSEXT_ERR_OK; /* unknown name: the default certificate */
}

int32_t cstls_ctx_add_sni(void *p, const uint8_t *name, size_t len, void *child) {
    SSL_CTX *ctx = p;
    if (len == 0 || len > 253) {
        return CSTLS_ERR_CONFIG;
    }
    ctx_extra *x = extra_of(ctx, 1);
    if (x == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    if (x->sni_len == x->sni_cap) {
        size_t cap = x->sni_cap == 0 ? 4 : x->sni_cap * 2;
        sni_entry *grown = ceesafe_realloc(x->sni, cap * sizeof *grown, CEE_ALLOC_FOREIGN);
        if (grown == NULL) {
            return CSTLS_ERR_NOMEM;
        }
        x->sni = grown;
        x->sni_cap = cap;
    }
    char *copy = ceesafe_alloc(len + 1, CEE_ALLOC_FOREIGN);
    if (copy == NULL) {
        return CSTLS_ERR_NOMEM;
    }
    for (size_t i = 0; i < len; i++) {
        copy[i] = (char)tolower(name[i]);
    }
    copy[len] = 0;
    SSL_CTX_up_ref((SSL_CTX *)child);
    x->sni[x->sni_len].name = copy;
    x->sni[x->sni_len].ctx = child;
    x->sni_len++;
    SSL_CTX_set_tlsext_servername_callback(ctx, sni_select);
    SSL_CTX_set_tlsext_servername_arg(ctx, x);
    return CSTLS_OK;
}

/* ── Holders ───────────────────────────────────────────────────────────────────────────────────────────────── */

typedef struct {
    pthread_mutex_t lock;
    SSL_CTX *ctx;
} holder;

void *cstls_holder_new(void *ctx) {
    holder *h = ceesafe_alloc(sizeof *h, CEE_ALLOC_FOREIGN);
    if (h == NULL) {
        return NULL;
    }
    if (pthread_mutex_init(&h->lock, NULL) != 0) {
        ceesafe_free(h, CEE_ALLOC_FOREIGN);
        return NULL;
    }
    SSL_CTX_up_ref(ctx);
    h->ctx = ctx;
    return h;
}

void *cstls_holder_get(void *p) {
    holder *h = p;
    pthread_mutex_lock(&h->lock);
    SSL_CTX *ctx = h->ctx;
    SSL_CTX_up_ref(ctx);
    pthread_mutex_unlock(&h->lock);
    return ctx;
}

void cstls_holder_set(void *p, void *ctx) {
    holder *h = p;
    SSL_CTX_up_ref(ctx);
    pthread_mutex_lock(&h->lock);
    SSL_CTX *old = h->ctx;
    h->ctx = ctx;
    pthread_mutex_unlock(&h->lock);
    SSL_CTX_free(old); /* connections made from it hold their own references */
}

void cstls_holder_free(void *p) {
    holder *h = p;
    SSL_CTX_free(h->ctx);
    pthread_mutex_destroy(&h->lock);
    ceesafe_free(h, CEE_ALLOC_FOREIGN);
}

/* ── Connections ───────────────────────────────────────────────────────────────────────────────────────────── */

typedef struct {
    SSL *ssl;
    BIO *in;
    BIO *out;
} conn;

void *cstls_conn_new(void *ctx, int32_t server, const uint8_t *host, size_t host_len, int32_t verify_host) {
    ERR_clear_error();
    char name[256];
    if (host_len >= sizeof name) {
        return NULL;
    }
    conn *c = ceesafe_alloc(sizeof *c, CEE_ALLOC_FOREIGN);
    if (c == NULL) {
        return NULL;
    }
    c->ssl = SSL_new((SSL_CTX *)ctx);
    c->in = BIO_new(BIO_s_mem());
    c->out = BIO_new(BIO_s_mem());
    if (c->ssl == NULL || c->in == NULL || c->out == NULL) {
        SSL_free(c->ssl);
        BIO_free(c->in);
        BIO_free(c->out);
        ceesafe_free(c, CEE_ALLOC_FOREIGN);
        return NULL;
    }
    BIO_set_mem_eof_return(c->in, -1); /* empty means "wait for more", not end of stream */
    BIO_set_mem_eof_return(c->out, -1);
    SSL_set_bio(c->ssl, c->in, c->out); /* the SSL owns both BIOs from here */
    if (server) {
        SSL_set_accept_state(c->ssl);
        return c;
    }
    SSL_set_connect_state(c->ssl);
    if (host_len > 0) {
        memcpy(name, host, host_len);
        name[host_len] = 0;
        unsigned char addr[16];
        int is_ip = inet_pton(AF_INET, name, addr) == 1 || inet_pton(AF_INET6, name, addr) == 1;
        int ok = 1;
        if (!is_ip) {
            ok = SSL_set_tlsext_host_name(c->ssl, name) == 1; /* RFC 6066: no SNI for IP literals */
        }
        if (ok && verify_host) {
            ok = is_ip ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(c->ssl), name) == 1
                       : SSL_set1_host(c->ssl, name) == 1;
        }
        if (!ok) {
            SSL_free(c->ssl);
            ceesafe_free(c, CEE_ALLOC_FOREIGN);
            return NULL;
        }
    }
    return c;
}

void cstls_conn_free(void *p) {
    conn *c = p;
    SSL_free(c->ssl);
    ceesafe_free(c, CEE_ALLOC_FOREIGN);
}

int32_t cstls_conn_handshake(void *p) {
    conn *c = p;
    ERR_clear_error();
    int r = SSL_do_handshake(c->ssl);
    if (r == 1) {
        return 1;
    }
    int e = SSL_get_error(c->ssl, r);
    return (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) ? 0 : -1;
}

int64_t cstls_conn_feed(void *p, const uint8_t *bytes, size_t len) {
    conn *c = p;
    if (len == 0) {
        return 0;
    }
    if (len > INT32_MAX) {
        len = INT32_MAX;
    }
    int n = BIO_write(c->in, bytes, (int)len);
    return n < 0 ? -1 : n;
}

size_t cstls_conn_pending_out(void *p) {
    conn *c = p;
    return BIO_ctrl_pending(c->out);
}

int64_t cstls_conn_drain(void *p, uint8_t *out, size_t cap) {
    conn *c = p;
    if (cap > INT32_MAX) {
        cap = INT32_MAX;
    }
    int n = BIO_read(c->out, out, (int)cap);
    return n < 0 ? 0 : n;
}

int64_t cstls_conn_read(void *p, uint8_t *out, size_t cap) {
    conn *c = p;
    ERR_clear_error();
    size_t n = 0;
    if (SSL_read_ex(c->ssl, out, cap, &n) == 1) {
        return (int64_t)n;
    }
    switch (SSL_get_error(c->ssl, 0)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        return -1;
    case SSL_ERROR_ZERO_RETURN:
        return 0;
    default:
        return -2;
    }
}

int64_t cstls_conn_write(void *p, const uint8_t *bytes, size_t len) {
    conn *c = p;
    ERR_clear_error();
    size_t n = 0;
    if (SSL_write_ex(c->ssl, bytes, len, &n) == 1) {
        return (int64_t)n;
    }
    int e = SSL_get_error(c->ssl, 0);
    return (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) ? -1 : -2;
}

int32_t cstls_conn_shutdown(void *p) {
    conn *c = p;
    ERR_clear_error();
    int r = SSL_shutdown(c->ssl);
    return r < 0 ? -1 : r;
}

int32_t cstls_conn_version(void *p) {
    conn *c = p;
    return SSL_version(c->ssl);
}

size_t cstls_conn_alpn(void *p, uint8_t *out, size_t cap) {
    conn *c = p;
    const unsigned char *data = NULL;
    unsigned int len = 0;
    SSL_get0_alpn_selected(c->ssl, &data, &len);
    size_t n = len < cap ? len : cap;
    if (n > 0) {
        memcpy(out, data, n);
    }
    return len;
}

size_t cstls_conn_server_name(void *p, uint8_t *out, size_t cap) {
    conn *c = p;
    const char *name = SSL_get_servername(c->ssl, TLSEXT_NAMETYPE_host_name);
    if (name == NULL) {
        return 0;
    }
    size_t len = strlen(name);
    memcpy(out, name, len < cap ? len : cap);
    return len;
}

int64_t cstls_conn_verify_result(void *p) {
    conn *c = p;
    return SSL_get_verify_result(c->ssl);
}

int32_t cstls_conn_has_peer_certificate(void *p) {
    conn *c = p;
    return SSL_get0_peer_certificate(c->ssl) != NULL;
}

/* ── DANE (RFC 6698; RFC 7672 for SMTP) ────────────────────────────────────────────────────────────────────── */

/* Lets the context's connections use DANE. Once, when the context is made: it changes the context, which other
   threads' connections may be using afterwards. */
int32_t cstls_ctx_dane_enable(void *ctx) {
    ERR_clear_error();
    return SSL_CTX_dane_enable((SSL_CTX *)ctx) > 0 ? CSTLS_OK : CSTLS_ERR_CONFIG;
}

/* Makes a client connection, before its handshake, authenticate the server by the TLSA records added next, with
   `base` (the TLSA base domain, an MX host's name) as the reference identifier for DANE-TA. DANE-EE matches skip
   name checks (RFC 7672 §3.1.1), and the peer must verify: a chain no record matches fails the handshake with
   X509_V_ERR_DANE_NO_MATCH. The context must have had cstls_ctx_dane_enable. */
int32_t cstls_conn_dane_enable(void *p, const uint8_t *base, size_t len) {
    conn *c = p;
    char name[254];
    if (len == 0 || len > 253) {
        return CSTLS_ERR_CONFIG;
    }
    memcpy(name, base, len);
    name[len] = 0;
    ERR_clear_error();
    if (SSL_dane_enable(c->ssl, name) <= 0) {
        return CSTLS_ERR_CONFIG;
    }
    SSL_dane_set_flags(c->ssl, DANE_FLAG_NO_DANE_EE_NAMECHECKS);
    SSL_set_verify(c->ssl, SSL_VERIFY_PEER, NULL);
    return CSTLS_OK;
}

/* Adds one TLSA record: 1 when OpenSSL can use it, 0 when it is unusable (an unknown usage, selector or matching
   type, or a digest of the wrong length), -1 on failure. */
int32_t cstls_conn_dane_add(void *p, int32_t usage, int32_t selector, int32_t matching, const uint8_t *data,
                            size_t len) {
    conn *c = p;
    if (usage < 0 || usage > 255 || selector < 0 || selector > 255 || matching < 0 || matching > 255) {
        return 0;
    }
    ERR_clear_error();
    int r = SSL_dane_tlsa_add(c->ssl, (uint8_t)usage, (uint8_t)selector, (uint8_t)matching, data, len);
    if (r < 0) {
        ERR_clear_error();
    }
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

/* After a handshake: the depth of the certificate a TLSA record matched (0 for the server's own), or -1 when DANE
   did not authenticate the peer. */
int32_t cstls_conn_dane_depth(void *p) {
    conn *c = p;
    return SSL_get0_dane_authority(c->ssl, NULL, NULL);
}

/* ── Errors ────────────────────────────────────────────────────────────────────────────────────────────────── */

uint64_t cstls_take_error(void) {
    unsigned long e;
    unsigned long last = 0;
    while ((e = ERR_get_error()) != 0) {
        last = e;
    }
    return last;
}

size_t cstls_error_text(uint64_t code, uint8_t *out, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    ERR_error_string_n((unsigned long)code, (char *)out, cap);
    return strlen((char *)out);
}

size_t cstls_verify_text(int64_t code, uint8_t *out, size_t cap) {
    const char *text = X509_verify_cert_error_string((long)code);
    size_t len = strlen(text);
    size_t n = len < cap ? len : cap;
    memcpy(out, text, n);
    return n;
}
