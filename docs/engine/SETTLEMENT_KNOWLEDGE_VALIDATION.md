# Settlement knowledge validation

Settlement knowledge parity covers 54 actual C# cases, including eight source errors, plus nine source-only null observations. Fixture SHA-256: `CCC6A84EAED3128279D4C2F9EED52ECB13021A6D674518754EB1F939DB17EFFA`. The combined maintained run passed 37/37 CTest and 19/19 Python checks in `work/native-026-028-live-testing.log`.

The retained generator reproduces the hash and standalone Release/Debug pass with `/W4 /WX`. Coverage preserves full-survey privacy, species lookup, occupancy, reservations, stable IDs, and const-view/read-only source observations. This gate does not create fresh colonies.
