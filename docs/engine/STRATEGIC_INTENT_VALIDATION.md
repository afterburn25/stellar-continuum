# Strategic intent validation

The retained actual C# oracle produces 50 builder cases, 10 ordered provider operations, seven formatting probes, and five explicitly source-only null observations. It pins invariant current and UI culture so fixture output is reproducible across machines.

Builder cases compare civilization ID, generated and review ticks, every declared priority weight, an unknown numeric weight, preferred fleet role, colonization deferral, and exact summary text. They cover negative values, signed zero, threshold neighbors, duplicate overwrite, unknown enums, NaN, both infinities, and empty plans. Formatting probes cover both infinities, both signed zero inputs, `.125`, `.375`, and NaN through the builder's interpolated `0.00` path.

The provider fixture replays every operation against the actual source and native providers. After each query, intent publication, overwrite, second-ID publication, review publication, NaN/infinity publication, removal, or clear, it compares both counts and all observed `GetWeights` and `GetPreference` results. Raw intent weights exercise every formula term plus upper and lower clamps. The review case uses plan civilization ID 999 and intent civilization ID 43. The first clear is performed on nonempty state.

Fixture text uses validated uppercase UTF-8 hexadecimal instead of an unsafe signed-shift base64 decoder. Native-only ownership probes mutate caller values after publication and are reported separately from source parity.

The regenerated fixture SHA-256 is `83818C6283D0C9685D950C2BACA982A90C03B8E6708D0745DA2E883DC0AA014A`.

Standalone Debug and Release consumers compiled with MSVC C++20, `/W4 /WX`, explicit debug/release runtimes, and all object, program database, and executable outputs under the ignored gate workspace. Both reported `validated 50 intent cases, 10 provider operations, 7 formatter cases, and 5 explicitly source-only null records`. Their executable SHA-256 values are `FD07DEA1167C8F2DB22DBBC6294ECFA80F14EC90A304160C5B06E1449266E85B` for Debug and `D6511EC8FF10F68BE6CE065B1C25D527C108E21D58F454507A1A873075B40D64` for Release.

The maintained `windows-testing` build passed 40 of 40 CTest targets, including `strategic_intent_parity`, and 19 of 19 Python recovery/package-integrity tests. Its complete log is `work/native-032-strategic-intent-testing.log` with SHA-256 `549E3FB8F60B28C3309F9D24D88ECCCAFD2454576467C09F21323DC2214DA6CD`.
