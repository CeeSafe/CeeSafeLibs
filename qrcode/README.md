# qrcode

QR Code encoding and decoding (ISO/IEC 18004) in CeeSafe. Encoding: bytes in, a square of dark and light modules out. Byte mode, versions 1
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

## Reading

```cee
// A grayscale image (a camera frame's luminance: Android's Y plane), `stride` bytes from one row to the next.
match (QrReader.read(luminance, width, height, stride)) {
    Some(code) => use(code.data.view()),   // and code.version, code.ecc
    null => {},                             // no code, or not readable
}
let again = QrReader.readModules(modules, size);   // from modules, as QrCode.dark gives them
```

The image is made black and white against local means (ZXing's hybrid binarizer), the three finder patterns are found
by their 1:1:3:1:1 runs and confirmed down and across (two scans at least when there are three such), the version
comes from their distance (and its neighbours when that is a little off), the alignment pattern is looked for along
the code's own axes, and each module is read through the perspective the four points give; a mirrored code is read
too. The modules then go back to bytes: the nearest valid format information, the mask taken off, the blocks put back
together, each corrected by Reed-Solomon (Berlekamp-Massey, Chien, Forney), and the numeric, alphanumeric, byte and
kanji segments (kanji as Shift JIS; ECI and structured append read past).

The tests read the encoder's codes back from their modules (every level, masks, versions 1 to 40, with flipped
modules corrected), and from pictures drawn of them: straight, turned, in perspective, shaded and noisy. A 640 by 480
frame of a phone's screen, blurred and in perspective, reads in about a millisecond.
