# Gate 086: durable atomic byte writer

This Engine-only Windows adapter writes an unchanged owned byte span to an exclusively created sibling temporary file, calls `FlushFileBuffers`, closes the handle, and commits with `MoveFileExW` for a new destination or `ReplaceFileW` for an existing destination. It has no Core or JSON dependency. Other platforms report `operation_not_supported` rather than substituting a weaker buffered writer.

The successful replace behavior follows the canonical C# writer, including ordinary `.bak` rotation and the one-call option that preserves a previously recovered backup. The stronger failure policy is deliberate: documented `ReplaceFileW` errors 1176 and 1177 can represent partially moved names, so a surviving owned temporary file is retained and exposed through `AtomicFileWriteError`. Definite precommit failures clean only the operation's owned temporary file. The writer never deletes a primary or backup during cleanup.

Calls targeting the same normalized parent identity and case-folded filename are serialized in-process. Resolving the parent directory handle makes ordinary case aliases and 8.3 aliases of the parent directory share a lock. Different destinations use independent locks. An 8.3 alias of the destination filename itself, hard-link aliases with different filenames, and coordination across processes are outside this in-process locking claim; the no-overwrite new-file move still preserves a destination created by a racing process.

`REPLACEFILE_WRITE_THROUGH` is not used because Microsoft documents it as unsupported. The replacement file's contents are flushed before commit, but this gate does not claim a separately flushed directory entry or immunity from every storage-device failure.
