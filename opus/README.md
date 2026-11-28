# opus

Opus (RFC 6716, with RFC 8251's updates) in CeeSafe, ported from libopus 1.5.2 (BSD; its notice is COPYING.libopus
below): the decoder now, the encoder next. No C.

- `OpusDecoder.create(channels)` at 48 kHz, mono or stereo; `decode(packet, mut pcm, frameSize, fec)` into 16-bit
  samples (soft-clipped, as `opus_decode`), `decodeFloat` into floats; a null packet is concealed; `fec` decodes the
  previous packet from this one's in-band FEC. `finalRange` is libopus's `OPUS_GET_FINAL_RANGE`.
- SILK (`silk_*.cee`) is fixed-point and bit-exact with libopus; CELT (`celt_*.cee`) is libopus's float build;
  `decoder.cee` is `opus_decoder.c` (hybrid, the redundant frames of mode switches, the transitions' cross-fades);
  `packet.cee` parses packets; `range.cee` is the range decoder.

**Tested** with RFC 8251's twelve test vectors (`tools/run_vectors.sh <vectors> <opus_compare>`, which runs
`tools/vectorcheck`): every packet's final range matches libopus's, and `opus_compare` passes all twelve in stereo and
in mono. SILK alone (`tools/silkcheck`) is byte-identical to libopus on vectors 02 to 04, lost packets included; CELT
alone (`tools/celtcheck`) is sample-identical to libopus's plain C build on 01, 07 and 11, concealment included.

Not yet: an output rate below 48 kHz; speed (about 20 times real time on an M-series Mac, unoptimised).

## libopus's notice

Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell,
Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon. Redistribution and use in source and binary forms, with or
without modification, are permitted provided that the conditions of libopus's COPYING (3-clause BSD) are met; this
port keeps that notice and those conditions.
