# qrcode

QR Code encoding (ISO/IEC 18004) in CeeSafe: bytes in, a square of dark and light modules out. Byte mode, versions 1
to 40, all four error correction levels, the mask chosen by the standard's penalty rules. It follows Project Nayuki's
QR Code generator.

```cee
let qr = QrCode.encode(text.bytes(), Ecc.Medium)?;   // the smallest version that holds it
for (y in 0usize..qr.size) {
    for (x in 0usize..qr.size) {
        if (qr.dark(x, y)) { /* draw a module */ }
    }
}
```

Leave four light modules around the code when drawing it (the quiet zone). `QrCode.encodeWith(data, ecc, minVersion,
mask)` fixes the version floor and the mask.

The tests compare every module with Python's `qrcode` for the same data, level, version and mask: `ceesafec test`.
