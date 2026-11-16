# CeeSafeLibs

Libraries for [CeeSafe](https://ceesafe.github.io/CeeSafe/) programs, written for the
[CeeSafe mail server](https://github.com/CeeSafe/mailserver) and usable on their own. Each directory is a
CeeSafe package (`ceesafe.toml`), with its own README and tests (`ceesafec test` in the package's directory).

| Package | What it is |
|---|---|
| `tls` | TLS through OpenSSL 3 on memory BIOs, for async streams: servers (SNI, hot certificate reload), clients (verified, or DANE through TLSA records), and STARTTLS upgrades. |
| `pkey` | Public-key signatures through OpenSSL 3: signing and verifying with RSA SHA-256 (PKCS #1 v1.5) and Ed25519; key generation; PEM and DER keys. |
| `sha256` | SHA-256 (FIPS 180-4), HMAC-SHA-256 (RFC 2104) and PBKDF2-HMAC-SHA-256 (RFC 8018), in CeeSafe. |
| `bcrypt` | bcrypt as OpenBSD defines it: `$2b$` hashes, and verifying `$2a$` and `$2y$`. |
| `encoding` | Hex and base64 (RFC 4648), including the URL-safe alphabet. |
| `json` | JSON (RFC 8259) in the spirit of serde_json: a tree with exact integers and ordered objects, a strict parser without recursion, compact and pretty writers, a streaming writer, JSON Pointer, and typed encoding and decoding through `ToJson` and `FromJson`. |
| `diskio` | File calls `lib/std` lacks: positioned reads, fsync, rename, mkdir -p, directory sync and exclusive creation. |
| `ngtcp2` | QUIC: ngtcp2 1.26.0 with its OpenSSL 3.5 backend, vendored as released (`upstream/`), and the C shim CeeSafe.Web's `web_quic` binds to. C only; `tools/vendor_quic.py` makes it. |
| `nghttp3` | HTTP/3 and QPACK: nghttp3 1.19.0, vendored as released (`upstream/`), and the C shim CeeSafe.Web's `web_http3` binds to. C only; `tools/vendor_quic.py` makes it. |

Everything is CeeSafe: there is no C in the repository. `tls` and `pkey` call OpenSSL 3 directly (their
`src/ffi.cee`, without OpenSSL's headers) and link `libssl` and `libcrypto`. On macOS the manifests point at
Homebrew's `openssl@3`; on Linux, install `libssl3` (or `libssl-dev`).

`repros/` holds reduced reproductions of CeeSafe compiler and runtime issues found while writing these, and
`NOTES-ceesafe-0.14.md` records the issues met with that release.

## License

CeeSafeLibs is licensed under the GNU General Public License, version 2 only ([COPYING](COPYING)),
with an exception that permits linking with OpenSSL ([COPYING.OPENSSL](COPYING.OPENSSL)).

The vendored libraries keep their own licence: `ngtcp2/upstream/` and `nghttp3/upstream/` are MIT (their `COPYING`).
