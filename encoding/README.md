# encoding

Hex (RFC 4648 §8) and base64 (RFC 4648 §4, and the URL-safe alphabet of §5), in pure CeeSafe. It has no
dependencies and no C.

```ceesafe
import encoding.Base64;
import encoding.Hex;

var out = Vector<u8>.create();
Base64.encode(data, mut out)?;                 // appends padded standard base64
Base64.encodeUrl(data, mut out)?;              // URL-safe, unpadded (JWTs, URLs)
Base64.decode(text, mut bytes)?;               // strict: padding optional but correct, no whitespace
Base64.decodeIgnoringSpace(body, mut bytes)?;  // MIME bodies and folded headers: skips SP, TAB, CR, LF
Hex.encode(digest[..], mut out)?;              // lowercase; encodeUpper for uppercase
Hex.decode(text, mut bytes)?;                  // either case, even length
```

- **Output.** Encoders and decoders append to a `Vector<u8>` with one growth. Encoded output is ASCII, so
  `StringView.fromUtf8` always accepts it.
- **Errors.** A decoder that meets malformed input returns `DecodeError.Invalid` and leaves the vector as it was.
- **Tests.** `ceesafec test` checks the RFC 4648 §10 vectors, both alphabets, round trips at every length up to 300
  bytes, and malformed input.
