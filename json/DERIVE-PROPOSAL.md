# Proposal: `@derive(ToJson, FromJson)`

CeeSafe's `@derive` (type-system.md §28.7) makes `Equal`, `HashKey` and `OrderKey`. A JSON impl for a struct is
as mechanical as those, and today it is written by hand (README.md): one `w.field` and one `Decode.field` per field.
This proposes that `@derive` make them too, for any interface that follows the shape below, so the json package
needs no compiler support of its own.

## What it generates

For

```ceesafe
@derive(ToJson, FromJson)
struct Server {
    host: String;
    @json(rename = "listenPort") port: u16;
    weight: u32?;
    tags: Vector<String>;
}
```

the compiler writes what README.md's example writes by hand:

```ceesafe
impl ToJson for Server {
    fn toJson(borrow this, w: borrow mut JsonWriter) -> Result<void, JsonWriteError> {
        w.beginObject()?;
        w.field("host", this.host)?;
        w.field("listenPort", this.port)?;
        w.optionalField("weight", this.weight)?;
        w.sliceField("tags", this.tags.view())?;
        return w.endObject();
    }
}

impl FromJson for Server {
    fn fromJson(value: borrow JsonValue) -> Result<Server, DecodeError> {
        Decode.expectObject(value, "Server")?;
        return Ok(Server {
            host: Decode.field<String>(value, "host")?,
            port: Decode.field<u16>(value, "listenPort")?,
            weight: Decode.optionalField<u32>(value, "weight")?,
            tags: Decode.vectorField<String>(value, "tags")?,
        });
    }
}
```

- **Fields** in declaration order, as members of an object; the key is the field's name.
- **A field's type picks the helper**: `T?` uses `optionalField` (written only when present; missing or `null`
  decodes to null), `Vector<T>` uses `sliceField`/`vectorField`, `HashMap<String, T>` uses `key` + `map`/`mapField`,
  anything else `field<T>`, which needs `T: ToJson` / `T: FromJson` (N0268 at the `@derive` otherwise, as for
  `HashKey`). The helpers exist because CeeSafe has no generic impl for `Vector<T>` or `T?` (ADR 0054 allows generic
  impls only for the program's own generic types); if that is lifted, every field becomes `field<T>`.
- **Enums.** A fieldless enum is a string of the variant's name (`"Active"`). An enum with payloads is serde's
  default "externally tagged" form: `{"Variant": payload}` for one payload, `{"Variant": [a, b]}` for several, and
  the bare string for a variant without one.
- **Generic types** get bounded impls, `impl<T: ToJson> ToJson for Wrapper<T>`, which ADR 0054 already allows.
- **Errors.** Decoding names the path, as the hand-written code does (`$.servers[2].listenPort: expected u16, found
  string`); the type's name is what `expectObject` says it expected.

## Field and container attributes (serde's, the ones worth having)

| Attribute | Meaning | serde |
|---|---|---|
| `@json(rename = "k")` on a field | the member's key is `k` | `#[serde(rename)]` |
| `@json(renameAll = "camelCase")` on the type | every key in that case: `camelCase`, `snake_case`, `kebab-case`, `SCREAMING_SNAKE_CASE`, `PascalCase` | `#[serde(rename_all)]` |
| `@json(default)` on a field | a missing member decodes to the type's default (`Decode.fieldOr`); the type needs a `Default`-like interface, or `@json(default = CONST)` names a constant | `#[serde(default)]` |
| `@json(skip)` on a field | never written, and decoded as its default | `#[serde(skip)]` |
| `@json(skipIfNull)` (the default for `T?`) / `@json(writeNull)` | an absent optional is left out / written as `null` | `skip_serializing_if = "Option::is_none"` |
| `@json(flatten)` on a struct field | its members are written into, and read from, the enclosing object | `#[serde(flatten)]` |
| `@json(denyUnknownFields)` on the type | a member no field names is `UnknownField` (`Decode.denyUnknownFields`) | `#[serde(deny_unknown_fields)]` |
| `@json(alias = "old")` on a field | also accepted under `old` when decoding | `#[serde(alias)]` |
| `@json(tag = "type")` on an enum | internally tagged: `{"type": "Variant", …fields}` | `#[serde(tag)]` |
| `@json(untagged)` on an enum | the first variant that decodes wins | `#[serde(untagged)]` |
| `@json(with = Module)` on a field | `Module.toJson(borrow T, w)` and `Module.fromJson(value) -> Result<T, DecodeError>` instead of the type's impls, for foreign types | `#[serde(with)]` |

Optional fields follow serde: an `Option` field is optional when decoding (missing and `null` are both null) and,
with the default `skipIfNull`, left out when writing; a non-optional field that is missing is `MissingField`.

## How the compiler would find the interfaces

`@derive(Equal)` knows lib/core's `DerivedEqual`. A package-provided derive needs a rule for which interfaces may be
derived. The smallest one: an interface may be derived if it is marked `@derivable(fieldwise = …)` and names, for
each method, the helper calls to emit per field; json would mark `ToJson` and `FromJson`. A smaller first step that
needs no new mechanism is a `ceesafec` code generator (`ceesafec derive json src/config.cee`) that writes the impls
above into a file the package checks in.
