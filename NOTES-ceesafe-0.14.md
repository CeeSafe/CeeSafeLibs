# CeeSafe 0.14.0: issues met while writing sha256 and tls

**Status on 0.15.0 (2026-11-05):** item 1 still reproduces exactly as below. Item 2's first two points are fixed by
ADR 0071 and the packages now use `resize`, `extend`, `pushBE`, `readBE`, `writeBE` and `rotateRight`; there is no
`TODO(ceesafe 0.15)` left. The missing `??` remains.

## 1. `Async.run(Async.scope(…))` in sync code: result not delivered (bug)

```ceesafe
enum E { Oom, Bad }
fn main() {
    var y = 0;
    let outcome = Async.run(Async.scope(E.Oom, (s) => {
        s.spawn(async () => { y = 2; return Ok(); });
    }));
    match (outcome) { Ok() => { Console.writeLine(y); } Err(_) => {} }
}
```

- `ceesafec run --interp` reports `N0212: 'outcome' is read here but may not have been assigned a value`.
- The same happens with `Async.run(Async.all(async () => …, async () => …))`.
- If `outcome` is declared first (`var outcome: Result<void, E> = Err(E.Bad);`) and then assigned, it compiles. The
  assignment never takes effect: the program prints the `Err` branch.
- Workaround: put the scope inside an `async fn` and call `Async.run(thatFn(...))`. That works, and is what
  `tls/tests` does.

## 2. Friction that 0.15.0's items 1–6 should remove

- **`Vector<u8>` filling.** There is no bulk append or resize, so `TlsStream` fills its 16 KiB buffer with `push(0)`
  one byte at a time.
- **Rotates and big-endian loads.** `sha256` hand-writes `rotr` and the big-endian loads; both are marked
  `TODO(ceesafe 0.15)`.
- **Missing `??`.** `StringView.fromUtf8(b) ?? "fallback"` is not CeeSafe, so every such fallback is a four-line
  `match`. A null-coalescing operator, or `valueOr(default)` on optionals, would help.

## 3. Notes, not bugs

- **C calls in async code.** An `extern "C"` function without `@effects` counts as possibly sleeping, so calling it
  from async code is N0295. The fix is `@effects(alloc)` or `@effects()` on the declaration. The guide's
  unsafe-and-c chapter could say so next to the async rule.
- **`Handle` is a built-in name** (`SlotMap`'s handle), so a user `struct Handle` is N0201. That is easy to hit in FFI
  code.

## 4. Met on 0.15.0 (PostgresClient, encoding)

- **A character literal is a `u32`, so `pushBE('P')` writes four bytes.** It compiles silently; the Postgres Describe
  message came out malformed (`08P01 invalid message format`). `v.push('P')` on a `Vector<u8>` works, since the
  expected type fixes the literal. For `pushBE`/`writeBE`, a character literal could be rejected as having no
  fixed width (like the flexible integer literals, N0260), or default to `u8`.
- **`ceesafec test` collects a dependency's `@test` functions from its `src/`, then cannot call them.** A package
  whose library keeps its tests in `src/` breaks every dependent's `ceesafec test` with N0312 ("… is not public").
  Workaround: keep tests in `tests/` (`tests = ["tests"]` in the manifest), as `sha256` and `encoding` now do.
- **Not bugs, but surprises:**
  - A `StringView` or `Slice` local cannot be `var` (N0246), so a parser that narrows a view step by step has to
    keep offsets instead;
  - `match` binds by reference only on `match (borrow mut x)`; a `match (this.field)` on a non-Copy enum is N0251;
  - structs cannot hold `const`s (N0107), so related constants become top-level `OID_…` names.

## 5. Met on 0.15.0 (the pool, diskio, mstore)

- **No multi-line string literals.** SQL schemas and statements have to be one long line each. `mstore` keeps them
  in `.sql` files and generates `.cee` functions from them (`tools/embed-sql.py`). Raw or multi-line literals, or
  an `embed("file")` built-in, would remove the generator. This is ADR 0064's finding A3 again.
- **A string literal over 4095 bytes makes clang warn** (`-Woverlength-strings`, which ISO C99 only requires
  support up to). The C backend could emit long literals as a `char[]` initializer, or split them and let C join
  the pieces.
- **A dependency's name cannot name a local** (N0315). A library called `files` breaks every `let files = …` in a
  dependent package, so it was renamed `diskio`. Since a dependency is reached as `name.Item`, the local could
  shadow it with a warning instead.
- **Fieldless enums have no `==`** (N0207), so `kind == RecipientKind.User` needs an `isUser()` per variant, written
  with `match`. Derived equality for enums without payloads would be cheap.
- **Generics and scoped types.** `fn orElse<T: Copy>(v: T?, d: T) -> T` cannot take a `StringView` (N0274), so
  string fallbacks need their own `textOr`. Together with the missing `??` (§2), every nullable getter needs a
  helper.
- **Evaluation order and borrows.** `run(connection, await connection.batch(sql))` is N0241: the first argument's
  borrow is live while the second runs. The fix is an async helper that takes the connection once.
- **Effects work well.** The effect checker caught a real design bug: a `drop` that deleted a temporary file could
  not run in async code. The writer's drop now only closes the file, and an explicit `discard` deletes it.

## 6. Met on 0.15.0 (mmime)

- **A slice returned from a `borrow mut this` method stays borrowed from `this`.** A lexer's
  `fn token(borrow mut this) -> Slice<r, u8>` names the struct's region `r`, but the result still holds the mutable
  loan of `this`, so the next `lexer.skipSpace()` is N0241. Lexers return offsets instead and the caller slices. If
  a written struct region in the result tied it to that region only, lexers could return views.
- **Methods cannot declare region parameters** (N0216), and a method result borrowing from `this` cannot be written
  as `borrow(this) Slice<u8>` ("references to references"). So `ParamList.find` and `MessageIndex.header` became free
  functions, `fn findParam<region a>(params: borrow(a) ParamList, …) -> Slice<a, u8>?`.
- **Optionals have no methods.** `x.isSome()` is N0234, so each check is a `match`. Together with the missing `??`
  (§2), `isSome`/`isNull`/`valueOr` would remove many four-line matches.
- **`as` is narrower than expected.** `usize` to `i64` needs `truncate`, which reads as possibly lossy where it is
  not. An `as` between same-width integers that the value range proves safe, or `i64.fromSize(n)`, would read better.
- **Console output must be UTF-8.** `Console.write(StringView.fromUtf8(bytes)!)` panics on mail; `Console.writeBytes`
  is the answer and is documented, but easy to miss.
- **Effects caught real bugs again.** Every disk and iconv call is visibly `sleep` or effect-free, and the checker
  kept the scanner pure. The alloc sweep (1,042 runs) found no crash or leak: every injected failure reached the
  caller as `AllocError`.

## 7. Met on 0.15.0 (msmtp, bcrypt): two compiler bugs, with repros in `CeeSafeLibs/repros/`

- **`types-index-panic/`: the compiler crashes.** A library whose only function is
  `async fn accepted(listener: borrow AsyncTcpListener) -> bool { match (await listener.accept()) { … } }`, built
  with `ceesafec test`, makes ceesafec panic with `index out of bounds at src/types/types.cee:311:61`.
- **`listener-drop-glue/`: drop glue is missing from the C.** The same library plus an `Async.tasks` accept loop
  that `spawnSend`s a child gives C that calls `ceeM_16AsyncTcpListener_drop` without declaring or defining it. It
  shows in `ceesafec test` of a library that only borrows listeners; an executable that creates one builds.
  Workaround in msmtp: a test that binds a listener.
- **An `async move` child cannot move its captures out.** Task-set children own what they capture but read it
  through their environment, so `async move () => await serve(stream)` (ADR 0053's own example) is N0251 for any
  non-Copy capture. Workaround: carry the value in a one-element `Vector` that the child `pop()`s.
- **A timeout cannot wrap an operation that consumes a value.** For the same reason,
  `Async.timeout(ms, e, async () => await TlsStream.accept(stream, cfg))` cannot move `stream` in. The tls package
  gained `acceptWithin(stream, cfg, ms)`, which times out its own handshake. A `move` form of `Async.timeout` would
  be the general fix.
- **Match arms cannot be blocks with a value.** `let x = match (y) { A => { match (z) { … } } … }` is N0100. Each
  such case becomes a `var` and assignments, or a helper function.
- **Smaller ones:**
  - `AsyncMutexGuard.data` is a method: `guard.data()`;
  - `Async.tasks` takes a `u32` limit;
  - `[u8; 16] == [u8; 16]` is not allowed (an element loop instead);
  - `move` is a reserved word, so no `move` parameter.

## 8. Met on 0.15.0 (mdns, mauth, pkey)

- **Built-in names shadow user types silently, until they collide.** A `struct Check` in a library is N0201
  ("already defined") because the test harness's `Check` is a built-in. `Handle` (§3) is the same. A namespace for
  test helpers (`test.Check`) would free the names.
- **`.length` of a `const` array is N0216** ("needs an array variable or field"), so a 10,406-entry constant table
  needs a second constant for its length. Binding it to a local (`let rules = TABLE`) would copy the whole array.
- **More reserved words to avoid:** `own` (and `move`, §7). The N0100 message says so clearly; a list in the guide
  would help.
- **Slices have no `==`** (N0207), so tag parsers compare with `a.length == b.length && a.startsWith(b)`.
  `Slice<u8> == Slice<u8>` comparing contents would be natural; StringView already has it.
- **A temporary cannot be borrowed** (N0247): `f(make().view())` must become `let x = make(); f(x.view())`. That
  happens constantly in tests. Extending the temporary's life to the end of the statement, as C++ and Rust do, would
  fix it.
- **Good:** async recursion (SPF's `include` and `redirect`) just works, and the effect checker again kept every
  disk call (the DATA spool) on `Async.blocking`.

## 9. Met on 0.15.0 (msieve, Sieve delivery in msmtp)

- **No two-phase borrows.** `fail(script, script.nodes[c].line, "…")` with `fail(into: borrow mut Script, …)` is
  N0242: the argument is read after the mutable borrow begins. Rust evaluates the arguments first. The workaround
  was a separate `Problem` struct for the validator's error, copied into the script at the end.
- **The range's type does not follow a method parameter.** `for (i in 0..list.count())` with the body only passing
  `i` to `list.host(i: usize)` makes `i` an `i32` and errors (N0205), though `count()` returns `usize`. Either
  operand being `usize`, or the use as a `usize` argument, should decide.
- **No `let _ = …`** (N0100 "expected identifier"): a value that must be bound but is not needed takes a name.
- **A generic over `T` cannot take `void`:** `fn oom<T>(r: Result<T, AllocError>) -> Result<T, StoreError>` matching
  `Ok(v)` fails for `Result<void, …>` because `Ok()` has no payload (N0222). A second, void-only helper was needed.
  Letting `Ok(v)` bind the unit value would make such adapters generic.
- **`var name = "literal"` is N0246** (a reference variable must be `let`). Choosing between literals has to be one
  `let` with an `if` or `match` expression. The message is clear.
- **`"{x}"` in a string literal is printed as is.** There is no interpolation, and nothing warns. A lint for `{ident}`
  inside a literal passed to `Console.write*` would catch the C#/TypeScript habit.
- **Good:**
  - The 1,100-line evaluator and the 300-line vacation module passed check with only the errors above.
  - A closure with `?` inside `Async.blocking` (`w.write(bytes)?; return w.finish();`) just works.
  - The differential test against Pigeonhole found no crash and no memory problem, only semantic differences.
- **(Follow-up) The range inference gap again**, in a test: `for (i in 0..25) { appendNumber(mut key, i); }` with
  `appendNumber(…, n: usize)` is N0205. It is the second time in one slice, so it is likely to be common.
