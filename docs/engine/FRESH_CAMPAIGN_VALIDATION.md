# Fresh campaign validation

Clean exact-commit engine 0.1.12 package: `Builds/Windows/StellarContinuum-windows-benchmark-73aaa813-20260913T044030295559Z`, commit `73aaa81344bf15b77487dcd65fb1a543e3bc47d6`, `sourceDirty: false`. The export passed 30/30 CTest, 19/19 Python checks and relocated fresh-campaign validation with seven sealed runtime files. Log: `work/native-022-clean.log`. Complete initialization means across three repeats were 8.2055 ms (250 systems), 17.0341 ms (500), 45.6532 ms (1000), and 417.3655 ms (2500). These are initialization timings, not FPS or full simulation ticks. The evidence below records the preceding pre-commit review and its limits.

Fresh campaign parity fixture SHA-256: `322BC2C58F54B86E93BADFD0FDE7A507B240712F6AEDF646EB9F4A82608A862E`.

The reviewed engine 0.1.12 testing, Release, and Debug runs passed 30/30 CTest and 19/19 Python checks. The earlier 18-check run predates the help/option-value regression fix. This validates complete fresh initialization and diagnostic output, not a player save or full campaign runtime.

Nine actual-C# cases cover five complete campaigns across 250/500/1000/2500 systems and four species/count mixes, three exact failures (settings versus generation stages identified), and one WarpCapable colony-population reservation composition. Every serialized state field is compared, including knowledge, construction, technology and shipyards. Successful full seeds are repeated. The native constrained-home fallback flag is tested for repeat determinism only because the C# result does not expose it; semantic fallback coverage remains in the founding parity gate.

Default founding creates PreWarp and AncientSpacefaring civilizations. The authoritative fleet seeder only creates fleets for WarpCapable civilizations, so the default fresh result correctly has zero fleets. The separate WarpCapable case verifies actual fleet creation and population conservation without changing those source rules.

Reviewed pre-commit Release: `Builds/Windows/StellarContinuum-windows-benchmark-4763cba2-20260913T043305145225Z`.

Reviewed pre-commit Debug: `Builds/Windows/StellarContinuum-windows-development-4763cba2-20260913T043441991554Z`.

Both report engine 0.1.12, source `4763cba2810e5c109d5834a26ea0072b5fd6bf32`, and `sourceDirty: true`. Logs are `work/native-022-reviewed-release.log` and `work/native-022-reviewed-debug.log`. Both relocated packages pass `relocatedFreshCampaign` with only Windows system paths and resolve astronomy data beside the executable. This is not a separate clean-machine certification.

Release complete-initialization means over three repeats on this host were 8.15 ms (250 systems), 16.94 ms (500), 46.12 ms (1000), and 414.50 ms (2500). These measure initialization, not frame rate or campaign ticking. The exporter checks report/data consistency and rejects damaged economy, construction, shipyard, fleet, or parity-boundary records; CLI failures preserve existing output and print terminal diagnostics.

The completed 0.1.11 clean checkpoint is `4763cba2810e5c109d5834a26ea0072b5fd6bf32`, package `Builds/Windows/StellarContinuum-windows-benchmark-4763cba2-20260913T035032571557Z`, engine 0.1.11, `sourceDirty: false`. All six CI workflows passed: native `34736417345`, build `34736417346`, Windows `34736417374`, research `34736417350`, voice `34736417372`, screenshots `34736417364`.
