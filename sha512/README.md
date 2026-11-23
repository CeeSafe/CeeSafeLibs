# sha512

SHA-512 (FIPS 180-4) and HMAC-SHA-512 (RFC 2104) in CeeSafe, with no allocation: `sha256`'s sibling.

```cee
let digest = Sha512.hash(data);              // [u8; 64]

var h = Sha512.create();                     // or in pieces
h.update(part1);
h.update(part2);
let same = h.finish();

let tag = HmacSha512.mac(key, message);      // [u8; 64]
```

Tested against Python's `hashlib` and `hmac` (FIPS 180-4 and RFC 4231 vectors among them): `ceesafec test`.
