# webrtc

A WebRTC peer for one audio stream (BUNDLE, rtcp-mux), without I/O, over this repository's `stun`, `ice`, `dtls`,
`srtp`, `sdp` and `rtp`. `PeerSession` makes the offer or the answer (Opus, payload type 111), takes the other side's,
trickles candidates both ways, runs ICE, then DTLS (the answerer is the client, `setup:active`), checks the peer's
certificate against its SDP fingerprint, and protects RTP and RTCP with SRTP. The caller owns the UDP sockets: it
hands every datagram to `receive` (STUN, DTLS and SRTP told apart by their first byte, RFC 7983) and sends what `poll`
and `sendAudio` give; `takeRtp` gives the decrypted packets that came.

Tested in memory (two sessions, audio both ways) and against the MAUI app's own WebRTC (SIPSorcery, MarivilDtls,
Concentus) with Marivil's `tools/cee-peer` and `tools/maui-peer`: each side offering in turn, the MAUI end heard its
own tone back through the port's echo.
