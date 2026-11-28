# rtp

RTP and RTCP (RFC 3550) for WebRTC: `RtpHeader.read` (CSRCs, RFC 8285 one-byte header extensions, padding) and
`RtpBuilder` (sequence numbers that wrap, the marker, extension elements); `isRtcp` tells RTCP from RTP on a muxed
port (RFC 5761); `rtcpParts` walks a compound packet; sender reports, receiver reports, SDES CNAME and BYE are built
and read; `ReceptionStats` keeps what a report block needs (extended highest sequence, cumulative and fractional
loss, interarrival jitter, LSR and DLSR); `ntpOf` makes NTP timestamps.
