# Native cold-build budget

Engine 0.1.50's local preview link failed with LNK1116 / Windows error 112
because the development drive was full. The ignored `build-native/` tree
contained 268 incremental linker (`.ilk`) caches totalling 10,377,218,652
bytes. Those generated caches were removed after checking every absolute
path stayed within the build tree. One linker PDB left incomplete by the
disk exhaustion was then rebuilt after LNK1285 identified it explicitly.
Sources, saves, object libraries and sealed packages were retained.

MSVC RelWithDebInfo builds now link with `/INCREMENTAL:NO`. Debug symbols
remain available, while preview/export/test targets no longer accumulate
these incremental linker caches. This changes build storage use, not game
behavior or validation deadlines.

The 0.1.42 native CI run `34800861449`, job `103843090974`, hit the
900-second compilation deadline. Its log showed continuous progress through
action 550/658 before timeout and 552/658 during process cleanup, with no
compiler diagnostic. Tests had not started. The local maintained build and
exact committed export at `0921c50182f021991befd2ac5dd80c3abcc623c5` both
passed 104/104 CTest and 29/29 Python checks.

Compilation now has a bounded 1,800-second allowance. The workflow allows
45 minutes for checkout, tools, compilation, tests, export checks and upload.
Parallelism stays at four; CTest, Python and individual runtime checks retain
their existing deadlines. No test is skipped and no failure is converted to
success. This change addresses runner compilation capacity, not game speed.

The repair commit `a47b82763a6f0bd5dd06a99fb700ea8c4d2bd406` passed all six
remote workflows, including native run `34802101782`. Remote validation is
recorded in draft PR #325 and coordination issue #324. The original 0.1.42 package remains evidence for its
exact source commit; a later package must record its own commit and checks.
