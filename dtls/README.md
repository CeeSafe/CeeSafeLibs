# dtls

DTLS 1.2 for WebRTC (RFC 8827, 8842; DTLS-SRTP, RFC 5764) through OpenSSL 3, in CeeSafe and without I/O, over the
declarations it shares with `tls` and `pkey`.

- `DtlsCertificate.generate()`: a self-signed ECDSA P-256 certificate for one call, and `fingerprint()` for SDP's
  `a=fingerprint:sha-256` (upper-case hex, colon-separated).
- `DtlsEndpoint.create(cert, client)`: the client (`a=setup:active`) or the server. `feed` takes each datagram that
  came, `takeDatagram` gives those to send (whole records, at most `MTU` = 1200 bytes each), `tick` retransmits a late
  flight, `close` sends close_notify.
- Only `TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256` on P-256, and `SRTP_AES128_CM_HMAC_SHA1_80`, as the MAUI app's DTLS
  (MarivilDtls) takes. The server always makes the cookie exchange (HelloVerifyRequest), which that client waits for.
- Every certificate passes OpenSSL's check; the caller compares `peerFingerprint()` with the SDP's
  (`sameFingerprint`). Once connected, `srtpKeys()` gives the local and remote SRTP master keys and salts.

The tests run a client and a server in memory: the cookie exchange, the datagrams within the MTU, each side seeing the
other's fingerprint, mirrored SRTP keys, close_notify, and a lost flight sent again. OpenSSL comes from Homebrew on
macOS, the system on Linux, and `$OPENSSL_ANDROID_DIR` / `$OPENSSL_IOS_DIR` (CeeUI's packer sets them) on phones.
