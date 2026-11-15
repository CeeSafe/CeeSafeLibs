# json

JSON (RFC 8259) in the spirit of Rust's [serde_json](https://github.com/serde-rs/json): a tree (`JsonValue`) that keeps
integers exact and objects in insertion order, a strict parser that does not recurse, compact and pretty writers, a
streaming `JsonWriter` that needs no tree, and typed encoding and decoding through two interfaces, `ToJson` and
`FromJson`. It has no dependencies and no C.

```ceesafe
import json.Json;
import json.JsonValue;
import json.JsonWriter;

let doc = Json.parse(text)?;                     // Result<JsonValue, JsonError>; `text` is a Slice<u8>
match (doc.pointer("/servers/0/port")) {         // RFC 6901, with ~0 and ~1: a borrow JsonValue?
    Some(v) => { let port = v.asU64() ?? 80; }   // asStr, asBool, asU64, asI64, asF64: optionals
    null => {}
}
let name = doc.get("name");                      // get(key), at(index): borrow JsonValue?
var out = Vector<u8>.create();
doc.write(mut out)?;                             // compact; writePretty for serde_json's 2-space form

var cfg = JsonValue.object();                    // builders stand in for serde's json! macro
cfg.insert("name", JsonValue.string("edge")?)?;
cfg.insert("port", JsonValue.fromU64(8080))?;

var w = JsonWriter.pretty();                     // streaming: commas, colons and indentation are its business
w.beginObject()?;
w.key("name")?;
w.string("edge")?;
w.field("port", 8080u16)?;                       // any ToJson value
w.endObject()?;
let bytes = w.finish()?;                         // Incomplete unless one whole value was written

let config = Json.decode<Config>(text)?;         // FromJson, below; errors name the path
let encoded = Json.encodePretty(config)?;        // ToJson
```

## Behaviour

- **Numbers.** `JsonNumber` is `PosInt(u64)` for an integer from 0 up, `NegInt(i64)` for a negative one, and
  `Float(f64)` for the rest, as serde_json keeps them: `18446744073709551615` and `-9223372036854775808` stay exact.
  An integer beyond both ranges becomes a float. A number too large for an `f64` (`1e400`, a 400-digit integer) is
  `NumberOutOfRange`, as in serde_json; one too small becomes `0.0`. `-0` is the float `-0.0`, as in serde_json.
  Floats are written in serde_json's (ryu's) form: the shortest digits that read back as the same `f64`, with `.0`
  on integral values and an exponent from 10^16 up or below 10^-5 (`1.0`, `0.1`, `1e16`, `1.5e-7`, `0.00001`,
  `1e-6`, `-0.0`). NaN and the infinities cannot be written: the writer returns `NonFiniteNumber` and writes nothing,
  and `JsonValue.fromF64` turns them into `Null`, as serde_json's `From<f64>` does.
- **Strings.** The text must be UTF-8 (checked once, up front); a `\u` escape of half a surrogate pair without the
  other half is `LoneSurrogate`. Writing escapes `"`, `\` and the controls (`\n` `\r` `\t` `\b` `\f`, else `\u00XX`)
  and leaves the rest of UTF-8 as it is. `WriteOptions.asciiOnly` writes everything outside ASCII as `\uXXXX`, and
  `WriteOptions.escapeEndTag` writes `</` as `<\/` for JSON inside an HTML `<script>`. For bytes that may not be
  UTF-8, `JsonWriter.stringBytes` refuses them (`InvalidUtf8`, nothing written) and `stringLossy` writes each
  ill-formed sequence as U+FFFD.
- **Objects** keep insertion order (serde_json's `preserve_order`), and compare as maps (`equals` ignores the order).
  A key that is inserted again keeps its place and takes the new value. Lookups scan up to a dozen members and use a
  SipHash index, keyed from the system's random source, beyond. Parsing a duplicate key keeps the last value in the
  first one's place, as serde_json does; `ParseOptions.rejectDuplicateKeys` makes it `DuplicateKey`.
- **Strictness.** No comments, trailing commas, single quotes, NaN, infinities, leading zeros or `+`; whitespace is
  SP, TAB, LF and CR only; a byte-order mark is refused (`ExpectedValue`), as serde_json refuses it.
- **Limits.** `ParseOptions.defaults()` allows 128 nested arrays and objects (serde_json's limit) and 64 MiB of text.
  The parser keeps the open containers on the heap, so a deeper text is `DepthLimitExceeded` rather than a stack
  overflow, whatever `maxDepth` allows. Writing a tree recurses once per level and is limited by
  `WriteOptions.maxDepth` (128). `clone`, `equals` and dropping a tree also recurse once per level, which is
  harmless at the default depth; a tree parsed with a much larger `maxDepth`, or built by hand that deep, should be
  destroyed with `value.dispose()`, which takes it apart on the heap (the interpreter's stack does not hold a
  100000-level drop: repros/deep-drop-interpreter).
- **Errors.** `JsonError` has a `kind` (`JsonErrorKind`, an enum of reasons with a `message()`), the `line` and
  `column` (from 1, in bytes) and the byte `offset`; `describe` writes serde_json's `trailing comma at line 1 column
  4`. Allocation failure is `OutOfMemory` everywhere, never a panic. `JsonWriteError` says why a write failed, and the
  writer refuses misuse: a value in an object without its key (`ExpectedKey`), a key outside an object
  (`UnexpectedKey`), an end that does not match (`UnbalancedEnd`), a second value at the top (`AlreadyComplete`).

## Typed encoding and decoding

`@derive` cannot make these yet ([DERIVE-PROPOSAL.md](DERIVE-PROPOSAL.md) proposes it), so a struct's impls are
written by hand, one line a field:

```ceesafe
struct Server {
    host: String;
    port: u16;
    weight: u32?;
    tags: Vector<String>;
}

impl ToJson for Server {
    fn toJson(borrow this, w: borrow mut JsonWriter) -> Result<void, JsonWriteError> {
        w.beginObject()?;
        w.field("host", this.host)?;
        w.field("port", this.port)?;
        w.optionalField("weight", this.weight)?;   // left out when null
        w.sliceField("tags", this.tags.view())?;
        return w.endObject();
    }
}

impl FromJson for Server {
    public fn fromJson(value: borrow JsonValue) -> Result<Server, DecodeError> {
        Decode.expectObject(value, "a server")?;
        return Ok(Server {
            host: Decode.field<String>(value, "host")?,              // MissingField if absent
            port: Decode.field<u16>(value, "port")?,                 // range-checked: 70000 is OutOfRange
            weight: Decode.optionalField<u32>(value, "weight")?,     // missing or null: null
            tags: Decode.vectorField<String>(value, "tags")?,
        });
    }
}
```

`fromJson` is written `public` here for ceesafec 0.48.2 and earlier, which refuse a static function of an impl in
another package than the interface's when the json package's generic code calls it (`Decode.vector<Server>`). From
0.48.3 (CeeSafe's F16 fix) it is as visible as the interface, as the specification says, and `public` changes
nothing (repros/static-impl-visibility). `toJson` needs nothing.

A decoding error names where it is: `$.servers[1].port: expected u16, found string` (`DecodeError.message()`;
`reason` is a `DecodeReason`). The library implements both interfaces for the integer types (range-checked on
decode; a float is never an integer), `bool`, `f64`, `f32`, `String` and `JsonValue`. `Decode.fieldOr`,
`Decode.mapField` (to a `HashMap<String, T>` in the object's order) and `Decode.denyUnknownFields` cover serde's
`default`, maps and `deny_unknown_fields`; `JsonWriter.map`, `slice`, `optional` and `value` are the writing side.

**What CeeSafe cannot express yet.** A generic impl may only be for a generic struct or enum of the program (ADR
0054), so `impl<T: ToJson> ToJson for Vector<T>` and `for T?` are N0267, and so is a `HashMap` impl. That is why
vectors, optionals and maps go through the `slice`/`optional`/`map` and `vectorField`/`optionalField`/`mapField`
helpers rather than `field<T>`, and why `Json.encode(v)` takes a vector only as `JsonWriter.slice`. With such impls,
every field would be one `field<T>` call.

## For a program writing a config

`JsonWriter.pretty()` writes serde_json's `to_string_pretty` byte for byte: members in the order written, two spaces
an indent level, `": "` after a key, `[]` and `{}` for empty containers, and no newline at the end (add one when
writing a file). Strings from bytes that may not be UTF-8 go through `stringBytes` (an error, so nothing invalid is
written) or `stringLossy` (U+FFFD for each bad sequence); `keyBytes` is `stringBytes` for keys.

## Tests

`ceesafec test` checks: every kind of value; integers at u64 max and i64 min, `-0`, `1e400` and other overflows,
underflow, exponent forms and malformed numbers; every escape, surrogate pairs and lone surrogates; invalid UTF-8
and where it is reported; line and column of errors; the depth limit (128 levels pass, 129 and 100000 fail cleanly,
100000 parse when allowed); the input limit; duplicate keys both ways, small objects and indexed ones; exact
compact and pretty output; float output against ryu's form and the round trip of 4000 random floats; escape
options; lossy and strict byte strings; the streaming writer's checks; builders, accessors, `insert`/`remove` order
in a 1000-member object; JSON Pointer with RFC 6901's examples; equality and copies; typed decoding of a nested
config with every error path; integer ranges of each type.

**JSONTestSuite.** `tests/JSONTestSuite/` holds its `test_parsing/` files and LICENSE (see its README for the
commit). `tests/suite_tests.cee` reads them with `Directory.list`: all 95 `y_` files parse, all 188 `n_` files are
refused, and each of the 35 `i_` files does what `tests/JSONTestSuite/i_results.txt` records (serde_json's outcomes:
5 accepted, 30 refused). It also writes each `y_` value compact and pretty and parses it back, and does the same for
400 generated values with every write option (`parse(write(v)) == v`).

All 40 tests pass natively and with `ceesafec test --interp` (which takes several minutes: the interpreter's float
printing and the 100000-level text are slow there). `ceesafec test --sweep alloc` finds no failure: no panic,
crash or leak in the library at any failed allocation (about 2000 runs; tests with many allocations are sampled).
