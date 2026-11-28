# sdp

SDP (RFC 8866) as WebRTC uses it: a session's BUNDLE group and its media sections, each with its codecs (`rtpmap`,
`fmtp`, `rtcp-fb`), ICE credentials and candidates, DTLS fingerprint and `setup` role, direction, header extensions,
SSRCs and CNAME. `SessionDescription.parse` reads leniently (CRLF or LF, unknown lines kept in `others`, session-level
ICE and DTLS attributes given to every section); `format` writes as JSEP does, CRLF after every line.

The tests read the MAUI app's own offer (SIPSorcery's, captured with Marivil's `tools/maui-peer`), a Chrome-style
description with session-level attributes, a rejected section and video's feedback lines, and a description written
and read back.
