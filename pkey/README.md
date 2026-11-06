# pkey

Public-key signature verification through OpenSSL 3's EVP API, for DKIM.

- `Signatures.verifyRsaSha256(key, data, signature)` checks RSA PKCS #1 v1.5 over SHA-256. The key is DER, as
  SubjectPublicKeyInfo or a bare RSAPublicKey (some DKIM keys are the latter). Keys under 1024 bits give `WeakKey`
  (RFC 8301).
- `Signatures.verifyEd25519(key, data, signature)` checks Ed25519 with a raw 32-byte key.
- Both give a `Verdict`: `Good`, `Bad`, `BadKey`, `WeakKey` or `OutOfMemory`.

The tests check signatures made by the `openssl` command line (`tests/fixtures`). Signing, for outbound DKIM, comes
with the relay.

## Signing and keys

`PrivateKey` holds an RSA or Ed25519 private key as PKCS #8 DER:
- `generate(KeyKind.Rsa, 2048)` or `generate(KeyKind.Ed25519, 0)` makes one;
- `fromPem` imports one (PKCS #8 or traditional RSA), and `fromDer` reads one back from storage;
- `publicKey` gives the public half in DKIM's `p=` form;
- `sign` gives RSA PKCS #1 v1.5 over SHA-256, or Ed25519.

RSA PKCS #1 v1.5 and Ed25519 signatures are deterministic, so the tests compare ours byte for byte with signatures
the openssl command line made from the same keys (`tests/fixtures/sign-*`).

