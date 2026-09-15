# Strategic input support validation

The actual C# oracle generated 56 combat-readiness cases, 22 prototype-capability cases, and three explicitly source-only null observations. The source oracle compares complete serialized input state before and after every call. The native fingerprint covers every decoded field relevant to this API, including the complete combat record; the production API's const view provides the all-field nonmutation boundary. A second Release run reproduced the fixture byte-for-byte.

The readiness matrix covers every fleet role and combat profile, missing and invalid profiles, inactive and foreign fleets, missing civilizations, negative/over-limit/nonfinite persisted durability, unknown orders, retreat, matching and mismatching nullable disengagement systems, exact hull/damage epsilon boundaries, stable equal-ID ordering, unsorted fractional accumulation, every result counter, both ratios, and complete source/native input preservation.

The capability matrix covers exact-case construction lookup, absent civilizations and IDs, duplicate technology states with first-match behavior, affirmative spacecraft and experimental transit mappings, and explicit false results for reliable and extended transit even when those named legacy technologies are completed in the first matching record. Similarly named raw, unknown, and case-mismatched IDs also remain false. It does not materialize Adaptive Research capabilities.

Five capability cases are affirmative: two construction queries, two spacecraft-construction queries across distinct first-record arrangements, and one experimental-interstellar-transit query. The named reliable and extended technology cases remain false.

Standalone MSVC Debug and Release builds use C++23, `/W4 /WX`, explicit debug/release runtimes, and place `/Fo`, `/Fd`, and executable outputs under this ignored draft folder.

Both strict executables reported `validated 56 combat readiness cases, 22 prototype capability cases, and 3 source-only null observations`. The Debug executable SHA-256 is `8D9D484C618C8D7611DC3A1AE2E6E0780E989F18FA32671CDB986F95C624A00C`; the Release executable SHA-256 is `4044E1916F9691186A87678293E6E024631A8FCB92F91488FA85FAAB7F364C7E`.

The retained Release oracle reproduced the promoted fixture byte-for-byte. Fixture SHA-256: `8D95C249B63D6C94430AD478313DBF9215F9AFF04F0BB6B02F3E6B97A85691A3`.

The combined maintained `windows-testing` build passed 43 of 43 CTest targets and 19 of 19 Python recovery/package-integrity tests. `strategic_input_support_parity` passed as test 40. The complete log is `work/native-034-strategic-input-support-testing.log`, SHA-256 `1142C64689D722AAC20948A53BC6C8D647A35BA70C900D34EEDA2BB385FE2930`.
