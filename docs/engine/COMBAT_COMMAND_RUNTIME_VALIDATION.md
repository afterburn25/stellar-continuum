# Combat Command Runtime Validation

The matched combat command runtime is checked against 60 multi-command cases produced by the actual C# `CombatCommandRuntime`, preview, and batch services. The cases cover every order preview outcome, direct issuance for every order kind, stable sorted batch behavior, duplicate and empty selections, mixed batch acceptance, engage-hostiles privacy and candidate ordering, default peaceful construction, stateful and throwing hostility policies, and advancement through the retained simulation.

Each command records its complete result or exact source error, ordered hostility calls, and the full system and fleet state immediately afterward. The native consumer decodes commands and expected values before entering the production exception capture, records only typed callback calls inside callbacks, and serializes and compares results afterward. Integer values are compared exactly. Preview cases retain nondefault combat and vessel history and verify that the read-only path neither initializes nor normalizes fleet combat state.

The fixture separately records 11 actual C# null-argument observations. These calls cannot be expressed through the typed native spans and references and are not counted as native parity passes. One native-only ownership boundary verifies that the runtime is movable and noncopyable: it previews, moves, issues, advances, moves again, and advances again while retaining one stateful hostility callable and the simulation's engagement history.

The canonical fixture SHA-256 is `C3A8F6D30E353A7397E6981A773DCAED9484BBF4390E439F82CC9C29C3F94829`, and the retained oracle regenerates it byte for byte. Standalone Release (`/O2`) and Debug (`/Od`) builds both pass with `/W4 /WX`, reporting `60 actual C# cases and 1 native boundary passed`.

This gate covers the matched command surfaces and the accepted strategic combat simulation. It does not add tactical movement, diplomacy ownership, foreign-intelligence disclosure, or the massive-combat campaign layer.
