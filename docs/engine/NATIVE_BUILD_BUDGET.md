# Native cold-build budget

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

Remote validation of this follow-up is recorded in draft PR #325 and
coordination issue #324. The original 0.1.42 package remains evidence for its
exact source commit; a later package must record its own commit and checks.
