# tls

TLS for CeeSafe async code, over OpenSSL 3, all in CeeSafe: `src/ffi.cee` calls OpenSSL directly (no C shim, no
OpenSSL headers), and a safe CeeSafe API sits on top of it. OpenSSL works on two memory BIOs, and every byte moves
through CeeSafe's own `AsyncTcpStream`. The package therefore works on whichever reactor CeeSafe uses (kqueue, epoll or io_uring), and cancellation works as it does for any
other socket operation.

## Usage

```ceesafe
import tls.TlsConfig;
import tls.TlsStream;

// Client: system roots, host name checked.
let config = TlsConfig.client()?;
var session = await TlsStream.connect(tcp, config, "imap.example.org")?;
await session.write(request)?;
let n = await session.read(mut buffer[..])?;   // 0 = peer sent close_notify
await session.shutdown()?;

// Server: PEM chain (leaf first) and key.
var server = TlsConfig.server(chainPem, keyPem)?;
server.addServerName("mail.other.org", otherConfig)?;   // SNI; "*.example.org" wildcards work too
var session = await TlsStream.accept(tcp, server)?;
```

- **STARTTLS.** `connect` and `accept` take a stream that is already connected, so an upgrade is the same call made
  after the plain-text exchange. Throw away anything read from the plain stream after the STARTTLS command
  (RFC 3207 §6).
- **Hot reload.** Wrap the server configuration in `SharedTlsConfig`, share it with an `Arc`, call `current()` for
  each accept, and call `replace(newConfig)` when a certificate is renewed. Established connections keep the
  configuration they started with.
- **Threads.** `TlsConfig` and `SharedTlsConfig` are `Send` and `Share`, so one configuration can serve every worker of
  `Async.runWorkers`. A `TlsStream` belongs to one task at a time.
- **Also available.** ALPN (`alpn([...])` on either side), client certificates (`verifyPeer(true)` plus `trustPem` on
  a server, `useCertificate` on a client), `minVersion(TlsVersion.Tls13)`, and `version()`, `alpn()`, `serverName()`
  and `hasPeerCertificate()` on a session.
- **DANE** (RFC 6698; RFC 7672 for SMTP). `enableDane()` once on a client configuration, then
  `TlsStream.connectDaneWithin(tcp, config, mxName, records, ms)` authenticates the server by its TLSA records (`Tlsa`,
  `Tlsa.parse(rdata)`, `usableForSmtp()`) instead of by trusted roots. DANE-EE records skip name checks, and
  DANE-TA records check `mxName`. A chain that no record matches fails as `Verify(65)`, and `daneDepth()` says which
  certificate matched. OpenSSL's own DANE does the matching.
- **Timeouts.** A `read` cancelled or timed out while it waits (inside `Async.timeout`) consumed nothing, and the session
  reads on: a server may wait for input with a deadline and poll between reads (IMAP IDLE). Other socket errors end
  the session.
- **Errors.** Errors are `TlsError` values. `TlsErrors.describe` turns one into OpenSSL's words, for example
  "certificate has expired" or "hostname mismatch". A TCP close without close_notify is `TlsError.Truncated`.

## Defaults

- TLS 1.2 is the minimum version.
- Compression and renegotiation are off.
- Servers prefer their own cipher order.
- Idle connections release their record buffers (`SSL_MODE_RELEASE_BUFFERS`), so many idle IMAP sessions stay cheap.
- Clients verify the server's chain and its name, an IP address included. SNI is sent for host names only.

## Building

The package links `-lssl -lcrypto`. On macOS it uses Homebrew's `openssl@3` at `/opt/homebrew/opt/openssl@3`; on
Linux, install the distribution's OpenSSL 3 development package. Code that calls this package runs natively only:
`ceesafec run --interp` cannot call C.

## Tests

`ceesafec test` runs loopback tests with the certificates in `tests/fixtures`, which `tests/fixtures/generate.sh`
recreates. They cover:

- a 3 MiB round trip;
- an untrusted CA, a wrong host name and an IP SAN;
- SNI with exact and wildcard names;
- ALPN;
- STARTTLS;
- hot reload;
- client certificates;
- PEM errors;
- truncation;
- a read that times out, then reads on;
- DANE: DANE-EE by key and by certificate (digests from the openssl command line), name checks skipped for DANE-EE
  and kept for DANE-TA, a mismatch, and no usable record.

`ceesafec test --sweep cancel` passes too: each test is cancelled at every suspension point, without leaks.

`examples/fetch` connects to a real server with the system roots: `ceesafec build` there, then
`./out/fetch imap.gmail.com 993`.

## Licence

`src/ffi.cee` is written for this package against OpenSSL's public API; it is not derived from CeeSafe.Web's GPL
`web_tls`. OpenSSL 3 is under the Apache License 2.0; linking with it is permitted by the repository's OpenSSL
exception (COPYING.OPENSSL).
