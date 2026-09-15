# Gate 083C validation

The retained actual-source generator is `../campaign-knowledge-oracle-083c/Program.cs`. It invokes the private `CampaignSaveService.ToKnowledge` and `ToKnowledgeDtos` methods and uses the public knowledge API for complete state projection and the successful continuation command. Two generations produced identical 24-row fixture bytes with SHA-256 `780E6A03D3CE0F8AEF21EFBB05690AC231338570630900B4821A3A51869F87C9`.

Rows cover empty/null inputs, null records and nested collections, core access/exploration and its ordering failures, legacy known-system behavior, repeated DTOs, all survey levels, unknown/sparse surveys, repeated surveys, NaN and both infinities, known-civilization ordering, core-only capture, mixed capture, and a follow-up public survey operation on a successfully restored state. Every row retains complete input before/after, result or exact typed error, and continuation state/result where applicable.

The native checker verifies the fixture and ordered two-file source inventory before replay and verifies all bytes again afterward. It decodes inputs before the production catch, projects results afterward, asserts capture presence, compares integers exactly with a `2^53+1` regression, and limits `1e-10` tolerance to floating comparisons. Owned restore/capture probes prove detached nested collections.

Final isolated Debug and Release builds both passed `24/24` rows and all probes under C++ latest, `/W4 /WX /permissive- /fp:precise /Z7`, explicit `/Fo` and `/Fd`, with optimized Release. Native missing arguments, fixture, and source root and managed missing source root each returned exit 1 with readable diagnostics; the managed failure created no output. Logs are under `build/debug`, `build/release`, and `managed-missing-source.log`.

Final SHA-256 values:

- public header: `B07EE1DA31D83E921B204763CE102BB4F15AB7B435E530640B95B9CF1A925420`
- production source: `B619925905E90DFAA2FAC485D73E46A076362CD66381F36D8B82117BBB743333`
- native checker: `8681107BAAE189A483B9EF6F9133AAFA7F09AFFC4334D3298D880B103C5BA1DC`
- managed generator/project: `660AD21ABC920A803194AAA28AF1835E9A7BC23CB300976EB1E8EA7A10EC29C5` / `E418F49AFF08929FF81B04A99346D0605B8410A855E06BF24EECF167CBCDE3FE`

## Maintained integration

Registered as `knowledge_persistence_parity`; actual-source generator `tests/Stellar.KnowledgePersistence.ParityGenerator`. Program and fixture bytes are unchanged. Only the project reference and native helper include paths are relocated. Combined maintained validation passed 94/94 CTest and 29/29 Python checks (`work/native-038-testing.log`). The maintained generator reproduced the sealed fixture byte-for-byte.

- `core/include/stellar/core/knowledge_persistence.hpp` SHA-256 `B07EE1DA31D83E921B204763CE102BB4F15AB7B435E530640B95B9CF1A925420`
- `core/src/knowledge_persistence.cpp` SHA-256 `B619925905E90DFAA2FAC485D73E46A076362CD66381F36D8B82117BBB743333`
- `native-tests/knowledge_persistence_tests.cpp` SHA-256 `8681107BAAE189A483B9EF6F9133AAFA7F09AFFC4334D3298D880B103C5BA1DC`
- `native-tests/fixtures/knowledge-persistence.json` SHA-256 `780E6A03D3CE0F8AEF21EFBB05690AC231338570630900B4821A3A51869F87C9`
- `tests/Stellar.KnowledgePersistence.ParityGenerator/Program.cs` SHA-256 `660AD21ABC920A803194AAA28AF1835E9A7BC23CB300976EB1E8EA7A10EC29C5`
