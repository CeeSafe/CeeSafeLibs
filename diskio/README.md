# diskio

The file operations a crash-safe store needs, in one small API: positioned reads, fsync, atomic rename,
`mkdir -p`, directory sync, exclusive creation, appending and modification times. Since 0.2.0 it is plain CeeSafe
over lib/std's `File` and `Directory` (CeeSafe 0.21, ADR 0075 and ADR 0077). It has no C file, so it builds the same
on every system CeeSafe supports. Its API is the one 0.1.0's C shim had.

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
- **Permissions.** A file diskio creates has mode 0640, and a directory 0750, whatever the umask
  (`File.setPermissions` on the new, still empty entry).
- **Platforms.** Wherever CeeSafe runs. It was tested on macOS and Linux. `sync` is lib/std's `syncAll`: `fsync`, and
  on macOS `F_FULLFSYNC`, which also flushes the drive's own cache. That is stronger, and slower there, than 0.1.0's
  plain `fsync`.
- **Errors.** `NoSpace` is lib/std's portable `IoError.StorageFull`. `Os` carries the negative errno where lib/std
  has one, else `-5` (EIO).
