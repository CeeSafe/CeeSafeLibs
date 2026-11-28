# ice

An ICE agent (RFC 8445) in CeeSafe for one component, as WebRTC uses it with BUNDLE and rtcp-mux, over the `stun`
package. No I/O: the caller owns the UDP sockets, hands the agent every datagram (`receive`, which says whether it was
STUN or the application's), and sends what `poll` gives, every 20 ms.

- Host candidates (`addHost`) and server-reflexive ones (`gather` asks a STUN server), trickled (`takeCandidate`);
  remote candidates from the SDP or trickled (`addRemote`), and peer-reflexive ones learnt from checks.
- `IceCandidate.parse` and `format` for SDP's `candidate:` lines (UDP; TCP and mDNS host names are skipped).
- Checks paced and retransmitted, triggered checks, role conflicts (487), and nomination: the controlling agent
  nominates every check; the controlled one uses the first pair that works until one is nominated.
- Keep-alive checks on the pair in use every 2 s; 30 s of silence, or no pair after 30 s, is `Failed`.

TURN relays come next. Written for Marivil's calls.
