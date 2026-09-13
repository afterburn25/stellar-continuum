# Validation

- Five actual-source success rows cover new nested Unicode output, exact UTF-8 without BOM, ordinary backup rotation, one-call backup preservation followed by normal rotation, empty content, and unrelated temporary-file retention.
- Actual-source fixture SHA-256: `908E01AFBDF31E3B7E532A8A6A570815B4534313A038CBEF227AA9D31D1248D1`; two runs were byte-identical and the invoked C# source is pinned before and after.
- Native-only boundaries cover arbitrary bytes including NUL, spaces/Unicode, long and real 8.3 parent paths when available, locked replacement, injected partial-write/flush/pre-replace interruptions, a new-file race, same-destination serialization, independent destinations, and unrelated sibling temporary files.
- Exclusive-create ownership tests retain one collided foreign temp through success, retain all 32 foreign collisions on exhaustion, and route an access-denied result through the production create-failure branch while retaining its foreign temp. Embedded-NUL paths are rejected before Win32 truncation. Injected successful-zero-byte `WriteFile` also passes through the production zero-write failure branch. An injected error 1177 retains a complete owned recovery temp and exposes all structured failure paths.
- Strict Debug and Release compiled with `/std:c++latest /W4 /WX /permissive- /fp:precise /Z7`; each also compiled the production translation unit without the test macro. Both full replays passed.
- Missing arguments, missing source root, and missing fixture probes each exited 1 with exception type/message, current directory, and absolute source/fixture paths or placeholders.

## Maintained integration

Registered on Windows as `atomic_file_write_parity`. The normal Engine translation unit compiles without test hooks. Maintained actual-source generator: `tests/Stellar.AtomicFileWrite.ParityGenerator`. Combined validation passed 97/97 CTest and 29/29 Python checks.

- `engine/include/stellar/engine/atomic_file_write.hpp` SHA-256 `7033BA02E379FF881B00149C2F39F339ACD43059E62285C4DB9910CDF10715F1`
- `engine/include/stellar/engine/detail/atomic_file_write_test.hpp` SHA-256 `15E1C151B1498E0170ED4B5CF4A7C9DE09C71603BC81C74C1B8278CD25555E4D`
- `engine/src/atomic_file_write.cpp` SHA-256 `AFEC0E5623EA94C9D758E44F008F0F079BEA3FCC8B81135416D9113872A29840`
- `native-tests/atomic_file_write_tests.cpp` SHA-256 `C13335150D3BE0F0145F7BAD82EC25C3CA80FDEABE8DB43CD18F0160EA711500`
- `native-tests/fixtures/atomic-file-write.json` SHA-256 `908E01AFBDF31E3B7E532A8A6A570815B4534313A038CBEF227AA9D31D1248D1`
- `tests/Stellar.AtomicFileWrite.ParityGenerator/Program.cs` SHA-256 `4C3290DD0EB825688EA5CB00297E485A59742A07E0BB3A07A45F4A6BA8AA1896`

## Engine 0.1.39 maintained validation

`atomic_file_write_parity` passed in the combined 97/97 native suite; 29/29 packaging checks passed (`work/native-039-testing.log`). The maintained generator is `tests/Stellar.AtomicFileWrite.ParityGenerator`. Its Program.cs is byte-identical to the reviewed oracle; only the project reference and native helper includes were relocated.

- Retained fixture SHA-256: `908E01AFBDF31E3B7E532A8A6A570815B4534313A038CBEF227AA9D31D1248D1`
- Maintained Program.cs SHA-256: `4C3290DD0EB825688EA5CB00297E485A59742A07E0BB3A07A45F4A6BA8AA1896`
