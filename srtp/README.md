# srtp

SRTP and SRTCP (RFC 3711) with `AES_CM_128_HMAC_SHA1_80`, the profile WebRTC's DTLS-SRTP agrees (RFC 5764). The
session keys come from the master key and salt (the `dtls` package exports them); AES-128 in counter mode is
OpenSSL's, which uses the processor's AES instructions; HMAC-SHA1, the rollover counter and the replay windows are
CeeSafe.

```cee
var send = SrtpContext.create(keys.localKey[..], keys.localSalt[..])!;
var receive = SrtpContext.create(keys.remoteKey[..], keys.remoteSalt[..])!;
send.protect(mut rtpPacket);          // encrypts the payload, appends the 10-byte tag
receive.unprotect(mut srtpPacket);    // false: a bad tag or a replay, and the packet is left as it came
send.protectRtcp(mut rtcpPacket);     // and unprotectRtcp
```

The tests check RFC 3711 B.3's key derivation, libsrtp's reference SRTP and SRTCP packets byte for byte (recomputed
with OpenSSL's command line and Python's hmac), the rollover counter across a sequence-number wrap, late packets
within the 64-packet window, replays and tampering.
