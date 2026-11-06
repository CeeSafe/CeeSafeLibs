/* pkey_shim.c: public-key signatures through OpenSSL 3's EVP API, for the CeeSafe `pkey` package (DKIM): verifying,
 * and, further down, signing and keys.
 *
 * Verification returns 1 for a good signature, 0 for a bad one, and a negative code when the key is unusable:
 * -1 unreadable, -2 an RSA key under 1024 bits (RFC 8301 forbids them), -3 out of memory.
 */
#include <stddef.h>
#include <stdint.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

int32_t pks_verify_rsa_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                              const uint8_t *sig, size_t sig_len);
int32_t pks_verify_ed25519(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                           const uint8_t *sig, size_t sig_len);

/* A DKIM p= value: SubjectPublicKeyInfo DER (the standard), or a bare RSAPublicKey (which some signers publish). */
static EVP_PKEY *rsa_key(const uint8_t *key, size_t key_len) {
    const unsigned char *p = key;
    EVP_PKEY *pkey = d2i_PUBKEY(NULL, &p, (long)key_len);
    if (pkey == NULL) {
        p = key;
        pkey = d2i_PublicKey(EVP_PKEY_RSA, NULL, &p, (long)key_len);
    }
    return pkey;
}

int32_t pks_verify_rsa_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                              const uint8_t *sig, size_t sig_len) {
    EVP_PKEY *pkey = rsa_key(key, key_len);
    if (pkey == NULL) return -1;
    if (EVP_PKEY_get_base_id(pkey) != EVP_PKEY_RSA) {
        EVP_PKEY_free(pkey);
        return -1;
    }
    if (EVP_PKEY_get_bits(pkey) < 1024) {
        EVP_PKEY_free(pkey);
        return -2;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        EVP_PKEY_free(pkey);
        return -3;
    }
    int32_t result = 0;
    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) == 1 &&
        EVP_DigestVerify(ctx, sig, sig_len, data, data_len) == 1) {
        result = 1;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

/* Ed25519 (RFC 8032): `key` is the raw 32-byte public key, as DKIM's ed25519 keys are (RFC 8463). */
int32_t pks_verify_ed25519(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                           const uint8_t *sig, size_t sig_len) {
    if (key_len != 32) return -1;
    EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, key, key_len);
    if (pkey == NULL) return -1;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        EVP_PKEY_free(pkey);
        return -3;
    }
    int32_t result = 0;
    if (EVP_DigestVerifyInit(ctx, NULL, NULL, NULL, pkey) == 1 &&
        EVP_DigestVerify(ctx, sig, sig_len, data, data_len) == 1) {
        result = 1;
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

/* ---- Signing (DKIM signers): private keys are PKCS #8 DER; public keys come out in DKIM's p= form ---- */

#include <openssl/pem.h>

int32_t pks_key_kind(const uint8_t *key, size_t key_len);
int64_t pks_sign(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap);
int64_t pks_generate(int32_t kind, int32_t bits, uint8_t *out, size_t cap);
int64_t pks_public(const uint8_t *key, size_t key_len, uint8_t *out, size_t cap);
int64_t pks_from_pem(const uint8_t *pem, size_t pem_len, uint8_t *out, size_t cap);

/* Kinds: 1 RSA, 2 Ed25519. Errors: -1 unreadable or unsupported key, -3 out of memory, -4 output too small,
 * -5 failed. */
static EVP_PKEY *private_key(const uint8_t *key, size_t key_len) {
    const unsigned char *p = key;
    return d2i_AutoPrivateKey(NULL, &p, (long)key_len);
}

static int32_t kind_of(EVP_PKEY *pkey) {
    int id = EVP_PKEY_get_base_id(pkey);
    if (id == EVP_PKEY_RSA) return 1;
    if (id == EVP_PKEY_ED25519) return 2;
    return -1;
}

int32_t pks_key_kind(const uint8_t *key, size_t key_len) {
    EVP_PKEY *pkey = private_key(key, key_len);
    if (pkey == NULL) return -1;
    int32_t kind = kind_of(pkey);
    EVP_PKEY_free(pkey);
    return kind;
}

/* RSA: PKCS #1 v1.5 over SHA-256(data); Ed25519: over data itself (DKIM passes the hash, RFC 8463). */
int64_t pks_sign(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *out, size_t cap) {
    EVP_PKEY *pkey = private_key(key, key_len);
    if (pkey == NULL) return -1;
    int32_t kind = kind_of(pkey);
    if (kind < 0) {
        EVP_PKEY_free(pkey);
        return -1;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        EVP_PKEY_free(pkey);
        return -3;
    }
    int64_t result = -5;
    size_t length = 0;
    if (EVP_DigestSignInit(ctx, NULL, kind == 1 ? EVP_sha256() : NULL, NULL, pkey) == 1 &&
        EVP_DigestSign(ctx, NULL, &length, data, data_len) == 1) {
        if (length > cap) {
            result = -4;
        } else if (EVP_DigestSign(ctx, out, &length, data, data_len) == 1) {
            result = (int64_t)length;
        }
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

/* PKCS #8 rather than i2d_PrivateKey's PKCS #1 for RSA: one format for both kinds, and what other tools read. */
static int64_t write_private(EVP_PKEY *pkey, uint8_t *out, size_t cap) {
    PKCS8_PRIV_KEY_INFO *info = EVP_PKEY2PKCS8(pkey);
    if (info == NULL) return -3;
    int length = i2d_PKCS8_PRIV_KEY_INFO(info, NULL);
    if (length <= 0) {
        PKCS8_PRIV_KEY_INFO_free(info);
        return -5;
    }
    if ((size_t)length > cap) {
        PKCS8_PRIV_KEY_INFO_free(info);
        return -4;
    }
    unsigned char *p = out;
    i2d_PKCS8_PRIV_KEY_INFO(info, &p);
    PKCS8_PRIV_KEY_INFO_free(info);
    return length;
}

/* A new key: kind 1 RSA of `bits` (2048 or more), kind 2 Ed25519. Writes its PKCS #8 DER. */
int64_t pks_generate(int32_t kind, int32_t bits, uint8_t *out, size_t cap) {
    EVP_PKEY *pkey = NULL;
    if (kind == 1) {
        if (bits < 1024 || bits > 8192) return -1;
        pkey = EVP_RSA_gen((unsigned int)bits);
    } else if (kind == 2) {
        pkey = EVP_PKEY_Q_keygen(NULL, NULL, "ED25519");
    } else {
        return -1;
    }
    if (pkey == NULL) return -5;
    int64_t result = write_private(pkey, out, cap);
    EVP_PKEY_free(pkey);
    return result;
}

/* The public half as DKIM publishes it: SubjectPublicKeyInfo DER for RSA, the raw 32 bytes for Ed25519. */
int64_t pks_public(const uint8_t *key, size_t key_len, uint8_t *out, size_t cap) {
    EVP_PKEY *pkey = private_key(key, key_len);
    if (pkey == NULL) return -1;
    int32_t kind = kind_of(pkey);
    int64_t result = -1;
    if (kind == 1) {
        int length = i2d_PUBKEY(pkey, NULL);
        if (length <= 0) {
            result = -5;
        } else if ((size_t)length > cap) {
            result = -4;
        } else {
            unsigned char *p = out;
            i2d_PUBKEY(pkey, &p);
            result = length;
        }
    } else if (kind == 2) {
        size_t length = cap;
        result = EVP_PKEY_get_raw_public_key(pkey, out, &length) == 1 ? (int64_t)length : -4;
    }
    EVP_PKEY_free(pkey);
    return result;
}

/* A PEM private key (PKCS #8, or traditional RSA) to PKCS #8 DER, for importing existing keys. */
int64_t pks_from_pem(const uint8_t *pem, size_t pem_len, uint8_t *out, size_t cap) {
    BIO *bio = BIO_new_mem_buf(pem, (int)pem_len);
    if (bio == NULL) return -3;
    EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);
    if (pkey == NULL) return -1;
    int64_t result = kind_of(pkey) < 0 ? -1 : write_private(pkey, out, cap);
    EVP_PKEY_free(pkey);
    return result;
}
