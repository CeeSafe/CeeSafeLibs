# stun

STUN (RFC 8489) in CeeSafe, with the parts of TURN (RFC 8656) and ICE (RFC 8445) that ride on it: messages built and
read, XOR addresses (IPv4 and IPv6), MESSAGE-INTEGRITY (HMAC-SHA1 under a short-term password or TURN's long-term key,
MD5 of "user:realm:password"), FINGERPRINT, ERROR-CODE, and TURN's ChannelData. No I/O: the caller sends and receives
the bytes, so it serves an ICE agent, a TURN client and a STUN server alike.

```cee
var m = StunMessage.create(BINDING, StunClass.Request, transaction);
m.add(USERNAME, "remote:local".bytes());
m.addU32(PRIORITY, priority);
let bytes = m.encode(password.bytes(), true);          // MESSAGE-INTEGRITY and FINGERPRINT last
// ...
if (isStun(datagram) && checkIntegrity(datagram, password.bytes())) {
    let answer = StunMessage.decode(datagram)!;
    let mapped = answer.address(XOR_MAPPED_ADDRESS);
}
```

The tests check RFC 5769's sample request and IPv4 response (integrity, fingerprint, every field), messages built and
read back (an IPv6 XOR address, a changed byte failing the integrity), RFC 2202's HMAC-SHA1, the long-term key and
ChannelData. Written for Marivil's calls (its port of the MAUI app, which speaks WebRTC to it).
