# Windows short-path diagnostic regression

The 0.1.35 native GitHub job `103775980550` (run `34776690148`) passed all 84 CTest checks but failed one Python assertion in `test_adaptive_campaign_uses_resolved_assets_and_fails_cleanly`. The expected research path used `C:\Users\runneradmin`; the native diagnostic correctly retained the equivalent Windows alias `C:\Users\RUNNER~1`. All other 27 Python checks passed. This was a test path-spelling assumption, not a failure to detect missing research data.

The test now resolves existing ancestors before passing its explicit asset root to the native process. The exporter applies the same rule to the relocated runtime directory. Resolving only a missing child after its directory was renamed cannot reliably expand every ancestor alias. Complete path, exception text, working directory and nonzero exit assertions remain intact; production path loading is unchanged.

A maintained Windows regression obtains a real alias with `GetShortPathNameW`, runs the existing asset-root/missing-data/corrupt-data checks through it, and restores the test root afterward. It skips only when Windows cannot supply a distinct short alias. The isolated 29-test run passed with the real alias exercised. Engine 0.1.36 combined validation and clean CI rerun provide the integration evidence.

This change normalizes paths controlled by the test/exporter. It does not erase, shorten or weaken error diagnostics, alter gameplay state, or require players to change their Windows user directory.

Maintained 0.1.36 validation passed 87/87 CTest and 29/29 Python, with the real Windows alias test executed successfully (`work/native-036-testing.log`). Exact-commit export and GitHub CI follow.
