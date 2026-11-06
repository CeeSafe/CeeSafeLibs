# sha256

SHA-256 (FIPS 180-4), HMAC-SHA-256 (RFC 2104) and PBKDF2-HMAC-SHA-256 (RFC 8018), in pure CeeSafe. The package has no
C, no allocation and no dependencies, so it runs in the interpreter and in freestanding code too.

```ceesafe
import sha256.Sha256;
import sha256.HmacSha256;

let digest = Sha256.hash(data);                 // [u8; 32]

var h = Sha256.create();                         // incremental
h.update(part1);
h.update(part2);
let d = h.finish();

let mac = HmacSha256.mac(key, message);
var out = [0u8; 32];
sha256.pbkdf2(password, salt, 4096, mut out[..]); // SCRAM-SHA-256's SaltedPassword
let same = sha256.constantTimeEqual(a, b);       // for comparing MACs and proofs
```

All three are Copy values, about 108 bytes each. A keyed `HmacSha256` can be copied to MAC several messages under one
key, and `pbkdf2` copies one per iteration instead of re-keying.

## Speed

With `--cflag -O3` on an Apple M-series CPU, it hashes about 190 MB/s. OpenSSL reaches about 2 GB/s with the ARMv8 SHA-2
instructions, which CeeSafe cannot reach without C or intrinsics. That is ample for authentication (SCRAM, HMAC
tokens). For hashing bulk mail bodies, a C-accelerated compression function could be added behind the same API.

## Tests

`ceesafec test` checks the FIPS 180-4, RFC 4231 and RFC 7914 vectors. Their expected values come from Python's
`hashlib`. The tests also cover the padding edges at 55, 56 and 64 bytes, every split point of an incremental update,
and a million `a`s.
