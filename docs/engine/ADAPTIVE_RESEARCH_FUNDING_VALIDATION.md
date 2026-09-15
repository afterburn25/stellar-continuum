# Gate 065 validation

The retained fixture contains 47 actual-source rows from
`AdaptiveResearchFunding.cs`: nineteen campaign commands, eleven quotes,
eight treasury-runway calculations, six complexity-policy lookups, and three
start-credit calculations. The fixture SHA-256 is
`A294EC1AE8FF570040B0B149B2B22D44958EAE5781EA32621FD2A7F658D50F2E`.
The retained generator SHA-256 is
`EC58AFEDD95B9F5099640CC203CE9F56F182F93541073996E84E9361DB9A41F1`.
The canonical research fingerprint before and after every production call is
`2E76D70C9E270E69DF4BFC66B4135419720A0BA5CCA0B2D61DD5906FA22975B1`.

Both strict configurations passed all 47 rows with MSVC
`/W4 /WX /permissive- /fp:precise`:

```powershell
python work/065-research-funding/build_strict.py debug
python work/065-research-funding/build_strict.py release
```

Object and program-database outputs are contained below the respective
`build-debug` and `build-release` directories through explicit `/Fo` and `/Fd`
arguments.

The command rows compare the exact returned result or typed exception, complete
blocker fields, treasury balance, civilization revision, active project, and
ordered funding state after each call. They include successful start, pause,
and resume operations; Authority rejection without a reservation; duplicate or
missing economies; insufficient funds; duplicate funding; invalid labs;
hypothesis-resolution pause precedence; and the source's distinct Start and
Resume exception handling. Resume proves that no second authorization or
milestone debit occurs. Quote rows cover all four complexity policies,
nonfinite and boundary lab values, and a copied mutated node definition to show
that the passed definition supplies the project requirements while the catalog
still supplies stage scaling.

The retained managed generator exits 1 with its full exception and stack,
current directory, research root, and fixture path for missing arguments and a
missing research root. Those `dotnet run --no-build` results are retained in
`managed-missing-args.log` and `managed-wrong-root.log`.

Maintained integration is engine0.1.30. The exported host remains the legacy
campaign; these focused gates do not establish full Adaptive Research campaign
advance, diplomacy composition, player save compatibility or graphical parity.

Maintained engine0.1.30 validation passed75/75 CTest and20/20 Python checks (`work/native-030-testing-embedded.log`). Exact committed package verification follows.
