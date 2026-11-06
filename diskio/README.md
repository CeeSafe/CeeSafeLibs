# diskio

The file operations a crash-safe store needs that lib/std's `File` does not offer: positioned reads, fsync,
atomic rename, `mkdir -p`, directory sync, exclusive creation and modification times. A small C file
(`c/diskio_shim.c`) makes the POSIX calls.

```ceesafe
import diskio.Files;
import diskio.RawFile;

var file = RawFile.createNew("/var/mail/tmp/x.part")?;   // FileError.Exists if it is there
file.write(bytes)?;
file.sync()?;                                            // fsync
Files.rename("/var/mail/tmp/x.part", "/var/mail/ab/cd/abcd…")?;
Files.syncDirectory("/var/mail/ab/cd")?;                 // the rename survives a crash

let blob = RawFile.open(path)?;
blob.readExactAt(mut buffer[..], offset)?;               // pread: no shared file position
```

- **Errors.** `FileError`: `NotFound`, `Exists`, `Denied`, `NoSpace`, or `Os(-errno)`. `describe` gives words.
- **Blocking.** Every call may block on the disk, so async code makes them through `Async.blocking`, and the
  effect checker enforces that. Dropping a `RawFile` only closes it, which does not wait for the device, so async
  code may drop one.
- **Paths.** A path holding a NUL or longer than `PATH_MAX` is refused.
- **Platforms.** It runs on POSIX systems; it was tested on macOS. `fsync` is used as is: on macOS it does not force
  the drive's own cache (`F_FULLFSYNC`), the same choice SQLite and Postgres make by default.
